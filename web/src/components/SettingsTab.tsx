import { useEffect, useState } from 'react';
import type { DeviceData, SettingsInput } from '../hooks/useDevice';
import { downloadText, EVENT_COLUMNS, fetchAllRows, READING_COLUMNS, safeFileName, toCsv } from '../lib/history';
import { formatDuration, useI18n, type MessageKey, type Translate } from '../lib/i18n';
import { errorMessage, supabase } from '../lib/supabase';
import type { DeviceSettings } from '../lib/types';

// Same limits as public.update_device_settings (supabase/migrations).
export const SETTING_LIMITS = {
  sal_min_ppt: { min: 10, max: 34, label: 'setSalMin', step: 0.1 },
  sal_max_ppt: { min: 10, max: 34, label: 'setSalMax', step: 0.1 },
  sal_hyst_ppt: { min: 1, max: 5, label: 'setSalHyst', step: 0.1 },
  bilge_max_run_min: { min: 5, max: 60, label: 'setMaxRun', step: 1 },
  bilge_min_off_min: { min: 10, max: 120, label: 'setMinOff', step: 1 },
  bilge_max_fails: { min: 1, max: 10, label: 'setMaxFails', step: 1 },
} as const;
type NumericField = keyof typeof SETTING_LIMITS;
const RETENTION = { min: 7, max: 3650 };

type Form = Record<NumericField, string> & { keepAll: boolean; retention_days: string };

function toForm(s: DeviceSettings): Form {
  return {
    sal_min_ppt: String(s.sal_min_ppt),
    sal_max_ppt: String(s.sal_max_ppt),
    sal_hyst_ppt: String(s.sal_hyst_ppt),
    bilge_max_run_min: String(s.bilge_max_run_min),
    bilge_min_off_min: String(s.bilge_min_off_min),
    bilge_max_fails: String(s.bilge_max_fails),
    keepAll: s.retention_days === null,
    retention_days: String(s.retention_days ?? 365),
  };
}

/** Returns the validated input, or the list of error messages. */
export function validateSettings(f: Form, t: Translate): { input?: SettingsInput; errors: string[] } {
  const errors: string[] = [];
  const out: Partial<SettingsInput> = {};
  for (const [field, lim] of Object.entries(SETTING_LIMITS) as [NumericField, (typeof SETTING_LIMITS)[NumericField]][]) {
    const v = Number(f[field]);
    const integer = lim.step === 1;
    if (f[field].trim() === '' || !Number.isFinite(v) || v < lim.min || v > lim.max || (integer && !Number.isInteger(v))) {
      errors.push(t('setErrRange', { field: t(lim.label as MessageKey), min: lim.min, max: lim.max }));
    } else {
      out[field] = v;
    }
  }
  if (out.sal_min_ppt !== undefined && out.sal_max_ppt !== undefined && out.sal_max_ppt <= out.sal_min_ppt) {
    errors.push(t('setErrOrder'));
  }
  let retention: number | null = null;
  if (!f.keepAll) {
    const d = Number(f.retention_days);
    if (!Number.isInteger(d) || d < RETENTION.min || d > RETENTION.max) {
      errors.push(t('setErrRange', { field: t('retentionDelete'), min: RETENTION.min, max: RETENTION.max }));
    } else {
      retention = d;
    }
  }
  if (errors.length) return { errors };
  return { input: { ...(out as Omit<SettingsInput, 'retention_days'>), retention_days: retention }, errors };
}

