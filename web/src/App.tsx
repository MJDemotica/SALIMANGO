import { useCallback, useEffect, useState } from 'react';
import { AccessScreen } from './components/AccessScreen';
import { DeviceView } from './components/DeviceView';
import { useDevice } from './hooks/useDevice';
import { clearCodeFromUrl, codeFromUrl, forgetDevice, loadSavedDevices, saveDevice } from './lib/access';
import { useI18n } from './lib/i18n';
import { ensureSession, errorMessage, supabase, supabaseConfigured } from './lib/supabase';
import type { SavedDevice } from './lib/types';

function ConnectedDevice({ device, onSwitch, onRevoked }: { device: SavedDevice; onSwitch: () => void; onRevoked: () => void }) {
  const data = useDevice(device.id);
  return <DeviceView deviceId={device.id} data={data} onSwitch={onSwitch} onRevoked={onRevoked} />;
}

export function App() {
  const { t } = useI18n();
  const [ready, setReady] = useState(false);
  const [sessionError, setSessionError] = useState<string | null>(null);
  const [saved, setSaved] = useState<SavedDevice[]>(() => loadSavedDevices());
  const [current, setCurrent] = useState<SavedDevice | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);

  const claim = useCallback(
    async (code: string) => {
      setBusy(true);
      setError(null);
      setNotice(null);
      try {
        const { data, error: e } = await supabase.rpc('claim_device_access', { p_code: code });
        if (e) throw e;
        const device = { id: data.device_id as string, name: data.name as string };
        setSaved(saveDevice(device));
        setCurrent(device);
      } catch (e) {
        const msg = errorMessage(e);
        setError(/not recognised/i.test(msg) ? t('accessInvalid') : t('errorGeneric', { msg }));
      } finally {
        setBusy(false);
      }
    },
    [t],
  );

  useEffect(() => {
    if (!supabaseConfigured) return;
    let cancelled = false;
    (async () => {
      try {
        await ensureSession();
        if (cancelled) return;
        setReady(true);
        const code = codeFromUrl(window.location.search);
        clearCodeFromUrl(); // the code is not left in the address bar or history
        if (code) await claim(code);
        else {
          const list = loadSavedDevices();
          if (list.length === 1) setCurrent(list[0]);
        }
      } catch (e) {
        if (!cancelled) setSessionError(errorMessage(e));
      }
    })();
    return () => {
      cancelled = true;
    };
    // run once on load
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const onRevoked = useCallback(() => {
    if (!current) return;
    setSaved(forgetDevice(current.id));
    setCurrent(null);
    setNotice(t('accessRevoked'));
  }, [current, t]);

  if (!supabaseConfigured) return <p className="banner bad wrap">{t('setupMissing')}</p>;
  if (sessionError) return <p className="banner bad wrap">{t('sessionError', { msg: sessionError })}</p>;
  if (!ready) return <p className="wrap muted" style={{ paddingTop: 24 }}>{t('loading')}</p>;

  if (current) {
    return <ConnectedDevice key={current.id} device={current} onSwitch={() => setCurrent(null)} onRevoked={onRevoked} />;
  }
  return (
    <AccessScreen
      saved={saved}
      busy={busy}
      error={error}
      notice={notice}
      onSubmit={(c) => void claim(c)}
      onOpen={setCurrent}
      onForget={(id) => setSaved(forgetDevice(id))}
    />
  );
}
