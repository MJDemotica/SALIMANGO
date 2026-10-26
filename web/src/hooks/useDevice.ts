import { useCallback, useEffect, useRef, useState } from 'react';
import type { SupabaseClient } from '@supabase/supabase-js';
import { errorMessage, supabase } from '../lib/supabase';
import type {
  CalibrationRecord,
  CommandKind,
  CommandRow,
  DeviceSettings,
  DeviceState,
  EventRow,
} from '../lib/types';

export interface DeviceData {
  loading: boolean;
  revoked: boolean;
  error: string | null;
  name: string;
  state: DeviceState | null;
  settings: DeviceSettings | null;
  commands: CommandRow[];
  events: EventRow[];
  calibrations: CalibrationRecord[];
  realtime: boolean;
  /** Current time on the server's clock (ms), ticking every second. */
  now: number;
  sendCommand: (kind: CommandKind, payload: Record<string, unknown>) => Promise<string | null>;
  updateSettings: (s: SettingsInput) => Promise<number>;
  refresh: () => Promise<void>;
}

export interface SettingsInput {
  sal_min_ppt: number;
  sal_max_ppt: number;
  sal_hyst_ppt: number;
  bilge_max_run_min: number;
  bilge_min_off_min: number;
  bilge_max_fails: number;
  retention_days: number | null;
}

const COMMAND_LIMIT = 40;
const EVENT_LIMIT = 30;

function upsertBy<T>(list: T[], row: T, key: (r: T) => unknown, sort: (a: T, b: T) => number, limit: number): T[] {
  const k = key(row);
  return [row, ...list.filter((r) => key(r) !== k)].sort(sort).slice(0, limit);
}

const newestFirst = (field: string) => (a: unknown, b: unknown) =>
  String((b as Record<string, string>)[field]).localeCompare(String((a as Record<string, string>)[field]));

export function useDevice(deviceId: string, client: SupabaseClient = supabase): DeviceData {
  const [loading, setLoading] = useState(true);
  const [revoked, setRevoked] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [name, setName] = useState('');
  const [state, setState] = useState<DeviceState | null>(null);
  const [settings, setSettings] = useState<DeviceSettings | null>(null);
  const [commands, setCommands] = useState<CommandRow[]>([]);
  const [events, setEvents] = useState<EventRow[]>([]);
  const [calibrations, setCalibrations] = useState<CalibrationRecord[]>([]);
  const [realtime, setRealtime] = useState(false);
  const [now, setNow] = useState(() => Date.now());
  const offsetRef = useRef(0);

  const refresh = useCallback(async () => {
    try {
      const [dev, st, set, cmd, ev, cal, time] = await Promise.all([
        client.from('devices').select('id, name').eq('id', deviceId).maybeSingle(),
        client.from('device_state').select('*').eq('device_id', deviceId).maybeSingle(),
        client.from('device_settings').select('*').eq('device_id', deviceId).maybeSingle(),
        client.from('commands').select('*').eq('device_id', deviceId).order('created_at', { ascending: false }).limit(COMMAND_LIMIT),
        client.from('events').select('*').eq('device_id', deviceId).order('created_at', { ascending: false }).limit(EVENT_LIMIT),
        client.from('calibration_records').select('*').eq('device_id', deviceId).order('received_at', { ascending: false }).limit(60),
        client.rpc('server_time'),
      ]);
      const failed = [dev, st, set, cmd, ev, cal].find((r) => r.error);
      if (failed?.error) throw failed.error;
      if (!dev.data) {
        setRevoked(true); // RLS hides the device: the grant is gone or the code was rotated
        return;
      }
      setName(dev.data.name);
      setState(st.data as DeviceState | null);
      setSettings(set.data as DeviceSettings | null);
      setCommands((cmd.data ?? []) as CommandRow[]);
      setEvents((ev.data ?? []) as EventRow[]);
      setCalibrations((cal.data ?? []) as CalibrationRecord[]);
      if (!time.error && typeof time.data === 'string') {
        offsetRef.current = Date.parse(time.data) - Date.now();
        setNow(Date.now() + offsetRef.current);
      }
      setError(null);
    } catch (e) {
      setError(errorMessage(e));
    } finally {
      setLoading(false);
    }
  }, [client, deviceId]);

  useEffect(() => {
    void refresh();
    const filter = `device_id=eq.${deviceId}`;
    const channel = client
      .channel(`device-${deviceId}`)
      .on('postgres_changes', { event: '*', schema: 'public', table: 'device_state', filter }, (p) => {
        if (p.new && 'device_id' in p.new) setState(p.new as DeviceState);
      })
      .on('postgres_changes', { event: '*', schema: 'public', table: 'device_settings', filter }, (p) => {
        if (p.new && 'device_id' in p.new) setSettings(p.new as DeviceSettings);
      })
      .on('postgres_changes', { event: '*', schema: 'public', table: 'commands', filter }, (p) => {
        if (p.new && 'id' in p.new)
          setCommands((l) => upsertBy(l, p.new as CommandRow, (r) => r.id, newestFirst('created_at'), COMMAND_LIMIT));
      })
      .on('postgres_changes', { event: 'INSERT', schema: 'public', table: 'events', filter }, (p) => {
        setEvents((l) => upsertBy(l, p.new as EventRow, (r) => r.id, newestFirst('created_at'), EVENT_LIMIT));
      })
      .on('postgres_changes', { event: 'INSERT', schema: 'public', table: 'calibration_records', filter }, (p) => {
        setCalibrations((l) => upsertBy(l, p.new as CalibrationRecord, (r) => r.id, newestFirst('received_at'), 60));
      })
      .subscribe((status) => setRealtime(status === 'SUBSCRIBED'));
    return () => {
      void client.removeChannel(channel);
    };
  }, [client, deviceId, refresh]);

  // Fallback while realtime is not connected
  useEffect(() => {
    if (realtime) return;
    const id = setInterval(() => void refresh(), 30_000);
    return () => clearInterval(id);
  }, [realtime, refresh]);

  useEffect(() => {
    const id = setInterval(() => setNow(Date.now() + offsetRef.current), 1000);
    return () => clearInterval(id);
  }, []);

  const sendCommand = useCallback(
    async (kind: CommandKind, payload: Record<string, unknown>) => {
      const { data, error: e } = await client.rpc('request_command', {
        p_device: deviceId,
        p_kind: kind,
        p_payload: payload,
      });
      if (e) throw new Error(e.message);
      void refresh();
      return (data as string) ?? null;
    },
    [client, deviceId, refresh],
  );

  const updateSettings = useCallback(
    async (s: SettingsInput) => {
      const { data, error: e } = await client.rpc('update_device_settings', {
        p_device: deviceId,
        p_sal_min_ppt: s.sal_min_ppt,
        p_sal_max_ppt: s.sal_max_ppt,
        p_sal_hyst_ppt: s.sal_hyst_ppt,
        p_bilge_max_run_min: s.bilge_max_run_min,
        p_bilge_min_off_min: s.bilge_min_off_min,
        p_bilge_max_fails: s.bilge_max_fails,
        p_retention_days: s.retention_days,
      });
      if (e) throw new Error(e.message);
      void refresh();
      return data as number;
    },
    [client, deviceId, refresh],
  );

  return {
    loading,
    revoked,
    error,
    name,
    state,
    settings,
    commands,
    events,
    calibrations,
    realtime,
    now,
    sendCommand,
    updateSettings,
    refresh,
  };
}