function SettingsForm({ data }: { data: DeviceData }) {
  const { t } = useI18n();
  const [form, setForm] = useState<Form | null>(data.settings ? toForm(data.settings) : null);
  const [errors, setErrors] = useState<string[]>([]);
  const [saving, setSaving] = useState(false);
  const [savedVersion, setSavedVersion] = useState<number | null>(null);
  const [dirty, setDirty] = useState(false);

  useEffect(() => {
    if (data.settings && !dirty) setForm(toForm(data.settings));
  }, [data.settings, dirty]);

  if (!form || !data.settings) return <p className="muted">{t('loading')}</p>;
  const set = (patch: Partial<Form>) => {
    setForm({ ...form, ...patch });
    setDirty(true);
    setSavedVersion(null);
  };

  async function save() {
    const { input, errors: errs } = validateSettings(form!, t);
    setErrors(errs);
    if (!input) return;
    setSaving(true);
    try {
      setSavedVersion(await data.updateSettings(input));
      setDirty(false);
    } catch (e) {
      setErrors([errorMessage(e)]);
    } finally {
      setSaving(false);
    }
  }

  const applied = data.state?.settings_version ?? null;
  const requested = data.settings.version;

  return (
    <section className="panel stack">
      <h2>{t('settingsTitle')}</h2>
      <p className="small muted">{t('settingsIntro')}</p>
      <div className="form-grid">
        {(Object.keys(SETTING_LIMITS) as NumericField[]).map((f) => (
          <div className="field" key={f}>
            <label htmlFor={`set-${f}`}>{t(SETTING_LIMITS[f].label as MessageKey)}</label>
            <input
              id={`set-${f}`}
              type="number"
              inputMode="decimal"
              min={SETTING_LIMITS[f].min}
              max={SETTING_LIMITS[f].max}
              step={SETTING_LIMITS[f].step}
              value={form[f]}
              onChange={(e) => set({ [f]: e.target.value } as Partial<Form>)}
            />
          </div>
        ))}
      </div>
      <p className="small muted">{t('setRangeNote')}</p>

      <fieldset className="stack" style={{ border: 0, padding: 0, margin: 0 }}>
        <legend><h3>{t('retentionTitle')}</h3></legend>
        <label className="row">
          <input type="radio" name="retention" checked={form.keepAll} onChange={() => set({ keepAll: true })} />
          {t('retentionKeep')}
        </label>
        <label className="row">
          <input type="radio" name="retention" checked={!form.keepAll} onChange={() => set({ keepAll: false })} />
          {t('retentionDelete')}
          <input
            aria-label={t('retentionDays')}
            type="number"
            min={RETENTION.min}
            max={RETENTION.max}
            step={1}
            style={{ width: 100 }}
            className="field"
            disabled={form.keepAll}
            value={form.retention_days}
            onChange={(e) => set({ retention_days: e.target.value })}
          />
          {t('retentionDays')}
        </label>
        {!form.keepAll && <p className="small error-text">{t('retentionWarn')}</p>}
      </fieldset>

      {errors.map((e) => (
        <p key={e} className="error-text" role="alert">
          {e}
        </p>
      ))}
      <div className="row">
        <button className="btn primary" disabled={saving || !dirty} onClick={() => void save()}>
          {saving ? t('setSaving') : t('setSave')}
        </button>
      </div>
      {savedVersion !== null && <p className="ok-text">{t('setSaved', { v: savedVersion })}</p>}
      {applied !== null &&
        (applied === requested ? (
          <p className="small muted">{t('setApplied', { v: applied })}</p>
        ) : (
          <p className="small">{t('setPendingApply', { a: applied, b: requested })}</p>
        ))}
    </section>
  );
}

function ExportPanel({ data, deviceId }: { data: DeviceData; deviceId: string }) {
  const { t } = useI18n();
  const [msg, setMsg] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function exportTable(table: 'readings' | 'events') {
    setBusy(true);
    setMsg(null);
    try {
      const orderColumn = table === 'readings' ? 'recorded_at' : 'created_at';
      const rows = await fetchAllRows<Record<string, unknown>>(supabase, table, deviceId, orderColumn, {
        onProgress: (n) => setMsg(t('exportProgress', { n })),
      });
      if (!rows.length) {
        setMsg(t('exportEmpty'));
        return;
      }
      const columns = table === 'readings' ? (READING_COLUMNS as string[]) : EVENT_COLUMNS;
      const stamp = new Date().toISOString().slice(0, 10);
      downloadText(`${safeFileName(data.name)}-${table}-${stamp}.csv`, toCsv(rows, columns));
      setMsg(t('exportDone', { n: rows.length }));
    } catch (e) {
      setMsg(t('errorGeneric', { msg: errorMessage(e) }));
    } finally {
      setBusy(false);
    }
  }

  return (
    <section className="panel stack">
      <h2>{t('exportTitle')}</h2>
      <div className="row">
        <button className="btn primary" disabled={busy} onClick={() => void exportTable('readings')}>
          {t('exportReadings')}
        </button>
        <button className="btn" disabled={busy} onClick={() => void exportTable('events')}>
          {t('exportEvents')}
        </button>
      </div>
      {msg && <p className="small" role="status">{msg}</p>}
    </section>
  );
}

function DeviceInfo({ data }: { data: DeviceData }) {
  const { t } = useI18n();
  const s = data.state;
  return (
    <section className="panel stack">
      <h2>{t('deviceTitle')}</h2>
      <p className="small">{data.name}</p>
      {s && (
        <ul className="small muted" style={{ margin: 0, paddingLeft: 18 }}>
          {s.firmware_version && <li>{t('deviceFirmware', { v: s.firmware_version })}</li>}
          {s.wifi_rssi_dbm !== null && <li>{t('deviceWifi', { rssi: s.wifi_rssi_dbm })}</li>}
          <li>{t('deviceUptime', { time: formatDuration(s.uptime_s) })}</li>
        </ul>
      )}
    </section>
  );
}

export function SettingsTab({ data, deviceId }: { data: DeviceData; deviceId: string }) {
  return (
    <div className="two-col" style={{ marginTop: 0 }}>
      <SettingsForm data={data} />
      <div className="stack">
        <ExportPanel data={data} deviceId={deviceId} />
        <DeviceInfo data={data} />
      </div>
    </div>
  );
}
