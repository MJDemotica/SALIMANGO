// Row shapes from supabase/migrations/20261026000000_salimango_initial.sql.
// Measurements are numbers or null; a null value is never a legitimate zero.

export type OperatingMode = 'AUTO' | 'MANUAL' | 'OFF';
export type SensorStatus = 'ok' | 'degraded' | 'over_range' | 'fault' | 'not_present';
export type DerivedStatus = 'ok' | 'over_range' | 'under_range' | 'unavailable';
export type BilgeState = 'IDLE' | 'RUNNING' | 'RESTING' | 'LOCKOUT';
export type RelayKey = 'k1' | 'k5' | 'k3' | 'k4';
export type CalSensor = 'ph' | 'ec' | 'do';
export type CommandKind = 'set_mode' | 'set_relays' | 'calibration' | 'clear_bilge_lockout';
export type CommandStatus = 'pending' | 'delivered' | 'accepted' | 'rejected' | 'expired' | 'superseded';

export interface CalibrationState {
  state: 'calibrated' | 'default';
  version: number;
  cal_id?: number;
  stored_invalid?: boolean;
  params?: Record<string, number | number[]>;
  session?: Record<string, unknown> | null;
}

export interface DeviceState {
  device_id: string;
  updated_at: string;
  seq: number;
  uptime_s: number;
  firmware_version: string | null;
  simulated: boolean;
  wifi_rssi_dbm: number | null;
  settings_version: number | null;
  mode: OperatingMode;
  mode_source: 'selector' | 'remote';
  selector_position: 'AUTO' | 'MANUAL' | 'OFF' | 'FAULT';
  manual_expires_in_s: number | null;
  do_emergency: boolean;
  relay_k1: boolean;
  relay_k3: boolean;
  relay_k4: boolean;
  relay_k5: boolean;
  bilge_state: BilgeState;
  bilge_fail_count: number;
  ph: number | null;
  ph_status: SensorStatus;
  ec_ms_cm: number | null;
  ec25_ms_cm: number | null;
  ec_status: SensorStatus;
  salinity_ppt: number | null;
  salinity_status: DerivedStatus;
  temp_c: number | null;
  temp_status: SensorStatus;
  temp2_c: number | null;
  do_mg_l: number | null;
  do_status: SensorStatus;
  nh3_fraction: number | null;
  nh3_status: DerivedStatus;
  calibration: Partial<Record<CalSensor, CalibrationState>>;
  faults: string[];
}

export interface DeviceSettings {
  device_id: string;
  version: number;
  sal_min_ppt: number;
  sal_max_ppt: number;
  sal_hyst_ppt: number;
  bilge_max_run_min: number;
  bilge_min_off_min: number;
  bilge_max_fails: number;
  retention_days: number | null;
  updated_at: string;
}

export interface CommandRow {
  id: string;
  device_id: string;
  created_at: string;
  expires_at: string;
  kind: CommandKind;
  payload: Record<string, unknown>;
  status: CommandStatus;
  status_message: string | null;
  resolved_at: string | null;
}

export interface EventRow {
  id: number;
  device_id: string;
  created_at: string;
  severity: 'info' | 'warning' | 'critical';
  kind: string;
  message: string;
}

export interface CalibrationRecord {
  id: number;
  device_id: string;
  sensor: CalSensor;
  cal_id: number;
  version: number;
  parameters: Record<string, number | number[]>;
  received_at: string;
  simulated: boolean;
}

export interface Reading {
  recorded_at: string;
  seq: number;
  simulated: boolean;
  mode: OperatingMode;
  do_emergency: boolean;
  relay_k1: boolean;
  relay_k3: boolean;
  relay_k4: boolean;
  relay_k5: boolean;
  bilge_state: BilgeState;
  ph: number | null;
  ph_status: SensorStatus;
  ec_ms_cm: number | null;
  ec25_ms_cm: number | null;
  ec_status: SensorStatus;
  salinity_ppt: number | null;
  salinity_status: DerivedStatus;
  temp_c: number | null;
  temp_status: SensorStatus;
  do_mg_l: number | null;
  do_status: SensorStatus;
  nh3_fraction: number | null;
  nh3_status: DerivedStatus;
  ph_cal_id: number | null;
  ec_cal_id: number | null;
  do_cal_id: number | null;
}

export interface SavedDevice {
  id: string;
  name: string;
}
