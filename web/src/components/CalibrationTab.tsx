import { useState, type ReactNode } from 'react';
import type { DeviceData } from '../hooks/useDevice';
import { useI18n, type MessageKey, type Translate } from '../lib/i18n';
import { formatNumber, freshness } from '../lib/status';
import { errorMessage } from '../lib/supabase';
import type { CalibrationRecord, CalibrationState, CalSensor } from '../lib/types';
import { CommandStatus } from './CommandStatus';
import { CalBadge } from './WaterQualityTab';

type Params = Record<string, number | number[]>;
const num = (p: Params | undefined, k: string): number | null => (typeof p?.[k] === 'number' ? (p[k] as number) : null);

function PhParams({ p, t }: { p?: Params; t: Translate }) {
  return (
    <dl className="kv">
      <dt>{t('phPh7')}</dt>
      <dd>{formatNumber(num(p, 'ph7_v'), 4)} V</dd>
      <dt>{t('phSlope')}</dt>
      <dd>{formatNumber(num(p, 'slope_v_per_ph'), 4)} V/pH</dd>
      <dt>{t('phCalTemp')}</dt>
      <dd>{formatNumber(num(p, 'cal_temp_c'), 2)} °C</dd>
      {num(p, 'points') !== null && (
        <>
          <dt>{t('phPoints')}</dt>
          <dd>{num(p, 'points')}</dd>
        </>
      )}
      <dt>{t('phDivider')}</dt>
      <dd>{formatNumber(num(p, 'divider_ratio'), 4)}</dd>
    </dl>
  );
}

function EcParams({ p, t }: { p?: Params; t: Translate }) {
  const stds = Array.isArray(p?.standards_ms_cm) ? (p.standards_ms_cm as number[]) : [];
  return (
    <dl className="kv">
      <dt>{t('ecK')}</dt>
      <dd data-testid="ec-k">{formatNumber(num(p, 'cell_constant_k'), 1)}</dd>
      <dt>{t('ecCorrLow')}</dt>
      <dd>{formatNumber(num(p, 'corr_low'), 4)}</dd>
      <dt>{t('ecCorrHigh')}</dt>
      <dd>{formatNumber(num(p, 'corr_high'), 4)}</dd>
      {stds.length > 0 && (
        <>
          <dt>{t('ecStandards')}</dt>
          <dd>{stds.map((s) => `${s} mS/cm`).join(', ')}</dd>
        </>
      )}
    </dl>
  );
}

function DoParams({ p, t }: { p?: Params; t: Translate }) {
  const pts = [
    [num(p, 'v1_mv'), num(p, 't1_c')],
    [num(p, 'v2_mv'), num(p, 't2_c')],
  ].filter(([v]) => v !== null) as [number, number][];
  return (
    <dl className="kv">
      <dt>{t('doPoints')}</dt>
      <dd>
        {pts.map(([v, c]) => (
          <div key={`${v}-${c}`}>{t('doPoint', { mv: formatNumber(v, 0), t: formatNumber(c, 2) })}</div>
        ))}
      </dd>
    </dl>
  );
}

function Session({ sensor, session, t }: { sensor: CalSensor; session: Record<string, unknown>; t: Translate }) {
  const stable = session.stable as boolean | undefined;
  const points = (session.points as Record<string, number>[] | undefined) ?? [];
  return (
    <div className="banner info stack">
      {stable !== undefined && <strong>{stable ? t('calStable') : t('calSettling')}</strong>}
      {sensor === 'ph' && <span>{formatNumber(session.module_v as number, 3)} V</span>}
      {sensor !== 'ph' && <span>{formatNumber(session.mv as number, 0)} mV</span>}
      {sensor === 'ec' && (
        <span>
          {t('ecPending')}: {formatNumber(session.pending_low as number | null, 4)} / {formatNumber(session.pending_high as number | null, 4)}
        </span>
      )}
      {points.length > 0 && (
        <>
          <span>{t('calCaptured')}:</span>
          <ul className="cal-points">
            {points.map((p, i) =>
              sensor === 'ph' ? (
                <li key={i}>
                  pH {p.buffer} → {formatNumber(p.buffer_ph, 2)} @ {formatNumber(p.temp_c, 1)} °C: {formatNumber(p.module_v, 3)} V
                </li>
              ) : (
                <li key={i}>{t('doPoint', { mv: formatNumber(p.mv, 0), t: formatNumber(p.temp_c, 2) })}</li>
              ),
            )}
          </ul>
        </>
      )}
    </div>
  );
}

interface SectionProps {
  data: DeviceData;
  sensor: CalSensor;
  title: MessageKey;
  steps: MessageKey;
  notes: MessageKey[];
  params: (p: Params | undefined, t: Translate) => ReactNode;
}

