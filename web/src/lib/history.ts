import type { SupabaseClient } from '@supabase/supabase-js';
import type { Reading } from './types';

// Supabase returns at most 1000 rows per request; larger exports are paged.
const PAGE = 1000;

export async function fetchAllRows<T>(
  client: SupabaseClient,
  table: 'readings' | 'events',
  deviceId: string,
  orderColumn: string,
  options: { since?: string; onProgress?: (n: number) => void } = {},
): Promise<T[]> {
  const rows: T[] = [];
  for (let from = 0; ; from += PAGE) {
    let q = client.from(table).select('*').eq('device_id', deviceId);
    if (options.since) q = q.gte(orderColumn, options.since);
    const { data, error } = await q
      .order(orderColumn, { ascending: true })
      .order('id', { ascending: true })
      .range(from, from + PAGE - 1);
    if (error) throw new Error(error.message);
    rows.push(...((data ?? []) as T[]));
    options.onProgress?.(rows.length);
    if (!data || data.length < PAGE) return rows;
  }
}

function csvCell(v: unknown): string {
  if (v === null || v === undefined) return '';
  const s = typeof v === 'object' ? JSON.stringify(v) : String(v);
  return /[",\r\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
}

/** RFC 4180 CSV; empty cells mean "no value" (never zero). */
export function toCsv(rows: Record<string, unknown>[], columns: string[]): string {
  const lines = [columns.join(',')];
  for (const r of rows) lines.push(columns.map((c) => csvCell(r[c])).join(','));
  return lines.join('\r\n') + '\r\n';
}

export const READING_COLUMNS: (keyof Reading)[] = [
  'recorded_at',
  'seq',
  'simulated',
  'mode',
  'ph',
  'ph_status',
  'ec_ms_cm',
  'ec25_ms_cm',
  'ec_status',
  'salinity_ppt',
  'salinity_status',
  'temp_c',
  'temp_status',
  'do_mg_l',
  'do_status',
  'nh3_fraction',
  'nh3_status',
  'do_emergency',
  'relay_k1',
  'relay_k5',
  'relay_k3',
  'relay_k4',
  'bilge_state',
  'ph_cal_id',
  'ec_cal_id',
  'do_cal_id',
];

export const EVENT_COLUMNS = ['created_at', 'severity', 'kind', 'message'];

export function downloadText(filename: string, text: string): void {
  // BOM so Excel opens the UTF-8 file (°C, ₃) correctly
  const blob = new Blob(['﻿', text], { type: 'text/csv;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function safeFileName(name: string): string {
  return name.replace(/[^A-Za-z0-9_-]+/g, '-').replace(/^-|-$/g, '').toLowerCase() || 'device';
}

export interface SeriesPoint {
  t: number;
  v: number | null;
}

/**
 * Averages valid values into fixed time buckets for charting. A bucket with no
 * valid value stays null, so the chart shows a gap instead of a false line.
 */
export function bucketSeries(
  rows: Reading[],
  pick: (r: Reading) => number | null,
  bucketMs: number,
  fromMs: number,
  toMs: number,
): SeriesPoint[] {
  const n = Math.max(1, Math.ceil((toMs - fromMs) / bucketMs));
  const sum = new Array<number>(n).fill(0);
  const count = new Array<number>(n).fill(0);
  for (const r of rows) {
    const v = pick(r);
    if (v === null || Number.isNaN(v)) continue;
    const i = Math.floor((Date.parse(r.recorded_at) - fromMs) / bucketMs);
    if (i < 0 || i >= n) continue;
    sum[i] += v;
    count[i]++;
  }
  return sum.map((s, i) => ({ t: fromMs + (i + 0.5) * bucketMs, v: count[i] ? s / count[i] : null }));
}
