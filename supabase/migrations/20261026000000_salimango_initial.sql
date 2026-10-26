-- =============================================================================
-- S.A.L.I.M.A.N.G.O. / Crab Grid Smart Aeration — initial Supabase schema
--
-- Data flow (see docs/SYSTEM-ARCHITECTURE.md):
--   ESP32 --HTTPS--> rpc/device_report(token, report)  -> device_state, readings,
--                                                          events, calibration_records,
--                                                          command acks
--                    <- pending commands + current settings (JSON)
--   Browser (anonymous Supabase session, no username/password)
--     rpc/claim_device_access(code)     QR / typed sticker code -> full access grant
--     rpc/request_command(...)          mode / relay / calibration requests
--     rpc/update_device_settings(...)   salinity thresholds (salMin / salMax)
--     SELECT + Realtime on device_state, device_settings, readings, commands,
--       events, calibration_records (RLS: only devices the session has claimed)
--
-- Access model (project decision): the sticker code opens the whole site,
-- including control, so farmers never need to remember a username/password.
-- The code is therefore the credential: rotating it (admin_rotate_access_code)
-- revokes every browser that used the old code.
--
-- Secrets are never stored in plain text: access codes and device tokens are
-- stored as SHA-256 hashes of high-entropy random values. No table is writable
-- by the anon/authenticated roles; every write goes through a SECURITY DEFINER
-- function that authorises and validates it.
--
-- Derived values (salinity via PSS-78, unionized-ammonia fraction via Emerson
-- et al. 1975) are computed once, in the firmware, because the AUTO bilge-pump
-- logic needs salinity locally. The database and dashboard store and display
-- what the device computed; they do not recompute it.
-- =============================================================================

create extension if not exists pgcrypto with schema extensions;

-- ---------------------------------------------------------------------------
-- Types
-- ---------------------------------------------------------------------------
create type public.operating_mode as enum ('AUTO', 'MANUAL', 'OFF');
create type public.command_status as enum
  ('pending', 'delivered', 'accepted', 'rejected', 'expired', 'superseded');
create type public.calibrated_sensor as enum ('ph', 'ec', 'do');

-- ---------------------------------------------------------------------------
-- Devices and access
-- ---------------------------------------------------------------------------
create table public.devices (
  id                uuid primary key default gen_random_uuid(),
  name              text not null check (length(name) between 1 and 80),
  active            boolean not null default true,
  created_at        timestamptz not null default now(),
  -- sha256(normalised sticker code); epoch bump revokes every browser grant
  access_code_hash  text not null unique,
  access_epoch      integer not null default 1,
  -- sha256(device token) used by the ESP32
  device_token_hash text not null unique
);

comment on table public.devices is
  'One row per ESP32 float node. Secret columns are hashes and are not readable by browser roles.';

create table public.device_access_grants (
  user_id      uuid not null,
  device_id    uuid not null references public.devices (id) on delete cascade,
  access_epoch integer not null,
  granted_at   timestamptz not null default now(),
  primary key (user_id, device_id)
);

-- ---------------------------------------------------------------------------
-- Adjustable device settings (requested here; the device reports the version
-- it has applied in device_state.settings_version).
-- Defaults and ranges: salMin 28 ppt, salMax 32 ppt, both 10–34 ppt,
-- salMax > salMin (docs/APP.md threshold table).
-- ---------------------------------------------------------------------------
create table public.device_settings (
  device_id   uuid primary key references public.devices (id) on delete cascade,
  version     integer not null default 1,
  sal_min_ppt numeric(4, 1) not null default 28 check (sal_min_ppt between 10 and 34),
  sal_max_ppt numeric(4, 1) not null default 32 check (sal_max_ppt between 10 and 34),
  updated_at  timestamptz not null default now(),
  updated_by  uuid,
  constraint device_settings_sal_order check (sal_max_ppt > sal_min_ppt)
);

