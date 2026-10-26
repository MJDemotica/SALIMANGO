#!/usr/bin/env node
// Development simulator: reports SIMULATED data through the same device RPC the
// ESP32 uses, so the dashboard can be tried before the hardware is online.
// Every report carries "simulated": true; the dashboard shows a red
// "SIMULATED DATA" banner and the CSV has simulated = true on every row.
// The numbers are a random walk, not physics; never present them as pond data.
//
//   SUPABASE_URL=... SUPABASE_ANON_KEY=... DEVICE_TOKEN=... npm run simulate
import { createClient } from '@supabase/supabase-js';

const { SUPABASE_URL, SUPABASE_ANON_KEY, DEVICE_TOKEN } = process.env;
if (!SUPABASE_URL || !SUPABASE_ANON_KEY || !DEVICE_TOKEN) {
  console.error('Set SUPABASE_URL, SUPABASE_ANON_KEY and DEVICE_TOKEN (from npm run provision).');
  process.exit(1);
}
const supabase = createClient(SUPABASE_URL, SUPABASE_ANON_KEY, { auth: { persistSession: false } });

const walk = (v, step, lo, hi) => Math.min(hi, Math.max(lo, v + (Math.random() - 0.5) * step));
const round = (v, d) => Math.round(v * 10 ** d) / 10 ** d;

let seq = 0;
let mode = 'AUTO';
let settingsVersion = 0;
const relays = { k1: true, k3: false, k4: false, k5: true };
const v = { ph: 7.9, ec: 12, temp: 29, dO: 6, sal: 7, nh3: 0.06 };
let acks = [];
const start = Date.now();

function handle(cmd) {
  const p = cmd.payload ?? {};
  if (cmd.kind === 'set_mode') {
    mode = p.mode;
    return { status: 'accepted', message: `Mode set to ${mode} (simulator)` };
  }
  if (cmd.kind === 'set_relays') {
    if (mode !== 'MANUAL') return { status: 'rejected', message: `Pumps can only be switched in MANUAL (current mode: ${mode})` };
    Object.assign(relays, p);
    return { status: 'accepted', message: 'Pump states applied (simulator)' };
  }
  return { status: 'rejected', message: 'Not supported by the simulator' };
}

async function tick() {
  v.ph = walk(v.ph, 0.04, 7.2, 8.6);
  v.ec = walk(v.ec, 0.2, 8, 16);
  v.temp = walk(v.temp, 0.1, 26, 32);
  v.dO = walk(v.dO, 0.15, 2.5, 8);
  v.sal = walk(v.sal, 0.1, 4, 10);
  v.nh3 = walk(v.nh3, 0.004, 0.01, 0.12);
  if (mode === 'AUTO') relays.k1 = relays.k5 = v.dO < 5;
  if (mode === 'OFF') Object.assign(relays, { k1: false, k3: false, k4: false, k5: false });

  const report = {
    seq: ++seq,
    uptime_s: Math.round((Date.now() - start) / 1000),
    fw: 'simulator',
    simulated: true,
    rssi: -60,
    settings_version: settingsVersion,
    mode,
    mode_source: mode === 'AUTO' ? 'selector' : 'remote',
    selector: 'AUTO',
    manual_expires_in_s: mode === 'MANUAL' ? 300 : null,
    do_emergency: false,
    relays,
    bilge: { state: 'IDLE', fail_count: 0 },
    ph: { value: round(v.ph, 2), status: 'ok', mv: 1550 },
    ec: { ec_ms_cm: round(v.ec, 3), ec25_ms_cm: round(v.ec * 0.92, 3), status: 'ok', mv: 2000 },
    salinity: { value: round(v.sal, 2), status: 'ok' },
    temp: { value: round(v.temp, 2), status: 'ok', t2: null },
    do: { value: round(v.dO, 2), status: 'ok', mv: 1100 },
    nh3: { fraction: round(v.nh3, 5), status: 'ok' },
    cal: { ph: { state: 'default', version: 0 }, ec: { state: 'default', version: 0 }, do: { state: 'default', version: 0 } },
    faults: [],
    acks,
    events: seq === 1 ? [{ severity: 'info', kind: 'boot', message: 'Simulator started (SIMULATED DATA)' }] : [],
  };
  const { data, error } = await supabase.rpc('device_report', { p_token: DEVICE_TOKEN, p_report: report });
  if (error) {
    console.error('report failed:', error.message);
    return;
  }
  acks = (data.commands ?? []).map((c) => ({ id: c.id, ...handle(c) }));
  if (data.settings) settingsVersion = data.settings.version;
  console.log(`#${seq} mode ${mode} DO ${round(v.dO, 2)} mg/L, ${acks.length} command(s)`);
}

console.log('Simulating a device (SIMULATED DATA). Ctrl+C to stop.');
await tick();
setInterval(() => void tick(), 5000);
