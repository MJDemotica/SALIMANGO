import { useState, type FormEvent } from 'react';
import { formatAccessCode } from '../lib/access';
import { useI18n } from '../lib/i18n';
import type { SavedDevice } from '../lib/types';
import { LanguageSwitch } from './LanguageSwitch';

interface Props {
  saved: SavedDevice[];
  busy: boolean;
  error: string | null;
  notice: string | null;
  onSubmit: (code: string) => void;
  onOpen: (d: SavedDevice) => void;
  onForget: (id: string) => void;
}

/** Sticker code entry. There is deliberately no username/password form. */
export function AccessScreen({ saved, busy, error, notice, onSubmit, onOpen, onForget }: Props) {
  const { t } = useI18n();
  const [code, setCode] = useState('');

  function submit(e: FormEvent) {
    e.preventDefault();
    if (code.replace(/-/g, '').length >= 8) onSubmit(code);
  }

  return (
    <main className="access stack">
      <div className="row">
        <h1 style={{ fontSize: '1.15rem' }}>{t('appName')}</h1>
        <span className="spacer" />
        <LanguageSwitch />
      </div>
      <form className="panel stack" onSubmit={submit}>
        <h2>{t('accessTitle')}</h2>
        <p className="small muted">{t('accessIntro')}</p>
        {notice && <p className="banner warn">{notice}</p>}
        <div className="field">
          <label htmlFor="access-code">{t('accessCodeLabel')}</label>
          <input
            id="access-code"
            className="code-input"
            autoComplete="off"
            autoCapitalize="characters"
            spellCheck={false}
            inputMode="text"
            placeholder="XXXX-XXXX"
            value={code}
            onChange={(e) => setCode(formatAccessCode(e.target.value))}
          />
        </div>
        {error && (
          <p className="error-text" role="alert">
            {error}
          </p>
        )}
        <button className="btn primary" type="submit" disabled={busy || code.replace(/-/g, '').length < 8}>
          {busy ? t('accessOpening') : t('accessOpen')}
        </button>
      </form>
      {saved.length > 0 && (
        <section className="panel">
          <h2>{t('accessSaved')}</h2>
          <ul className="saved">
            {saved.map((d) => (
              <li key={d.id}>
                <button className="btn" style={{ flex: 1, textAlign: 'left' }} onClick={() => onOpen(d)}>
                  {d.name}
                </button>
                <button className="btn danger" onClick={() => onForget(d.id)}>
                  {t('accessForget')}
                </button>
              </li>
            ))}
          </ul>
        </section>
      )}
    </main>
  );
}
