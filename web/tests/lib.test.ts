import { describe, expect, it } from 'vitest';
import { formatAccessCode, codeFromUrl } from '../src/lib/access';
import { bucketSeries, toCsv } from '../src/lib/history';
import { dictionaries, formatAgo, translate } from '../src/lib/i18n';
import { commandView, doLevel, freshness, hasValue, phLevel, salinityLevel } from '../src/lib/status';
import { validateSettings } from '../src/components/SettingsTab';
import type { Reading } from '../src/lib/types';

const t = (k: Parameters<typeof translate>[1], v?: Record<string, string | number>) => translate('en', k, v);

describe('translations', () => {
  const placeholders = (s: string) => [...s.matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort();
  it('Filipino has every English key with the same placeholders', () => {
    for (const key of Object.keys(dictionaries.en) as (keyof typeof dictionaries.en)[]) {
      expect(dictionaries.fil[key], key).toBeTruthy();
      expect(placeholders(dictionaries.fil[key]), key).toEqual(placeholders(dictionaries.en[key]));
    }
    expect(Object.keys(dictionaries.fil).sort()).toEqual(Object.keys(dictionaries.en).sort());
  });
  it('interpolates variables', () => {
    expect(translate('en', 'calCalibrated', { v: 3 })).toBe('Calibrated (v3)');
    expect(formatAgo(t, 30)).toBe('30 s ago');
    expect(formatAgo(t, 600)).toBe('10 min ago');
  });
});

describe('access code', () => {
  it('formats typed codes for display', () => {
    expect(formatAccessCode('k7qm3xrp')).toBe('K7QM-3XRP');
    expect(formatAccessCode('k7 qm-3x')).toBe('K7QM-3X');
  });
  it('reads the code from the QR URL', () => {
    expect(codeFromUrl('?code=K7QM-3XRP')).toBe('K7QM-3XRP');
    expect(codeFromUrl('?x=1')).toBeNull();
  });
});

describe('status', () => {
  const now = Date.parse('2026-10-26T10:00:00Z');
  it('marks data older than 45 s as stale', () => {
    expect(freshness('2026-10-26T09:59:30Z', now).kind).toBe('live');
    expect(freshness('2026-10-26T09:58:00Z', now).kind).toBe('stale');
    expect(freshness(null, now).kind).toBe('none');
  });
  it('never treats a withheld reading as a value, but keeps a real zero', () => {
    expect(hasValue('fault', null)).toBe(false);
    expect(hasValue('over_range', null)).toBe(false);
    expect(hasValue('ok', 0)).toBe(true);
  });
  it('colours values against the documented targets', () => {
    expect(doLevel(2.9)).toBe('critical');
    expect(doLevel(4)).toBe('warning');
    expect(doLevel(6)).toBe('good');
    expect(phLevel(8)).toBe('good');
    expect(phLevel(9)).toBe('warning');
    expect(salinityLevel(30, { sal_min_ppt: 28, sal_max_ppt: 32 })).toBe('good');
    expect(salinityLevel(20, { sal_min_ppt: 28, sal_max_ppt: 32 })).toBe('warning');
  });
  it('shows an unanswered request as timed out, never as applied', () => {
    expect(commandView({ status: 'delivered', expires_at: '2026-10-26T09:58:00Z' }, now)).toBe('timed_out');
    expect(commandView({ status: 'delivered', expires_at: '2026-10-26T10:01:00Z' }, now)).toBe('delivered');
    expect(commandView({ status: 'accepted', expires_at: '2026-10-26T09:00:00Z' }, now)).toBe('accepted');
  });
});

describe('CSV and charts', () => {
  it('escapes cells and leaves missing values empty (not zero)', () => {
    const csv = toCsv(
      [
        { a: 'x,y', b: null, c: 0 },
        { a: 'say "hi"', b: 7.25, c: true },
      ],
      ['a', 'b', 'c'],
    );
    expect(csv).toBe('a,b,c\r\n"x,y",,0\r\n"say ""hi""",7.25,true\r\n');
  });
  it('averages into buckets and leaves gaps where there is no valid value', () => {
    const r = (iso: string, v: number | null) => ({ recorded_at: iso, do_mg_l: v }) as Reading;
    const from = Date.parse('2026-10-26T00:00:00Z');
    const pts = bucketSeries(
      [r('2026-10-26T00:01:00Z', 5), r('2026-10-26T00:05:00Z', 7), r('2026-10-26T00:25:00Z', null)],
      (x) => x.do_mg_l,
      10 * 60_000,
      from,
      from + 30 * 60_000,
    );
    expect(pts.map((p) => p.v)).toEqual([6, null, null]);
  });
});

describe('settings validation (mirrors the database)', () => {
  const base = {
    sal_min_ppt: '28',
    sal_max_ppt: '32',
    sal_hyst_ppt: '2',
    bilge_max_run_min: '15',
    bilge_min_off_min: '30',
    bilge_max_fails: '3',
    keepAll: true,
    retention_days: '365',
  };
  it('accepts the defaults and keeps history forever', () => {
    const r = validateSettings(base, t);
    expect(r.errors).toEqual([]);
    expect(r.input?.retention_days).toBeNull();
  });
  it('rejects out-of-range values, salMax ≤ salMin and bad retention', () => {
    expect(validateSettings({ ...base, sal_min_ppt: '9' }, t).errors).toHaveLength(1);
    expect(validateSettings({ ...base, sal_max_ppt: '28' }, t).errors).toEqual(['salMax must be higher than salMin.']);
    expect(validateSettings({ ...base, bilge_max_fails: '2.5' }, t).errors).toHaveLength(1);
    expect(validateSettings({ ...base, keepAll: false, retention_days: '3' }, t).errors).toHaveLength(1);
    expect(validateSettings({ ...base, keepAll: false, retention_days: '90' }, t).input?.retention_days).toBe(90);
  });
});