-- ---------------------------------------------------------------------------
-- Telemetry
-- Measured-sensor status vocabulary (firmware/salimango_node/telemetry.h):
--   ok          valid measurement
--   degraded    value present but computed with a fallback (e.g. 25 °C
--               compensation because the DS18B20 faulted)
--   over_range  outside the probe/ADC measuring range; value withheld
--   fault       invalid/implausible signal; value withheld
--   not_present sensor not detected
-- Derived values (salinity, NH3 fraction) use:
--   ok | over_range | unavailable (a required input is missing/invalid)
-- A NULL value is never a legitimate zero.
-- ---------------------------------------------------------------------------
create table public.device_state (
  device_id            uuid primary key references public.devices (id) on delete cascade,
  updated_at           timestamptz not null default now(),
  last_history_at      timestamptz,
  seq                  bigint not null,
  uptime_s             bigint not null,
  firmware_version     text,
  simulated            boolean not null default false,
  wifi_rssi_dbm        integer,
  settings_version     integer,

  mode                 public.operating_mode not null,
  mode_source          text not null check (mode_source in ('selector', 'remote')),
  selector_position    text not null check (selector_position in ('AUTO', 'MANUAL', 'OFF', 'FAULT')),
  manual_expires_in_s  integer,
  do_emergency         boolean not null default false,
  relay_k1             boolean not null,
  relay_k2             boolean not null,
  relay_k3             boolean not null,
  relay_k4             boolean not null,

  ph                   double precision check (ph between 0 and 14),
  ph_status            text not null,
  ph_mv                double precision,
  ec_ms_cm             double precision check (ec_ms_cm >= 0),
  ec25_ms_cm           double precision check (ec25_ms_cm >= 0),
  ec_status            text not null,
  ec_mv                double precision,
  salinity_ppt         double precision check (salinity_ppt between 0 and 42),
  salinity_status      text not null,
  temp_c               double precision check (temp_c between -10 and 60),
  temp_status          text not null,
  temp2_c              double precision check (temp2_c between -10 and 60),
  do_mg_l              double precision check (do_mg_l >= 0 and do_mg_l < 50),
  do_status            text not null,
  do_mv                double precision,
  nh3_fraction         double precision check (nh3_fraction between 0 and 1),
  nh3_status           text not null,

  calibration          jsonb not null default '{}'::jsonb,
  faults               text[] not null default '{}',

  constraint device_state_status_values check (
    ph_status       in ('ok', 'degraded', 'over_range', 'fault', 'not_present') and
    ec_status       in ('ok', 'degraded', 'over_range', 'fault', 'not_present') and
    temp_status     in ('ok', 'degraded', 'over_range', 'fault', 'not_present') and
    do_status       in ('ok', 'degraded', 'over_range', 'fault', 'not_present') and
    salinity_status in ('ok', 'over_range', 'unavailable') and
    nh3_status      in ('ok', 'over_range', 'unavailable')),
  constraint device_state_withheld_values check (
    (ph_status       in ('ok', 'degraded') or ph is null) and
    (ec_status       in ('ok', 'degraded') or (ec_ms_cm is null and ec25_ms_cm is null)) and
    (temp_status     in ('ok', 'degraded') or temp_c is null) and
    (do_status       in ('ok', 'degraded') or do_mg_l is null) and
    (salinity_status = 'ok' or salinity_ppt is null) and
    (nh3_status      = 'ok' or nh3_fraction is null))
);

-- One row per minute at most; this is the table the dashboard exports to CSV.
create table public.readings (
  id               bigint generated always as identity primary key,
  device_id        uuid not null references public.devices (id) on delete cascade,
  recorded_at      timestamptz not null default now(),
  seq              bigint not null,
  simulated        boolean not null default false,
  mode             public.operating_mode not null,
  do_emergency     boolean not null,
  relay_k1         boolean not null,
  relay_k2         boolean not null,
  relay_k3         boolean not null,
  relay_k4         boolean not null,
  ph               double precision check (ph between 0 and 14),
  ph_status        text not null,
  ec_ms_cm         double precision check (ec_ms_cm >= 0),
  ec25_ms_cm       double precision check (ec25_ms_cm >= 0),
  ec_status        text not null,
  salinity_ppt     double precision check (salinity_ppt between 0 and 42),
  salinity_status  text not null,
  temp_c           double precision check (temp_c between -10 and 60),
  temp_status      text not null,
  do_mg_l          double precision check (do_mg_l >= 0 and do_mg_l < 50),
  do_status        text not null,
  nh3_fraction     double precision check (nh3_fraction between 0 and 1),
  nh3_status       text not null,
  -- calibration identity in force when the reading was taken
  ph_cal_id        bigint,
  ec_cal_id        bigint,
  do_cal_id        bigint
);

