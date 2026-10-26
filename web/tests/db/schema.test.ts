// @vitest-environment node
import { beforeAll, describe, expect, it } from 'vitest';
import { as, createDb, type Db } from './harness';

const USER_A = '11111111-1111-4111-8111-111111111111';
const USER_B = '22222222-2222-4222-8222-222222222222';
const CODE = 'K7QM3XRP';
const TOKEN = 'tok_' + 'a'.repeat(40);

function report(overrides: Record<string, unknown> = {}) {
  return {
    seq: 1,
    uptime_s: 30,
    fw: 'test',
    simulated: false,
    rssi: -61,
    settings_version: 1,
    mode: 'AUTO',
    mode_source: 'selector',
    selector: 'AUTO',
    manual_expires_in_s: null,
    do_emergency: false,
    relays: { k1: true, k3: false, k4: false, k5: true },
    bilge: { state: 'IDLE', fail_count: 0 },
    ph: { value: 7.9, status: 'ok', mv: 1580 },
    ec: { ec_ms_cm: 12.1, ec25_ms_cm: 11.2, status: 'ok', mv: 1980 },
    salinity: { value: 6.9, status: 'ok' },
    temp: { value: 29.1, status: 'ok', t2: null },
    do: { value: 6.2, status: 'ok', mv: 1100 },
    nh3: { fraction: 0.0712, status: 'ok' },
    cal: {
      ph: { state: 'calibrated', version: 2, cal_id: 1234, params: { ph7_v: 2.51, slope_v: 0.172, cal_t_c: 26.1 } },
      ec: { state: 'default', version: 0 },
      do: { state: 'default', version: 0 },
    },
    faults: [],
    acks: [],
    events: [],
    ...overrides,
  };
}

type Settings = {
  version: number;
  sal_min_ppt: number;
  sal_max_ppt: number;
  sal_hyst_ppt: number;
  bilge_max_run_min: number;
  bilge_min_off_min: number;
  bilge_max_fails: number;
};
type ReportResponse = { commands: Array<{ id: string; kind: string; payload: unknown }>; settings: Settings };

const DEFAULT_SETTINGS = {
  sal_min_ppt: 28,
  sal_max_ppt: 32,
  sal_hyst_ppt: 2,
  bilge_max_run_min: 15,
  bilge_min_off_min: 30,
  bilge_max_fails: 3,
};

async function deviceReport(db: Db, body: unknown, token = TOKEN): Promise<ReportResponse> {
  return as(db, 'anon', null, async () => {
    const r = await db.query<{ device_report: ReportResponse }>('select public.device_report($1, $2::jsonb)', [
      token,
      JSON.stringify(body),
    ]);
    return r.rows[0].device_report;
  });
}

