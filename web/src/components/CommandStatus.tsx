import { useI18n, type MessageKey } from '../lib/i18n';
import { commandView, type CommandView } from '../lib/status';
import type { CommandRow } from '../lib/types';

const KEYS: Record<CommandView, MessageKey> = {
  pending: 'cmdPending',
  delivered: 'cmdDelivered',
  accepted: 'cmdAccepted',
  rejected: 'cmdRejected',
  expired: 'cmdExpired',
  timed_out: 'cmdTimedOut',
  superseded: 'cmdSuperseded',
};

/** Shows what happened to a request: sending, waiting, done, refused or timed out. */
export function CommandStatus({ command, now, sendError }: { command?: CommandRow; now: number; sendError?: string | null }) {
  const { t } = useI18n();
  if (sendError) return <p className="cmd error" role="status">{t('cmdSendFailed', { msg: sendError })}</p>;
  if (!command) return null;
  const view = commandView(command, now);
  const msg = command.status_message ?? '';
  const text = view === 'accepted' && msg ? `${t('cmdAccepted')} — ${msg}` : t(KEYS[view], { msg });
  return (
    <p className={`cmd ${view}`} role="status">
      {text}
    </p>
  );
}