create index readings_device_time_idx on public.readings (device_id, recorded_at desc);

create table public.calibration_records (
  id               bigint generated always as identity primary key,
  device_id        uuid not null references public.devices (id) on delete cascade,
  sensor           public.calibrated_sensor not null,
  cal_id           bigint not null,          -- random id generated on the ESP32 at save time
  version          integer not null,          -- per-device save counter (NVS)
  parameters       jsonb not null,
  received_at      timestamptz not null default now(),
  firmware_version text,
  simulated        boolean not null default false,
  unique (device_id, sensor, cal_id)
);

create index calibration_records_device_idx
  on public.calibration_records (device_id, sensor, received_at desc);

create table public.events (
  id          bigint generated always as identity primary key,
  device_id   uuid not null references public.devices (id) on delete cascade,
  created_at  timestamptz not null default now(),
  severity    text not null check (severity in ('info', 'warning', 'critical')),
  kind        text not null check (length(kind) between 1 and 40),
  message     text not null check (length(message) <= 300)
);

create index events_device_time_idx on public.events (device_id, created_at desc);

-- ---------------------------------------------------------------------------
-- Commands (dashboard -> device). Requested state lives here; confirmed state
-- lives in device_state. They are never merged.
-- ---------------------------------------------------------------------------
create table public.commands (
  id             uuid primary key default gen_random_uuid(),
  device_id      uuid not null references public.devices (id) on delete cascade,
  created_at     timestamptz not null default now(),
  expires_at     timestamptz not null,
  kind           text not null check (kind in ('set_mode', 'set_relays', 'calibration')),
  payload        jsonb not null,
  status         public.command_status not null default 'pending',
  status_message text,
  delivered_at   timestamptz,
  resolved_at    timestamptz,
  requested_by   uuid
);

create index commands_device_status_idx on public.commands (device_id, status, created_at);
create index commands_device_time_idx on public.commands (device_id, created_at desc);

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------
create or replace function public.has_device_access(p_device uuid)
returns boolean
language sql stable security definer
set search_path = public, pg_temp
as $$
  select exists (
    select 1
    from public.device_access_grants g
    join public.devices d on d.id = g.device_id
    where g.user_id = auth.uid()
      and g.device_id = p_device
      and g.access_epoch = d.access_epoch
      and d.active);
$$;

-- Sticker codes use the Crockford base-32 alphabet; I/L read as 1, O as 0,
-- separators and case are ignored.
create or replace function public.normalize_access_code(p_code text)
returns text
language sql immutable
as $$
  select translate(upper(regexp_replace(coalesce(p_code, ''), '[^0-9A-Za-z]', '', 'g')), 'ILO', '110');
$$;

create or replace function public.sha256_hex(p_value text)
returns text
language sql immutable
set search_path = public, pg_temp
as $$
  select encode(sha256(convert_to(p_value, 'UTF8')), 'hex');
$$;

-- ---------------------------------------------------------------------------
-- Browser RPCs (role: authenticated, i.e. Supabase anonymous sessions)
-- ---------------------------------------------------------------------------
create or replace function public.claim_device_access(p_code text)
returns jsonb
language plpgsql security definer
set search_path = public, pg_temp
as $$
declare
  v_code   text := public.normalize_access_code(p_code);
  v_device public.devices%rowtype;
