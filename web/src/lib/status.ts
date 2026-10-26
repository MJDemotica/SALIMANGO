import type { CommandRow, DerivedStatus, DeviceSettings, SensorStatus } from './types';

// The device reports every 10 s (firmware config.h REPORT_INTERVAL_MS).
// Older than this, the dashboard marks every value as stale.
export const LIVE_WINDOW_S = 45;

// Display targets. pH, temperature and DO come from the SEAFDEC/AQD mud-crab
// ranges in docs/Components.md; the DO 3 / 5 mg/L marks match the firmware's
// emergency level and the DO sketch's "watch" level. Salinity uses the device's
// salMin / salMax settings. These only colour the cards; control decisions are
// made by the firmware.
export const TARGETS = {
  ph: { min: 7.5, max: 8.5 },
  tempC: { min: 27, max: 30 },
  doMgL: { critical: 3, good: 5 },
} as const;

export type Freshness = 'none' | 'live' | 'stale';
export type Level = 'good' | 'warning' | 'critical' | 'neutral' | 'unknown';

export function freshness(updatedAt: string | null | undefined, nowMs: number): { kind: Freshness; ageS: number } {
  if (!updatedAt) return { kind: 'none', ageS: Infinity };
  const ageS = Math.max(0, (nowMs - Date.parse(updatedAt)) / 1000);
  return { kind: ageS <= LIVE_WINDOW_S ? 'live' : 'stale', ageS };
}

export function hasValue(status: SensorStatus | DerivedStatus, value: number | null): value is number {
  return value !== null && (status === 'ok' || status === 'degraded');
}

export function inBand(v: number, min: number, max: number): Level {
  return v >= min && v <= max ? 'good' : 'warning';
}

export function phLevel(v: number): Level {
  return inBand(v, TARGETS.ph.min, TARGETS.ph.max);
}

export function tempLevel(v: number): Level {
  return inBand(v, TARGETS.tempC.min, TARGETS.tempC.max);
}

export function doLevel(v: number): Level {
  if (v < TARGETS.doMgL.critical) return 'critical';
  if (v < TARGETS.doMgL.good) return 'warning';
  return 'good';
}

export function salinityLevel(v: number, settings: Pick<DeviceSettings, 'sal_min_ppt' | 'sal_max_ppt'> | null): Level {
  if (!settings) return 'neutral';
  return inBand(v, Number(settings.sal_min_ppt), Number(settings.sal_max_ppt));
}

export function formatNumber(v: number | null | undefined, decimals: number): string {
  return v === null || v === undefined || Number.isNaN(v) ? '—' : v.toFixed(decimals);
}

export type CommandView = CommandRow['status'] | 'timed_out';

/** A request still waiting after its expiry is shown as timed out, never as applied. */
export function commandView(cmd: Pick<CommandRow, 'status' | 'expires_at'>, nowMs: number): CommandView {
  if ((cmd.status === 'pending' || cmd.status === 'delivered') && Date.parse(cmd.expires_at) + 30_000 < nowMs) {
    return 'timed_out';
  }
  return cmd.status;
}

export function isWaiting(view: CommandView): boolean {
  return view === 'pending' || view === 'delivered';
}
