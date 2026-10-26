import type { SavedDevice } from './types';

const STORAGE_KEY = 'salimango.devices';

// Display formatting only; the database normalises codes (public.normalize_access_code).
export function formatAccessCode(input: string): string {
  const clean = input.toUpperCase().replace(/[^0-9A-Z]/g, '').slice(0, 12);
  return clean.length > 4 ? `${clean.slice(0, 4)}-${clean.slice(4)}` : clean;
}

export function codeFromUrl(search: string): string | null {
  const code = new URLSearchParams(search).get('code');
  return code && code.trim() ? code.trim() : null;
}

/** Removes ?code= from the address bar after it has been used. */
export function clearCodeFromUrl(): void {
  const url = new URL(window.location.href);
  if (!url.searchParams.has('code')) return;
  url.searchParams.delete('code');
  window.history.replaceState(null, '', url.pathname + url.search + url.hash);
}

export function loadSavedDevices(): SavedDevice[] {
  try {
    const parsed = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? '[]');
    return Array.isArray(parsed) ? parsed.filter((d) => d && typeof d.id === 'string') : [];
  } catch {
    return [];
  }
}

export function saveDevice(device: SavedDevice): SavedDevice[] {
  const list = [device, ...loadSavedDevices().filter((d) => d.id !== device.id)].slice(0, 10);
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(list));
  } catch {
    /* storage unavailable: access still works for this visit */
  }
  return list;
}

export function forgetDevice(id: string): SavedDevice[] {
  const list = loadSavedDevices().filter((d) => d.id !== id);
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(list));
  } catch {
    /* ignore */
  }
  return list;
}