function CalSection({ data, sensor, title, steps, notes, params }: SectionProps) {
  const { t, lang } = useI18n();
  const [sendError, setSendError] = useState<string | null>(null);
  const cal: CalibrationState | undefined = data.state?.calibration[sensor];
  const live = freshness(data.state?.updated_at, data.now).kind === 'live';
  const session = cal?.session ?? null;
  const last = data.commands.find((c) => c.kind === 'calibration' && c.payload.sensor === sensor);
  const records: CalibrationRecord[] = data.calibrations.filter((r) => r.sensor === sensor);
  const current = cal?.cal_id !== undefined ? records.find((r) => r.cal_id === cal.cal_id) : undefined;
  const fmt = new Intl.DateTimeFormat(lang === 'fil' ? 'fil-PH' : 'en-PH', { dateStyle: 'medium', timeStyle: 'short' });

  async function act(action: 'enter' | 'capture' | 'save' | 'abort') {
    setSendError(null);
    try {
      await data.sendCommand('calibration', { sensor, action });
    } catch (e) {
      setSendError(errorMessage(e));
    }
  }

  return (
    <section className="panel stack" data-testid={`cal-${sensor}`}>
      <div className="row">
        <h2>{t(title)}</h2>
        <span className="spacer" />
        <CalBadge cal={cal} />
      </div>
      <p className="small">{t(steps)}</p>
      {notes.map((n) => (
        <p key={n} className="small muted">
          {t(n)}
        </p>
      ))}

      <div>
        <h3>{t('calStatus')}</h3>
        <p className="small">
          {cal?.state === 'calibrated' ? t('calCalibratedVersion', { v: cal.version }) : t('calNotCalibrated')}
          {cal?.state === 'calibrated' &&
            ` · ${current ? t('calRecordedAt', { date: fmt.format(new Date(current.received_at)) }) : t('calNoRecord')}`}
        </p>
      </div>

      <div>
        <h3>{t('calActiveValues')}</h3>
        {params(cal?.params, t)}
      </div>

      {session && <Session sensor={sensor} session={session} t={t} />}

      <div className="row">
        {!session ? (
          <button className="btn primary" disabled={!live} onClick={() => void act('enter')}>
            {t('calStart')}
          </button>
        ) : (
          <>
            <button className="btn primary" disabled={!live} onClick={() => void act('capture')}>
              {t('calCapture')}
            </button>
            <button className="btn" disabled={!live} onClick={() => void act('save')}>
              {t('calSave')}
            </button>
            <button className="btn danger" disabled={!live} onClick={() => void act('abort')}>
              {t('calCancel')}
            </button>
          </>
        )}
      </div>
      {!live && <p className="small muted">{t('calNeedsLive')}</p>}
      <CommandStatus command={last} now={data.now} sendError={sendError} />

      <details>
        <summary className="small">{t('calHistory')}</summary>
        {records.length === 0 ? (
          <p className="small muted">{t('calNoHistory')}</p>
        ) : (
          <ul className="events">
            {records.map((r) => (
              <li key={r.id}>
                <time dateTime={r.received_at}>
                  v{r.version} · {fmt.format(new Date(r.received_at))}
                  {r.simulated ? ' · SIMULATED' : ''}
                </time>
                <span className="small">{params(r.parameters, t)}</span>
              </li>
            ))}
          </ul>
        )}
      </details>
    </section>
  );
}

export function CalibrationTab({ data }: { data: DeviceData }) {
  const { t } = useI18n();
  return (
    <div className="stack">
      <p className="small muted">{t('calIntro')}</p>
      <div className="two-col" style={{ marginTop: 0 }}>
        <CalSection
          data={data}
          sensor="ph"
          title="cardPh"
          steps="phSteps"
          notes={[]}
          params={(p, tr) => <PhParams p={p} t={tr} />}
        />
        <CalSection
          data={data}
          sensor="ec"
          title="calEcTitle"
          steps="ecSteps"
          notes={['ecTempComp', 'ecRangeNote']}
          params={(p, tr) => <EcParams p={p} t={tr} />}
        />
        <CalSection
          data={data}
          sensor="do"
          title="cardDo"
          steps="doSteps"
          notes={['doDo9100']}
          params={(p, tr) => <DoParams p={p} t={tr} />}
        />
        <section className="panel stack" data-testid="cal-nh3">
          <h2>{t('nh3CalTitle')}</h2>
          <p className="small">{t('nh3CalBody')}</p>
          <p className="formula">{t('nh3CalFormula')}</p>
          <p className="small muted">{t('nh3CalSource')}</p>
          <p className="small muted">{t('nh3CalLimits')}</p>
        </section>
      </div>
      <p className="small muted">{t('calTempNote')}</p>
    </div>
  );
}
