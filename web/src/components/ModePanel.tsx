import { useState } from 'react';
import type { DeviceData } from '../hooks/useDevice';
import { formatDuration, useI18n, type MessageKey } from '../lib/i18n';
import { commandView, freshness, isWaiting } from '../lib/status';
import { errorMessage } from '../lib/supabase';
import type { OperatingMode } from '../lib/types';
import { CommandStatus } from './CommandStatus';
import { ConfirmDialog } from './ConfirmDialog';

const MODES: OperatingMode[] = ['AUTO', 'MANUAL', 'OFF'];
const HELP: Record<OperatingMode, MessageKey> = { AUTO: 'modeAutoHelp', MANUAL: 'modeManualHelp', OFF: 'modeOffHelp' };

export function ModePanel({ data }: { data: DeviceData }) {
  const { t } = useI18n();
  const [confirmOff, setConfirmOff] = useState(false);
  const [sendError, setSendError] = useState<string | null>(null);
  const s = data.state;
  const stale = freshness(s?.updated_at, data.now).kind !== 'live';
  const last = data.commands.find((c) => c.kind === 'set_mode');
  const lastView = last ? commandView(last, data.now) : null;
  const requested = last && lastView && (isWaiting(lastView) || lastView === 'accepted') ? (last.payload.mode as OperatingMode) : null;
  const selector = s?.selector_position;
  const selectorLocks = selector === 'OFF' || selector === 'MANUAL';

  async function send(mode: OperatingMode) {
    setSendError(null);
    try {
      await data.sendCommand('set_mode', mode === 'OFF' ? { mode, confirm: true } : { mode });
    } catch (e) {
      setSendError(errorMessage(e));
    }
  }

  return (
    <section className="panel stack">
      <h2>{t('modeTitle')}</h2>
      <div className="seg" role="group" aria-label={t('modeTitle')}>
        {MODES.map((m) => {
          const disabled = !s || (selectorLocks && !(selector === 'MANUAL' && m === 'MANUAL'));
          const classes = [s?.mode === m && !stale ? 'active' : '', requested === m && s?.mode !== m ? 'requested' : '']
            .join(' ')
            .trim();
          return (
            <button
              key={m}
              className={classes}
              aria-pressed={s?.mode === m}
              disabled={disabled}
              onClick={() => (m === 'OFF' ? setConfirmOff(true) : void send(m))}
            >
              {t(`mode${m}` as MessageKey)}
            </button>
          );
        })}
      </div>

      <dl className="kv">
        <dt>{t('modeConfirmed')}</dt>
        <dd>
          {s ? `${s.mode}${stale ? ` (${t('modeLastKnown')})` : ''}` : '—'}
        </dd>
        {requested && requested !== s?.mode && (
          <>
            <dt>{t('modeRequested')}</dt>
            <dd>{requested}</dd>
          </>
        )}
      </dl>
      <CommandStatus command={last} now={data.now} sendError={sendError} />

      {s && <p className="small muted">{t(HELP[s.mode])}</p>}
      {s && s.manual_expires_in_s !== null && s.mode === 'MANUAL' && !stale && (
        <p className="small">{t('modeManualExpires', { time: formatDuration(s.manual_expires_in_s) })}</p>
      )}
      {selector && <p className="small muted">{t('modeSelector', { pos: selector })}</p>}
      {selectorLocks && <p className="banner warn">{t('modeSelectorOverrides', { pos: selector! })}</p>}
      {selector === 'FAULT' && <p className="banner warn">{t('modeSelectorFault')}</p>}

      {confirmOff && (
        <ConfirmDialog
          title={t('confirmOffTitle')}
          body={t('confirmOffBody')}
          confirmLabel={t('confirmOffYes')}
          onCancel={() => setConfirmOff(false)}
          onConfirm={() => {
            setConfirmOff(false);
            void send('OFF');
          }}
        />
      )}
    </section>
  );
}