describe('Supabase schema (PGlite)', () => {
  let db: Db;
  let deviceId: string;

  const command = (uid: string, kind: string, payload: unknown) =>
    as(db, 'authenticated', uid, () =>
      db.query<{ id: string }>(`select public.request_command($1, $2, $3::jsonb) as id`, [
        deviceId,
        kind,
        JSON.stringify(payload),
      ]),
    );

  beforeAll(async () => {
    db = await createDb();
    deviceId = await as(db, 'service_role', null, async () => {
      const r = await db.query<{ id: string }>('select public.admin_register_device($1, $2, $3) as id', [
        'Float node 1',
        'K7QM-3XRP',
        TOKEN,
      ]);
      return r.rows[0].id;
    });
  }, 60_000);

  it('stores only hashes of secrets', async () => {
    const r = await db.query('select * from public.devices');
    const row = JSON.stringify(r.rows[0]);
    expect(row).not.toContain(CODE);
    expect(row).not.toContain(TOKEN);
  });

  it('gives anon no direct table access', async () => {
    for (const table of ['device_state', 'devices', 'readings', 'device_settings']) {
      await expect(as(db, 'anon', null, () => db.query(`select * from public.${table}`))).rejects.toThrow(
        /permission denied/,
      );
    }
  });

  it('keeps admin functions away from browser and device roles', async () => {
    for (const role of ['anon', 'authenticated'] as const) {
      await expect(
        as(db, role, USER_A, () => db.query(`select public.admin_rotate_access_code($1, 'ZZZZZZZZ')`, [deviceId])),
      ).rejects.toThrow(/permission denied/);
    }
  });

  it('hides secret columns even from granted users', async () => {
    await expect(
      as(db, 'authenticated', USER_A, () => db.query('select access_code_hash from public.devices')),
    ).rejects.toThrow(/permission denied/);
  });

  it('gives browsers no direct write access', async () => {
    await expect(
      as(db, 'authenticated', USER_A, () =>
        db.query(`update public.device_settings set sal_min_ppt = 10 where device_id = $1`, [deviceId]),
      ),
    ).rejects.toThrow(/permission denied/);
  });

  it('rejects reports with an unknown token', async () => {
    await expect(deviceReport(db, report(), 'tok_wrong_' + 'b'.repeat(40))).rejects.toThrow(
      /Unknown or revoked device token/,
    );
  });

  it('accepts a valid report, returns settings, and stores one history row per minute', async () => {
    const res = await deviceReport(db, report());
    expect(res.commands).toEqual([]);
    expect(res.settings).toEqual({ version: 1, ...DEFAULT_SETTINGS });
    await deviceReport(db, report({ seq: 2 }));
    const readings = await db.query<{ salinity_ppt: number; nh3_fraction: number }>(
      'select salinity_ppt, nh3_fraction from public.readings',
    );
    expect(readings.rows).toEqual([{ salinity_ppt: 6.9, nh3_fraction: 0.0712 }]);
    const state = await db.query<{ seq: string }>('select seq from public.device_state');
    expect(Number(state.rows[0].seq)).toBe(2);
    const cal = await db.query('select * from public.calibration_records');
    expect(cal.rows).toHaveLength(1);
  });

  it('refuses a withheld-status value (a sensor error is never stored as a number)', async () => {
    await expect(deviceReport(db, report({ seq: 3, ph: { value: 7.0, status: 'fault', mv: 0 } }))).rejects.toThrow(
      /device_state_withheld_values/,
    );
    await expect(
      deviceReport(db, report({ seq: 3, salinity: { value: 30, status: 'over_range' } })),
    ).rejects.toThrow(/device_state_withheld_values/);
    // an over-range EC with no value is a legitimate report
    await deviceReport(
      db,
      report({
        seq: 3,
        ec: { ec_ms_cm: null, ec25_ms_cm: null, status: 'over_range', mv: 3050 },
        salinity: { value: null, status: 'over_range' },
      }),
    );
  });

  it('does not show device data before the access code is claimed', async () => {
    const rows = await as(db, 'authenticated', USER_A, () => db.query('select * from public.device_state'));
    expect(rows.rows).toHaveLength(0);
    await expect(command(USER_A, 'set_mode', { mode: 'MANUAL' })).rejects.toThrow(/No access/);
  });

  it('rejects an unknown access code and accepts the sticker code in any typed form', async () => {
    await expect(
      as(db, 'authenticated', USER_A, () => db.query(`select public.claim_device_access('ZZZZ-ZZZZ')`)),
    ).rejects.toThrow(/not recognised/);
    // lower case, spaces and the letter O/I typed for 0/1 are tolerated
    const claimed = await as(db, 'authenticated', USER_A, async () => {
      const r = await db.query<{ claim_device_access: { device_id: string } }>(
        `select public.claim_device_access(' k7qm 3xrp ')`,
      );
      return r.rows[0].claim_device_access;
    });
    expect(claimed.device_id).toBe(deviceId);
    const rows = await as(db, 'authenticated', USER_A, () => db.query('select * from public.device_state'));
    expect(rows.rows).toHaveLength(1);
    const other = await as(db, 'authenticated', USER_B, () => db.query('select * from public.device_state'));
    expect(other.rows).toHaveLength(0);
  });

  it('validates command payloads and requires confirmation for OFF', async () => {
    await expect(command(USER_A, 'set_mode', { mode: 'OFF' })).rejects.toThrow(/explicitly confirmed/);
    await expect(command(USER_A, 'set_mode', { mode: 'TURBO' })).rejects.toThrow(/set_mode requires/);
    await expect(command(USER_A, 'set_relays', { k2: true })).rejects.toThrow(/only k1, k3, k4 and k5/);
    await expect(command(USER_A, 'clear_bilge_lockout', { force: true })).rejects.toThrow(/no parameters/);
    await expect(command(USER_A, 'set_relays', { k1: 'on' })).rejects.toThrow(/true or false/);
    await expect(command(USER_A, 'calibration', { sensor: 'temp', action: 'enter' })).rejects.toThrow(
      /calibration requires/,
    );
    await expect(command(USER_A, 'reboot', {})).rejects.toThrow(/Unknown command kind/);
    const first = await command(USER_A, 'set_mode', { mode: 'MANUAL' });
    const second = await command(USER_A, 'set_mode', { mode: 'OFF', confirm: true });
    const statuses = await db.query<{ id: string; status: string }>('select id, status from public.commands');
    const byId = Object.fromEntries(statuses.rows.map((r) => [r.id, r.status]));
    expect(byId[first.rows[0].id]).toBe('superseded');
    expect(byId[second.rows[0].id]).toBe('pending');
  });

  it('delivers commands to the device and records its acknowledgement', async () => {
    const res = await deviceReport(db, report({ seq: 4 }));
    expect(res.commands).toHaveLength(1);
    const cmd = res.commands[0];
    expect(cmd.kind).toBe('set_mode');
    // not re-delivered immediately
    expect((await deviceReport(db, report({ seq: 5 }))).commands).toHaveLength(0);
    await deviceReport(
      db,
      report({ seq: 6, acks: [{ id: cmd.id, status: 'rejected', message: 'Local selector is in MANUAL' }] }),
    );
    const r = await db.query<{ status: string; status_message: string }>(
      'select status, status_message from public.commands where id = $1',
      [cmd.id],
    );
    expect(r.rows[0]).toEqual({ status: 'rejected', status_message: 'Local selector is in MANUAL' });
  });

  it('expires commands the device never collected', async () => {
    const r1 = await command(USER_A, 'set_relays', { k3: true });
    const id = r1.rows[0].id;
    await db.query(`update public.commands set expires_at = now() - interval '1 second' where id = $1`, [id]);
    await deviceReport(db, report({ seq: 7 }));
    const r = await db.query<{ status: string }>('select status from public.commands where id = $1', [id]);
    expect(r.rows[0].status).toBe('expired');
  });

  it('validates settings and hands the new version to the device', async () => {
    type Args = [number, number, number, number, number, number, number | null];
    const base: Args = [28, 32, 2, 15, 30, 3, null];
    const update = (args: Args, uid = USER_A) =>
      as(db, 'authenticated', uid, () =>
        db.query<{ v: number }>('select public.update_device_settings($1, $2, $3, $4, $5, $6, $7, $8) as v', [
          deviceId,
          ...args,
        ]),
      );
    const withArg = (i: number, v: number | null): Args => base.map((x, j) => (j === i ? v : x)) as Args;
    await expect(update(withArg(0, 9))).rejects.toThrow(/between 10 and 34/);
    await expect(update(withArg(1, 35))).rejects.toThrow(/between 10 and 34/);
    await expect(update(withArg(1, 28))).rejects.toThrow(/higher than salMin/);
    await expect(update(withArg(2, 0.5))).rejects.toThrow(/salHyst/);
    await expect(update(withArg(3, 61))).rejects.toThrow(/maximum run time/);
    await expect(update(withArg(4, 5))).rejects.toThrow(/minimum off time/);
    await expect(update(withArg(5, 0))).rejects.toThrow(/maximum failures/);
    await expect(update(withArg(6, 3))).rejects.toThrow(/Retention/);
    await expect(update(base, USER_B)).rejects.toThrow(/No access/);
    const v = await update([20.04, 30, 2.5, 20, 45, 4, 30]);
    expect(v.rows[0].v).toBe(2);
    const res = await deviceReport(db, report({ seq: 8 }));
    expect(res.settings).toEqual({
      version: 2,
      sal_min_ppt: 20,
      sal_max_ppt: 30,
      sal_hyst_ppt: 2.5,
      bilge_max_run_min: 20,
      bilge_min_off_min: 45,
      bilge_max_fails: 4,
    });
  });

  it('records device events', async () => {
    await deviceReport(
      db,
      report({ seq: 9, events: [{ severity: 'critical', kind: 'do_emergency', message: 'DO below 3.0 mg/L' }] }),
    );
    const r = await as(db, 'authenticated', USER_A, () => db.query('select kind from public.events'));
    expect(r.rows).toEqual([{ kind: 'do_emergency' }]);
  });

  it('lets a granted browser read history for CSV export', async () => {
    const r = await as(db, 'authenticated', USER_A, () =>
      db.query('select recorded_at, ph, salinity_ppt, temp_c, do_mg_l, nh3_fraction from public.readings'),
    );
    expect(r.rows.length).toBeGreaterThan(0);
  });

  it('revokes every browser when the sticker code is rotated', async () => {
    await as(db, 'service_role', null, () =>
      db.query(`select public.admin_rotate_access_code($1, 'NEWC-ODE9')`, [deviceId]),
    );
    const rows = await as(db, 'authenticated', USER_A, () => db.query('select * from public.device_state'));
    expect(rows.rows).toHaveLength(0);
    await expect(command(USER_A, 'set_mode', { mode: 'AUTO' })).rejects.toThrow(/No access/);
    // the new code works
    await as(db, 'authenticated', USER_A, () => db.query(`select public.claim_device_access('newc-ode9')`));
    await command(USER_A, 'set_mode', { mode: 'AUTO' });
  });

  it('keeps history forever by default and prunes only when a retention window is set', async () => {
    const count = async () => Number((await db.query<{ n: string }>('select count(*) as n from public.readings')).rows[0].n);
    // force the next report to write history, and age every existing row
    const age = async () => {
      await db.query(`update public.readings set recorded_at = now() - interval '100 days'`);
      await db.query(`update public.device_state set last_history_at = now() - interval '2 minutes'`);
    };
    await db.query(`update public.device_settings set retention_days = null`);
    await age();
    const before = await count();
    await deviceReport(db, report({ seq: 20 }));
    expect(await count()).toBe(before + 1);
    await db.query(`update public.device_settings set retention_days = 90`);
    await age();
    await deviceReport(db, report({ seq: 21 }));
    expect(await count()).toBe(1);
    const cal = await db.query('select * from public.calibration_records');
    expect(cal.rows).toHaveLength(1);
  });
});
