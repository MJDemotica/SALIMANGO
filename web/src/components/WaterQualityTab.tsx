import type { DeviceData } from '../hooks/useDevice';
import { useI18n, type Translate } from '../lib/i18n';
import {
  doLevel,
  formatNumber,
  freshness,
  hasValue,
  phLevel,
  salinityLevel,
  tempLevel,
  TARGETS,
} from '../lib/status';
import type { CalibrationState, DeviceState } from '../lib/types';
import { HistoryPanel } from './HistoryPanel';
import { ModePanel } from './ModePanel';
import { PumpPanel } from './PumpPanel';
import { SensorCard } from './SensorCard';

export function CalBadge({ cal }: { cal?: CalibrationState }) {
  const { t } = useI18n();
  if (!cal) return null;
  if (cal.session) return <span className="badge warning">{t('calInProgress')}</span>;
  if (cal.stored_invalid) return <span className="badge critical">{t('calInvalid')}</span>;
  if (cal.state === 'calibrated') return <span className="badge good">{t('calCalibrated', { v: cal.version })}</span>;
  return <span className="badge warning">{t('calDefault')}</span>;
}

function SensorCards({ data }: { data: DeviceData }) {
  const { t } = useI18n();
  const s = data.state;
  const stale = freshness(s?.updated_at, data.now).kind !== 'live';
  const missing = data.loading ? null : ('unavailable' as const);

  const ph = s && hasValue(s.ph_status, s.ph) ? s.ph : null;
  const sal = s && hasValue(s.salinity_status, s.salinity_ppt) ? s.salinity_ppt : null;
  const temp = s && hasValue(s.temp_status, s.temp_c) ? s.temp_c : null;
  const dox = s && hasValue(s.do_status, s.do_mg_l) ? s.do_mg_l : null;
  const nh3 = s && hasValue(s.nh3_status, s.nh3_fraction) ? s.nh3_fraction : null;
  const settings = data.settings;

  return (
    <div className="grid">
      <SensorCard
        testId="card-ph"
        title={t('cardPh')}
        value={ph !== null ? formatNumber(ph, 2) : null}
        status={s ? s.ph_status : missing}
        level={ph !== null ? phLevel(ph) : 'unknown'}
        stale={stale}
        badge={<CalBadge cal={s?.calibration.ph} />}
      >
        <span>{t('target', { range: `${TARGETS.ph.min}–${TARGETS.ph.max}` })}</span>
        {s?.ph_status === 'degraded' && <span>{t('tempFallbackNote')}</span>}
      </SensorCard>

      <SensorCard
        testId="card-salinity"
        title={t('cardSalinity')}
        value={sal !== null ? formatNumber(sal, 1) : null}
        unit="ppt"
        status={s ? s.salinity_status : missing}
        level={sal !== null ? salinityLevel(sal, settings) : 'unknown'}
        stale={stale}
        badge={<CalBadge cal={s?.calibration.ec} />}
      >
        {settings && <span>{t('target', { range: `${settings.sal_min_ppt}–${settings.sal_max_ppt} ppt` })}</span>}
        {s && hasValue(s.ec_status, s.ec_ms_cm) && (
          <span>
            {t('salEc', {
              ec: formatNumber(s.ec_ms_cm, 2),
              t: formatNumber(s.temp_c, 1),
              ec25: formatNumber(s.ec25_ms_cm, 2),
            })}
          </span>
        )}
        <SalinityNote t={t} state={s} />
        <span>{t('salMethod')}</span>
      </SensorCard>

      <SensorCard
        testId="card-temp"
        title={t('cardTemp')}
        value={temp !== null ? formatNumber(temp, 1) : null}
        unit="°C"
        status={s ? s.temp_status : missing}
        level={temp !== null ? tempLevel(temp) : 'unknown'}
        stale={stale}
      >
        <span>{t('target', { range: `${TARGETS.tempC.min}–${TARGETS.tempC.max} °C` })}</span>
        {s?.temp2_c !== null && s?.temp2_c !== undefined && <span>{t('tempSecond', { t: formatNumber(s.temp2_c, 1) })}</span>}
        <span>{t('tempUsedFor')}</span>
      </SensorCard>

      <SensorCard
        testId="card-do"
        title={t('cardDo')}
        value={dox !== null ? formatNumber(dox, 2) : null}
        unit="mg/L"
        status={s ? s.do_status : missing}
        level={dox !== null ? doLevel(dox) : 'unknown'}
        stale={stale}
        badge={<CalBadge cal={s?.calibration.do} />}
      >
        {s?.do_emergency && <strong className="error-text">{t('doEmergency')}</strong>}
        <span>{t('target', { range: `≥ ${TARGETS.doMgL.good} mg/L` })}</span>
        {s?.do_status === 'degraded' && <span>{t('tempFallbackNote')}</span>}
      </SensorCard>

      <SensorCard
        testId="card-nh3"
        title={t('cardNh3')}
        value={nh3 !== null ? formatNumber(nh3 * 100, 2) : null}
        unit="%"
        status={s ? s.nh3_status : missing}
        level="neutral"
        stale={stale}
      >
        {s?.nh3_status === 'unavailable' && <span>{t('nh3Unavailable')}</span>}
        {(s?.nh3_status === 'over_range' || s?.nh3_status === 'under_range') && <span>{t('nh3OutOfDomain')}</span>}
        <span>{t('nh3Note')}</span>
      </SensorCard>
    </div>
  );
}

function SalinityNote({ t, state }: { t: Translate; state: DeviceState | null }) {
  if (!state) return null;
  if (state.salinity_status === 'over_range') return <span>{t('salOverRange')}</span>;
  if (state.salinity_status === 'under_range') return <span>{t('salUnderRange')}</span>;
  if (state.salinity_status === 'unavailable') return <span>{t('salUnavailable')}</span>;
  return null;
}

function EventsList({ data }: { data: DeviceData }) {
  const { t, lang } = useI18n();
  const fmt = new Intl.DateTimeFormat(lang === 'fil' ? 'fil-PH' : 'en-PH', { dateStyle: 'medium', timeStyle: 'short' });
  return (
    <section className="panel">
      <h2>{t('eventsTitle')}</h2>
      {data.events.length === 0 ? (
        <p className="muted small">{t('eventsEmpty')}</p>
      ) : (
        <ul className="events">
          {data.events.map((e) => (
            <li key={e.id}>
              <time dateTime={e.created_at}>{fmt.format(new Date(e.created_at))}</time>
              <span className={e.severity}>{e.message}</span>
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}

export function WaterQualityTab({ data, deviceId }: { data: DeviceData; deviceId: string }) {
  return (
    <div className="stack">
      <SensorCards data={data} />
      <div className="two-col" style={{ marginTop: 0 }}>
        <ModePanel data={data} />
        <PumpPanel data={data} />
      </div>
      <HistoryPanel deviceId={deviceId} settings={data.settings} />
      <EventsList data={data} />
    </div>
  );
}
