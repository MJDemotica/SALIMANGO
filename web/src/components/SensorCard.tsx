import type { ReactNode } from 'react';
import { useI18n, type MessageKey } from '../lib/i18n';
import type { Level } from '../lib/status';
import type { DerivedStatus, SensorStatus } from '../lib/types';

const STATUS_KEY: Record<SensorStatus | DerivedStatus, MessageKey> = {
  ok: 'statusOk',
  degraded: 'statusDegraded',
  over_range: 'statusOverRange',
  under_range: 'statusUnderRange',
  fault: 'statusFault',
  not_present: 'statusNotPresent',
  unavailable: 'statusUnavailable',
};

const LEVEL_KEY: Partial<Record<Level, MessageKey>> = {
  good: 'levelGood',
  warning: 'levelWarning',
  critical: 'levelCritical',
};

export function statusKey(s: SensorStatus | DerivedStatus): MessageKey {
  return STATUS_KEY[s];
}

interface Props {
  title: string;
  /** Formatted value; null when the device withheld it (fault, over range…). */
  value: string | null;
  unit?: string;
  status: SensorStatus | DerivedStatus | null;
  level: Level;
  stale: boolean;
  badge?: ReactNode;
  children?: ReactNode;
  testId?: string;
}

export function SensorCard({ title, value, unit, status, level, stale, badge, children, testId }: Props) {
  const { t } = useI18n();
  const levelKey = value !== null ? LEVEL_KEY[level] : undefined;
  return (
    <section
      className={`card level-${value !== null ? level : 'unknown'}${stale ? ' is-stale' : ''}`}
      aria-label={title}
      data-testid={testId}
    >
      <div className="card-head">
        <h2 className="card-title">{title}</h2>
        {badge}
      </div>
      {value !== null ? (
        <p className="reading">
          {value}
          {unit && <span className="unit">{unit}</span>}
        </p>
      ) : (
        <p className="reading none">{status ? t(statusKey(status)) : t('loading')}</p>
      )}
      <div className="row">
        {status && value !== null && status !== 'ok' && <span className="badge warning">{t(statusKey(status))}</span>}
        {levelKey && <span className={`badge ${level}`}>{t(levelKey)}</span>}
        {stale && <span className="badge">{t('modeLastKnown')}</span>}
      </div>
      {children && <div className="meta">{children}</div>}
    </section>
  );
}