begin
  if auth.uid() is null then
    raise exception 'A browser session is required' using errcode = '28000';
  end if;
  if length(v_code) < 8 then
    raise exception 'Access code not recognised' using errcode = 'P0002';
  end if;

  select * into v_device
  from public.devices
  where access_code_hash = public.sha256_hex(v_code) and active;

  if not found then
    raise exception 'Access code not recognised' using errcode = 'P0002';
  end if;

  insert into public.device_access_grants (user_id, device_id, access_epoch)
  values (auth.uid(), v_device.id, v_device.access_epoch)
  on conflict (user_id, device_id) do update
    set access_epoch = excluded.access_epoch, granted_at = now();

  return jsonb_build_object('device_id', v_device.id, 'name', v_device.name);
end;
$$;

create or replace function public.server_time()
returns timestamptz
language sql stable
as $$ select now(); $$;

-- Validates and queues a command. The ESP32 validates again before actuating.
create or replace function public.request_command(p_device uuid, p_kind text, p_payload jsonb)
returns uuid
language plpgsql security definer
set search_path = public, pg_temp
as $$
declare
  v_id   uuid;
  v_keys text[];
begin
  if not public.has_device_access(p_device) then
    raise exception 'No access to this device. Scan the QR code or type the access code again.'
      using errcode = '42501';
  end if;
  if p_payload is null or jsonb_typeof(p_payload) <> 'object' then
    raise exception 'Command payload must be an object' using errcode = '22023';
  end if;

  select coalesce(array_agg(k order by k), '{}') into v_keys from jsonb_object_keys(p_payload) k;

  if p_kind = 'set_mode' then
    if not (v_keys <@ array['confirm', 'mode'])
       or coalesce(p_payload ->> 'mode', '') not in ('AUTO', 'MANUAL', 'OFF') then
      raise exception 'set_mode requires {"mode": "AUTO"|"MANUAL"|"OFF"}' using errcode = '22023';
    end if;
    if p_payload ->> 'mode' = 'OFF' and coalesce(p_payload -> 'confirm', 'false'::jsonb) <> 'true'::jsonb then
      raise exception 'OFF must be explicitly confirmed' using errcode = '22023';
    end if;
  elsif p_kind = 'set_relays' then
    if cardinality(v_keys) = 0 or not (v_keys <@ array['k1', 'k2', 'k3', 'k4']) then
      raise exception 'set_relays accepts only k1..k4' using errcode = '22023';
    end if;
    if exists (select 1 from jsonb_each(p_payload) e where jsonb_typeof(e.value) <> 'boolean') then
      raise exception 'Relay states must be true or false' using errcode = '22023';
    end if;
  elsif p_kind = 'calibration' then
    if v_keys <> array['action', 'sensor']
       or coalesce(p_payload ->> 'sensor', '') not in ('ph', 'ec', 'do')
       or coalesce(p_payload ->> 'action', '') not in ('enter', 'capture', 'save', 'abort') then
      raise exception 'calibration requires {"sensor": "ph"|"ec"|"do", "action": "enter"|"capture"|"save"|"abort"}'
        using errcode = '22023';
    end if;
  else
    raise exception 'Unknown command kind %', p_kind using errcode = '22023';
  end if;

  -- A newer request of the same kind replaces one the device has not yet received.
  update public.commands
     set status = 'superseded', resolved_at = now(),
         status_message = 'Replaced by a newer request before delivery'
   where device_id = p_device and kind = p_kind and status = 'pending'
     and (p_kind <> 'calibration' or payload ->> 'sensor' = p_payload ->> 'sensor');

  insert into public.commands (device_id, kind, payload, expires_at, requested_by)
  values (p_device, p_kind, p_payload, now() + interval '90 seconds', auth.uid())
  returning id into v_id;

  return v_id;
end;
$$;

-- Salinity thresholds for the AUTO bilge-pump logic. The device picks up the
-- new version on its next report and echoes it in device_state.settings_version.
create or replace function public.update_device_settings(p_device uuid, p_sal_min_ppt numeric, p_sal_max_ppt numeric)
returns integer
language plpgsql security definer
set search_path = public, pg_temp
as $$
declare
  v_version integer;
