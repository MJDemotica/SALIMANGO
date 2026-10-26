import { useEffect, useState } from 'react';
import type { DeviceData } from '../hooks/useDevice';
import { formatAgo, useI18n, type MessageKey } from '../lib/i18n';
import { freshness } from '../lib/status';
import { CalibrationTab } from './CalibrationTab';
import { LanguageSwitch } from './LanguageSwitch';
import { SettingsTab } from './SettingsTab';
import { WaterQualityTab } from './WaterQualityTab';

type Tab = 'water' | 'cal' | 'settings';
const TABS: { id: Tab; label: MessageKey }[] = [
  { id: 'water', label: 'tabWater' },
  { id: 'cal', label: 'tabCal' },
  { id: 'settings', label: 'tabSettings' },
];

interface Props {
  deviceId: string;
  data: DeviceData;
  onSwitch: () => void;
  onRevoked: () => void;
}

export function DeviceView({ deviceId, data, onSwitch, onRevoked }: Props) {
  const { t } = useI18n();
  const [tab, setTab] = useState<Tab>('water');
  const fresh = freshness(data.state?.updated_at, data.now);

  useEffect(() => {
    if (data.revoked) onRevoked();
  }, [data.revoked, onRevoked]);

  const connKey: MessageKey = fresh.kind === 'live' ? 'connLive' : fresh.kind === 'stale' ? 'connStale' : 'connNone';

  return (
    <>
      <header className="topbar">
        <div className="wrap">
          <div style={{ flex: 1, minWidth: 180 }}>
            <h1>{data.name || t('appName')}</h1>
            <p className="sub">
              <span className="conn">
                <span className={`dot ${fresh.kind}`} aria-hidden="true" />
                {t(connKey)}
              </span>
              {data.state && ` · ${t('lastUpdate', { ago: formatAgo(t, fresh.ageS) })}`}
            </p>
          </div>
          <LanguageSwitch />
          <button className="linkbtn" onClick={onSwitch}>
            {t('switchPond')}
          </button>
        </div>
      </header>
      <main className="wrap">
        {data.state?.simulated && <p className="banner bad">{t('simulatedBanner')}</p>}
        {!data.loading && !data.state && <p className="banner info">{t('noDataBanner')}</p>}
        {fresh.kind === 'stale' && <p className="banner warn">{t('staleBanner', { ago: formatAgo(t, fresh.ageS) })}</p>}
        {data.state?.do_emergency && fresh.kind === 'live' && <p className="banner bad">{t('doEmergency')}</p>}
        {data.error && <p className="banner bad">{t('errorGeneric', { msg: data.error })}</p>}
        {!data.realtime && !data.loading && <p className="banner info small">{t('realtimeOff')}</p>}

        <nav className="tabs" role="tablist">
          {TABS.map((x) => (
            <button
              key={x.id}
              role="tab"
              aria-selected={tab === x.id}
              aria-controls={`panel-${x.id}`}
              onClick={() => setTab(x.id)}
            >
              {t(x.label)}
            </button>
          ))}
        </nav>
        <div role="tabpanel" id={`panel-${tab}`}>
          {tab === 'water' && <WaterQualityTab data={data} deviceId={deviceId} />}
          {tab === 'cal' && <CalibrationTab data={data} />}
          {tab === 'settings' && <SettingsTab data={data} deviceId={deviceId} />}
        </div>
      </main>
    </>
  );
}
