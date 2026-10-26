import { useState } from 'react';
import type { DeviceData } from '../hooks/useDevice';
import { useI18n, type MessageKey } from '../lib/i18n';
import { freshness } from '../lib/status';
import { errorMessage } from '../lib/supabase';
import type { RelayKey } from '../lib/types';
import { CommandStatus } from './CommandStatus';

const RELAYS: { key: RelayKey; label: MessageKey; air: boolean }[] = [
  { key: 'k1', label: 'relayK1', air: true },
  { key: 'k5', label: 'relayK5', air: true },
  { key: 'k3', label: 'relayK3', air: false },
  { key: 'k4', label: 'relayK4', air: false },
];

export function PumpPanel({ data }: { data: DeviceData }) {
  const { t } = useI18n();
  const [relayError, setRelayError] = useState<string | null>(null);
  const [lockError, setLockError] = useState<string | null>(null);
  const s = data.state;
  const live = freshness(s?.updated_at, data.now).kind === 'live';
  const manual = s?.mode === 'MANUAL' && live;
  const lastRelay = data.commands.find((c) => c.kind === 'set_relays');
  const lastLockout = data.commands.find((c) => c.kind === 'clear_bilge_lockout');

  async function toggle(key: RelayKey, on: boolean) {
    setRelayError(null);
    try {
      await data.sendCommand('set_relays', { [key]: on });
    } catch (e) {
      setRelayError(errorMessage(e));
    }
  }

  async function clearLockout() {
    setLockError(null);
    try {
      await data.sendCommand('clear_bilge_lockout', {});
    } catch (e) {
      setLockError(errorMessage(e));
    }
  }

  const bilgeKey = s ? (`bilge${s.bilge_state}` as MessageKey) : null;

  return (
    <section className="panel stack">
      <h2>{t('pumpsTitle')}</h2>
      {s?.do_emergency && <p className="banner bad">{t('doEmergency')}</p>}
      <div>
        {RELAYS.map((r) => {
          const on = s ? Boolean(s[`relay_${r.key}`]) : false;
          const locked = r.air && on && Boolean(s?.do_emergency);
          return (
            <div className="pump" key={r.key}>
              <span className="name">{t(r.label)}</span>
              <span className={`pill${on ? ' on' : ''}`}>{s ? (on ? t('pumpOn') : t('pumpOff')) : '—'}</span>
              <button className="btn" disabled={!manual || locked} onClick={() => void toggle(r.key, !on)}>
                {on ? t('pumpTurnOff') : t('pumpTurnOn')}
              </button>
            </div>
          );
        })}
      </div>
      {!manual && <p className="small muted">{t('pumpsManualOnly')}</p>}
      {s?.do_emergency && manual && <p className="small muted">{t('pumpsEmergencyLock')}</p>}
      <CommandStatus command={lastRelay} now={data.now} sendError={relayError} />

      <div className="stack" style={{ borderTop: '1px solid var(--line)', paddingTop: 12 }}>
        <div className="row">
          <h3>{t('bilgeTitle')}</h3>
          <span className="spacer" />
          {bilgeKey && <span className={`badge ${s?.bilge_state === 'LOCKOUT' ? 'critical' : ''}`}>{t(bilgeKey)}</span>}
        </div>
        {s?.bilge_state === 'LOCKOUT' && (
          <>
            <p className="small">{t('bilgeLockoutBody', { n: s.bilge_fail_count })}</p>
            <button className="btn" disabled={!live} onClick={() => void clearLockout()}>
              {t('bilgeClear')}
            </button>
            <CommandStatus command={lastLockout} now={data.now} sendError={lockError} />
          </>
        )}
      </div>
    </section>
  );
}