begin
  if not public.has_device_access(p_device) then
    raise exception 'No access to this device. Scan the QR code or type the access code again.'
      using errcode = '42501';
  end if;
  if p_sal_min_ppt is null or p_sal_max_ppt is null
     or p_sal_min_ppt not between 10 and 34 or p_sal_max_ppt not between 10 and 34 then
    raise exception 'salMin and salMax must be between 10 and 34 ppt' using errcode = '22023';
  end if;
  if p_sal_max_ppt <= p_sal_min_ppt then
    raise exception 'salMax must be higher than salMin' using errcode = '22023';
  end if;

  update public.device_settings
     set sal_min_ppt = round(p_sal_min_ppt, 1), sal_max_ppt = round(p_sal_max_ppt, 1),
         version = version + 1, updated_at = now(), updated_by = auth.uid()
   where device_id = p_device
  returning version into v_version;

  return v_version;
end;
$$;

-- ---------------------------------------------------------------------------
-- Device RPC (role: anon — the ESP32 has no user session). Authorised by the
-- per-device token. One call per report cycle: stores state, history (at most
-- once per 55 s), events, calibration records and command acknowledgements,
-- then returns the commands waiting for the device and the current settings.
-- ---------------------------------------------------------------------------
create or replace function public.device_report(p_token text, p_report jsonb)
returns jsonb
language plpgsql security definer
set search_path = public, pg_temp
as $$
declare
  v_device    uuid;
  v_simulated boolean;
  v_last_hist timestamptz;
  v_item      jsonb;
  v_sensor    text;
  v_cmds      jsonb;
  v_settings  jsonb;
  v_store     boolean;
