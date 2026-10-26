import { useEffect, useMemo, useState } from 'react';
import { bucketSeries, fetchAllRows } from '../lib/history';
import { useI18n } from '../lib/i18n';
import { TARGETS } from '../lib/status';
import { errorMessage, supabase } from '../lib/supabase';
import type { DeviceSettings, Reading } from '../lib/types';
import { LineChart } from './LineChart';

const RANGES = { '24h': { ms: 86_400_000, bucket: 10 * 60_000 }, '7d': { ms: 7 * 86_400_000, bucket: 60 * 60_000 } } as const;
type Range = keyof typeof RANGES;

export function HistoryPanel({ deviceId, settings }: { deviceId: string; settings: DeviceSettings | null }) {
  const { t, lang } = useI18n();
  const [range, setRange] = useState<Range>('24h');
  const [rows, setRows] = useState<Reading[]>([]);
  const [span, setSpan] = useState(() => ({ from: Date.now() - RANGES['24h'].ms, to: Date.now() }));
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let cancelled = false;
    async function load() {
      const to = Date.now();
      const from = to - RANGES[range].ms;
      setLoading(true);
      try {
        const data = await fetchAllRows<Reading>(supabase, 'readings', deviceId, 'recorded_at', {
          since: new Date(from).toISOString(),
        });
        if (!cancelled) {
          setRows(data);
          setSpan({ from, to });
          setError(null);
        }
      } catch (e) {
        if (!cancelled) setError(errorMessage(e));
      } finally {
        if (!cancelled) setLoading(false);
      }
    }
    void load();
    const id = setInterval(() => void load(), 5 * 60_000);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, [deviceId, range]);

  const bucket = RANGES[range].bucket;
  const series = useMemo(() => {
    const make = (pick: (r: Reading) => number | null) => bucketSeries(rows, pick, bucket, span.from, span.to);
    return {
      do: make((r) => r.do_mg_l),
      sal: make((r) => r.salinity_ppt),
      temp: make((r) => r.temp_c),
      ph: make((r) => r.ph),
      nh3: make((r) => (r.nh3_fraction === null ? null : r.nh3_fraction * 100)),
    };
  }, [rows, bucket, span]);

  const locale = lang === 'fil' ? 'fil-PH' : 'en-PH';
  const common = { fromMs: span.from, toMs: span.to, locale, emptyText: loading ? t('loading') : t('historyEmpty') };

  return (
    <section className="stack">
      <div className="row">
        <h2>{t('historyTitle')}</h2>
        <span className="spacer" />
        <div className="seg" style={{ gridTemplateColumns: '1fr 1fr', minWidth: 200 }} role="group" aria-label={t('historyTitle')}>
          {(Object.keys(RANGES) as Range[]).map((r) => (
            <button key={r} className={range === r ? 'active' : ''} aria-pressed={range === r} onClick={() => setRange(r)}>
              {r === '24h' ? t('range24h') : t('range7d')}
            </button>
          ))}
        </div>
      </div>
      {error && <p className="error-text">{t('errorGeneric', { msg: error })}</p>}
      <div className="charts">
        <LineChart title={t('cardDo')} unit="mg/L" decimals={2} points={series.do} band={{ min: TARGETS.doMgL.good, max: 1e9 }} {...common} />
        <LineChart
          title={t('cardSalinity')}
          unit="ppt"
          decimals={1}
          points={series.sal}
          band={settings ? { min: Number(settings.sal_min_ppt), max: Number(settings.sal_max_ppt) } : undefined}
          {...common}
        />
        <LineChart title={t('cardTemp')} unit="°C" decimals={1} points={series.temp} band={TARGETS.tempC} {...common} />
        <LineChart title={t('cardPh')} unit="pH" decimals={2} points={series.ph} band={TARGETS.ph} {...common} />
        <LineChart title={t('cardNh3')} unit={t('historyNh3Unit')} decimals={2} points={series.nh3} {...common} />
      </div>
    </section>
  );
}
