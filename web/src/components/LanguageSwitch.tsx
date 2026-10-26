import { useI18n, type Lang } from '../lib/i18n';

export function LanguageSwitch() {
  const { lang, setLang, t } = useI18n();
  return (
    <select aria-label={t('language')} value={lang} onChange={(e) => setLang(e.target.value as Lang)}>
      <option value="en">English</option>
      <option value="fil">Filipino</option>
    </select>
  );
}