begin
  select id into v_device
  from public.devices
  where device_token_hash = public.sha256_hex(coalesce(p_token, '')) and active;
  if v_device is null then
    raise exception 'Unknown or revoked device token' using errcode = '28000';
  end if;
  if p_report is null or jsonb_typeof(p_report) <> 'object' then
    raise exception 'Report must be a JSON object' using errcode = '22023';
  end if;

  v_simulated := coalesce((p_report ->> 'simulated')::boolean, false);

  select last_history_at into v_last_hist from public.device_state where device_id = v_device;
  v_store := v_last_hist is null or v_last_hist < now() - interval '55 seconds';

  insert into public.device_state as s (
    device_id, updated_at, last_history_at, seq, uptime_s, firmware_version, simulated, wifi_rssi_dbm,
    settings_version, mode, mode_source, selector_position, manual_expires_in_s, do_emergency,
    relay_k1, relay_k2, relay_k3, relay_k4,
    ph, ph_status, ph_mv, ec_ms_cm, ec25_ms_cm, ec_status, ec_mv, salinity_ppt, salinity_status,
    temp_c, temp_status, temp2_c, do_mg_l, do_status, do_mv, nh3_fraction, nh3_status,
    calibration, faults)
  values (
    v_device, now(), case when v_store then now() else v_last_hist end,
    (p_report ->> 'seq')::bigint, (p_report ->> 'uptime_s')::bigint, p_report ->> 'fw', v_simulated,
    (p_report ->> 'rssi')::integer, (p_report ->> 'settings_version')::integer,
    (p_report ->> 'mode')::public.operating_mode, p_report ->> 'mode_source', p_report ->> 'selector',
    (p_report ->> 'manual_expires_in_s')::integer, coalesce((p_report ->> 'do_emergency')::boolean, false),
    (p_report #>> '{relays,k1}')::boolean, (p_report #>> '{relays,k2}')::boolean,
    (p_report #>> '{relays,k3}')::boolean, (p_report #>> '{relays,k4}')::boolean,
    (p_report #>> '{ph,value}')::double precision, p_report #>> '{ph,status}', (p_report #>> '{ph,mv}')::double precision,
    (p_report #>> '{ec,ec_ms_cm}')::double precision, (p_report #>> '{ec,ec25_ms_cm}')::double precision,
    p_report #>> '{ec,status}', (p_report #>> '{ec,mv}')::double precision,
    (p_report #>> '{salinity,value}')::double precision, p_report #>> '{salinity,status}',
    (p_report #>> '{temp,value}')::double precision, p_report #>> '{temp,status}', (p_report #>> '{temp,t2}')::double precision,
    (p_report #>> '{do,value}')::double precision, p_report #>> '{do,status}', (p_report #>> '{do,mv}')::double precision,
    (p_report #>> '{nh3,fraction}')::double precision, p_report #>> '{nh3,status}',
    coalesce(p_report -> 'cal', '{}'::jsonb),
    coalesce(array(select jsonb_array_elements_text(p_report -> 'faults')), '{}'))
  on conflict (device_id) do update set
    updated_at = excluded.updated_at, last_history_at = excluded.last_history_at,
    seq = excluded.seq, uptime_s = excluded.uptime_s, firmware_version = excluded.firmware_version,
    simulated = excluded.simulated, wifi_rssi_dbm = excluded.wifi_rssi_dbm,
    settings_version = excluded.settings_version,
    mode = excluded.mode, mode_source = excluded.mode_source, selector_position = excluded.selector_position,
    manual_expires_in_s = excluded.manual_expires_in_s, do_emergency = excluded.do_emergency,
    relay_k1 = excluded.relay_k1, relay_k2 = excluded.relay_k2,
    relay_k3 = excluded.relay_k3, relay_k4 = excluded.relay_k4,
    ph = excluded.ph, ph_status = excluded.ph_status, ph_mv = excluded.ph_mv,
    ec_ms_cm = excluded.ec_ms_cm, ec25_ms_cm = excluded.ec25_ms_cm, ec_status = excluded.ec_status, ec_mv = excluded.ec_mv,
    salinity_ppt = excluded.salinity_ppt, salinity_status = excluded.salinity_status,
    temp_c = excluded.temp_c, temp_status = excluded.temp_status, temp2_c = excluded.temp2_c,
    do_mg_l = excluded.do_mg_l, do_status = excluded.do_status, do_mv = excluded.do_mv,
    nh3_fraction = excluded.nh3_fraction, nh3_status = excluded.nh3_status,
    calibration = excluded.calibration, faults = excluded.faults;

  if v_store then
    insert into public.readings (
      device_id, seq, simulated, mode, do_emergency, relay_k1, relay_k2, relay_k3, relay_k4,
      ph, ph_status, ec_ms_cm, ec25_ms_cm, ec_status, salinity_ppt, salinity_status,
      temp_c, temp_status, do_mg_l, do_status, nh3_fraction, nh3_status,
      ph_cal_id, ec_cal_id, do_cal_id)
    select device_id, seq, simulated, mode, do_emergency, relay_k1, relay_k2, relay_k3, relay_k4,
           ph, ph_status, ec_ms_cm, ec25_ms_cm, ec_status, salinity_ppt, salinity_status,
           temp_c, temp_status, do_mg_l, do_status, nh3_fraction, nh3_status,
           (calibration #>> '{ph,cal_id}')::bigint, (calibration #>> '{ec,cal_id}')::bigint,
           (calibration #>> '{do,cal_id}')::bigint
    from public.device_state where device_id = v_device;
  end if;

  -- Calibration records: one row per (sensor, cal_id) the device reports as active.
  foreach v_sensor in array array['ph', 'ec', 'do'] loop
    v_item := p_report #> array['cal', v_sensor];
    if v_item is not null and v_item ->> 'state' = 'calibrated' and v_item ? 'cal_id' then
      insert into public.calibration_records (device_id, sensor, cal_id, version, parameters, firmware_version, simulated)
      values (v_device, v_sensor::public.calibrated_sensor, (v_item ->> 'cal_id')::bigint,
              (v_item ->> 'version')::integer, coalesce(v_item -> 'params', '{}'::jsonb),
              p_report ->> 'fw', v_simulated)
      on conflict (device_id, sensor, cal_id) do nothing;
    end if;
  end loop;

  for v_item in select * from jsonb_array_elements(coalesce(p_report -> 'events', '[]'::jsonb)) loop
    insert into public.events (device_id, severity, kind, message)
    values (v_device, v_item ->> 'severity', v_item ->> 'kind', left(v_item ->> 'message', 300));
  end loop;

  -- Acknowledgements. An ack that arrives after expiry still records what the
  -- device actually did, as long as the command had been delivered.
  for v_item in select * from jsonb_array_elements(coalesce(p_report -> 'acks', '[]'::jsonb)) loop
    if v_item ->> 'status' not in ('accepted', 'rejected') then
      raise exception 'Ack status must be accepted or rejected' using errcode = '22023';
    end if;
    update public.commands
       set status = (v_item ->> 'status')::public.command_status,
           status_message = left(v_item ->> 'message', 200),
           resolved_at = now()
     where id = (v_item ->> 'id')::uuid and device_id = v_device
       and (status in ('pending', 'delivered') or (status = 'expired' and delivered_at is not null));
  end loop;

  update public.commands
     set status = 'expired', resolved_at = now(),
         status_message = case when delivered_at is null
                               then 'Device did not collect the request before it expired'
                               else 'No acknowledgement from the device' end
   where device_id = v_device and status in ('pending', 'delivered')
     and expires_at < now() - case when status = 'delivered' then interval '30 seconds' else interval '0' end;

  -- Deliver pending commands; re-deliver unacknowledged ones after 15 s in case
  -- the previous response was lost (the firmware de-duplicates by id).
  with picked as (
    update public.commands c
       set status = 'delivered', delivered_at = coalesce(c.delivered_at, now())
     where c.id in (
       select id from public.commands
        where device_id = v_device and expires_at > now()
          and (status = 'pending' or (status = 'delivered' and delivered_at < now() - interval '15 seconds'))
        order by created_at
        limit 4)
    returning c.id, c.kind, c.payload, c.created_at, c.expires_at)
  select jsonb_agg(jsonb_build_object(
           'id', id, 'kind', kind, 'payload', payload,
           'expires_in_s', greatest(0, floor(extract(epoch from expires_at - now())))::integer)
         order by created_at)
    into v_cmds
  from picked;

  select jsonb_build_object('version', version, 'sal_min_ppt', sal_min_ppt, 'sal_max_ppt', sal_max_ppt)
    into v_settings
  from public.device_settings where device_id = v_device;

  return jsonb_build_object('commands', coalesce(v_cmds, '[]'::jsonb),
                            'settings', v_settings,
                            'server_time', now());
end;
$$;

-- ---------------------------------------------------------------------------
-- Administration (service_role / SQL editor only)
-- ---------------------------------------------------------------------------
create or replace function public.admin_register_device(p_name text, p_access_code text, p_device_token text)
returns uuid
language plpgsql security definer
set search_path = public, pg_temp
as $$
declare
  v_id uuid;
begin
  if length(public.normalize_access_code(p_access_code)) < 8 then
    raise exception 'Access code must have at least 8 base-32 characters';
  end if;
  if length(p_device_token) < 32 then
    raise exception 'Device token must be at least 32 characters';
  end if;
  insert into public.devices (name, access_code_hash, device_token_hash)
  values (p_name, public.sha256_hex(public.normalize_access_code(p_access_code)),
          public.sha256_hex(p_device_token))
  returning id into v_id;
  insert into public.device_settings (device_id) values (v_id);
  return v_id;
end;
$$;

-- Replaces the sticker code and revokes every browser that used the old one.
create or replace function public.admin_rotate_access_code(p_device uuid, p_new_code text)
returns void
language plpgsql security definer
set search_path = public, pg_temp
as $$
begin
  if length(public.normalize_access_code(p_new_code)) < 8 then
    raise exception 'Access code must have at least 8 base-32 characters';
  end if;
  update public.devices
     set access_code_hash = public.sha256_hex(public.normalize_access_code(p_new_code)),
         access_epoch = access_epoch + 1
   where id = p_device;
  if not found then raise exception 'Unknown device'; end if;
end;
$$;

create or replace function public.admin_rotate_device_token(p_device uuid, p_new_token text)
returns void
language plpgsql security definer
set search_path = public, pg_temp
as $$
begin
  if length(p_new_token) < 32 then
    raise exception 'Device token must be at least 32 characters';
  end if;
  update public.devices set device_token_hash = public.sha256_hex(p_new_token) where id = p_device;
  if not found then raise exception 'Unknown device'; end if;
end;
$$;

create or replace function public.admin_set_device_active(p_device uuid, p_active boolean)
returns void
language sql security definer
set search_path = public, pg_temp
as $$
  update public.devices set active = p_active where id = p_device;
$$;

-- Telemetry retention: history rows older than the window are deleted.
-- Calibration records, events and commands are kept. Export CSV before pruning.
create or replace function public.prune_old_readings(p_keep interval default interval '90 days')
returns bigint
language plpgsql security definer
set search_path = public, pg_temp
as $$
declare
  v_count bigint;
begin
  delete from public.readings where recorded_at < now() - p_keep;
  get diagnostics v_count = row_count;
  return v_count;
end;
$$;

-- ---------------------------------------------------------------------------
-- Privileges and Row Level Security
-- ---------------------------------------------------------------------------
alter table public.devices              enable row level security;
alter table public.device_access_grants enable row level security;
alter table public.device_settings      enable row level security;
alter table public.device_state         enable row level security;
alter table public.readings             enable row level security;
alter table public.calibration_records  enable row level security;
alter table public.events               enable row level security;
alter table public.commands             enable row level security;

revoke all on public.devices, public.device_access_grants, public.device_settings, public.device_state,
              public.readings, public.calibration_records, public.events, public.commands
  from anon, authenticated;

grant select (id, name, active, created_at) on public.devices to authenticated;
grant select on public.device_access_grants, public.device_settings, public.device_state, public.readings,
                public.calibration_records, public.events, public.commands
  to authenticated;

create policy devices_read on public.devices
  for select to authenticated using (public.has_device_access(id));
create policy grants_read_own on public.device_access_grants
  for select to authenticated using (user_id = auth.uid());
create policy device_settings_read on public.device_settings
  for select to authenticated using (public.has_device_access(device_id));
create policy device_state_read on public.device_state
  for select to authenticated using (public.has_device_access(device_id));
create policy readings_read on public.readings
  for select to authenticated using (public.has_device_access(device_id));
create policy calibration_records_read on public.calibration_records
  for select to authenticated using (public.has_device_access(device_id));
create policy events_read on public.events
  for select to authenticated using (public.has_device_access(device_id));
create policy commands_read on public.commands
  for select to authenticated using (public.has_device_access(device_id));

revoke all on function
  public.has_device_access(uuid), public.normalize_access_code(text), public.sha256_hex(text),
  public.claim_device_access(text), public.server_time(), public.request_command(uuid, text, jsonb),
  public.update_device_settings(uuid, numeric, numeric), public.device_report(text, jsonb),
  public.admin_register_device(text, text, text), public.admin_rotate_access_code(uuid, text),
  public.admin_rotate_device_token(uuid, text), public.admin_set_device_active(uuid, boolean),
  public.prune_old_readings(interval)
  from public, anon, authenticated;

grant execute on function
  public.has_device_access(uuid), public.claim_device_access(text), public.server_time(),
  public.request_command(uuid, text, jsonb), public.update_device_settings(uuid, numeric, numeric)
  to authenticated;

grant execute on function public.device_report(text, jsonb) to anon, authenticated;

grant execute on function
  public.admin_register_device(text, text, text), public.admin_rotate_access_code(uuid, text),
  public.admin_rotate_device_token(uuid, text), public.admin_set_device_active(uuid, boolean),
  public.prune_old_readings(interval)
  to service_role;

-- ---------------------------------------------------------------------------
-- Realtime and retention scheduling
-- ---------------------------------------------------------------------------
do $$
begin
  if not exists (select 1 from pg_publication where pubname = 'supabase_realtime') then
    create publication supabase_realtime;
  end if;
end;
$$;

alter publication supabase_realtime add table
  public.device_state, public.device_settings, public.readings, public.commands,
  public.events, public.calibration_records;

-- Daily pruning when pg_cron is enabled (Database > Extensions > pg_cron).
-- Without pg_cron nothing is deleted automatically.
do $$
begin
  if exists (select 1 from pg_extension where extname = 'pg_cron') then
    perform cron.schedule('salimango-prune-readings', '17 3 * * *',
                          'select public.prune_old_readings()');
  end if;
end;
$$;
