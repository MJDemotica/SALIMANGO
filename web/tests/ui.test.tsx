import { fireEvent, render, screen, within } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { AccessScreen } from '../src/components/AccessScreen';
import { DeviceView } from '../src/components/DeviceView';
import type { DeviceData } from '../src/hooks/useDevice';
import { I18nProvider } from '../src/lib/i18n';
import type { DeviceState } from '../src/lib/types';

vi.mock('../src/lib/history', async (orig) => ({
  ...(await orig<typeof import('../src/lib/history')>()),
  fetchAllRows: vi.fn().mockResolvedValue([]),
}));

const NOW = Date.parse('2026-10-26T10:00:00Z');

function state(over: Partial<DeviceState> = {}): DeviceState {
  return {
    device_id: 'd1',
    updated_at: '2026-10-26T09:59:55Z',
    seq: 10,
    uptime_s: 600,
    firmware_version: '2.0.0',
    simulated: false,
    wifi_rssi_dbm: -60,
    settings_version: 1,
    mode: 'AUTO',
    mode_source: 'selector',
    selector_position: 'AUTO',
    manual_expires_in_s: null,
    do_emergency: false,
    relay_k1: true,
    relay_k3: false,
    relay_k4: false,
    relay_k5: true,
    bilge_state: 'IDLE',
    bilge_fail_count: 0,
    ph: 7.91,
    ph_status: 'ok',
    ec_ms_cm: 12.3,
    ec25_ms_cm: 11.4,
    ec_status: 'ok',
    salinity_ppt: 7.1,
    salinity_status: 'ok',
    temp_c: 29.2,
    temp_status: 'ok',
    temp2_c: null,
    do_mg_l: 6.12,
    do_status: 'ok',
    nh3_fraction: 0.0712,
    nh3_status: 'ok',
    calibration: {
      ph: { state: 'calibrated', version: 2, cal_id: 5, params: { ph7_v: 2.51, slope_v_per_ph: 0.17, cal_temp_c: 26, divider_ratio: 1.5 } },
      ec: { state: 'default', version: 0, params: { cell_constant_k: 1, corr_low: 1, corr_high: 1 } },
      do: { state: 'default', version: 0, params: { points: 1, v1_mv: 1600, t1_c: 25 } },
    },
    faults: [],
    ...over,
  };
}

function data(over: Partial<DeviceData> = {}): DeviceData {
  return {
    loading: false,
    revoked: false,
    error: null,
    name: 'Float node 1',
    state: state(),
    settings: {
      device_id: 'd1',
      version: 1,
      sal_min_ppt: 28,
      sal_max_ppt: 32,
      sal_hyst_ppt: 2,
      bilge_max_run_min: 15,
      bilge_min_off_min: 30,
      bilge_max_fails: 3,
      retention_days: null,
      updated_at: '2026-10-26T09:00:00Z',
    },
    commands: [],
    events: [],
    calibrations: [],
    realtime: true,
    now: NOW,
    sendCommand: vi.fn().mockResolvedValue('c1'),
    updateSettings: vi.fn().mockResolvedValue(2),
    refresh: vi.fn(),
    ...over,
  };
}

const renderDevice = (d: DeviceData) =>
  render(
    <I18nProvider lang="en">
      <DeviceView deviceId="d1" data={d} onSwitch={() => {}} onRevoked={() => {}} />
    </I18nProvider>,
  );

describe('access screen', () => {
  it('asks for the sticker code only: no username or password field', () => {
    const { container } = render(
      <I18nProvider lang="en">
        <AccessScreen saved={[]} busy={false} error={null} notice={null} onSubmit={() => {}} onOpen={() => {}} onForget={() => {}} />
      </I18nProvider>,
    );
    expect(container.querySelector('input[type="password"]')).toBeNull();
    expect(screen.queryByText(/username|password/i)).toBeNull();
    expect(screen.getByLabelText('Access code')).toBeInTheDocument();
  });
});

describe('Water Quality Sensing tab', () => {
  it('shows the five indicators with units', () => {
    renderDevice(data());
    expect(within(screen.getByTestId('card-ph')).getByText('7.91')).toBeInTheDocument();
    expect(within(screen.getByTestId('card-salinity')).getByText('7.1')).toBeInTheDocument();
    expect(within(screen.getByTestId('card-temp')).getByText('29.2')).toBeInTheDocument();
    expect(within(screen.getByTestId('card-do')).getByText('6.12')).toBeInTheDocument();
    expect(within(screen.getByTestId('card-nh3')).getByText('7.12')).toBeInTheDocument();
    expect(screen.getByText(/not an ammonia concentration/i)).toBeInTheDocument();
  });

  it('shows a sensor fault as a fault, not as zero', () => {
    renderDevice(data({ state: state({ do_mg_l: null, do_status: 'fault' }) }));
    const card = screen.getByTestId('card-do');
    expect(within(card).getByText('Sensor fault')).toBeInTheDocument();
    expect(within(card).queryByText(/^0/)).toBeNull();
  });

  it('explains an over-range salinity from the K = 1 probe', () => {
    renderDevice(data({ state: state({ salinity_ppt: null, salinity_status: 'over_range', ec_ms_cm: null, ec25_ms_cm: null, ec_status: 'over_range' }) }));
    expect(within(screen.getByTestId('card-salinity')).getByText(/Above the K = 1 probe/)).toBeInTheDocument();
  });

  it('marks old data as stale and keeps the last known values', () => {
    renderDevice(data({ state: state({ updated_at: '2026-10-26T09:50:00Z' }) }));
    expect(screen.getByText(/No report from the device for 10 min/)).toBeInTheDocument();
    expect(within(screen.getByTestId('card-ph')).getByText('7.91')).toBeInTheDocument();
    expect(screen.getAllByText('last known').length).toBeGreaterThan(0);
  });

  it('labels simulated data clearly', () => {
    renderDevice(data({ state: state({ simulated: true }) }));
    expect(screen.getByText(/SIMULATED DATA/)).toBeInTheDocument();
  });

  it('asks for confirmation before sending OFF', () => {
    const d = data();
    renderDevice(d);
    fireEvent.click(screen.getByRole('button', { name: 'OFF' }));
    expect(screen.getByRole('alertdialog')).toBeInTheDocument();
    expect(d.sendCommand).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: 'Yes, turn OFF' }));
    expect(d.sendCommand).toHaveBeenCalledWith('set_mode', { mode: 'OFF', confirm: true });
  });

  it('shows requested and confirmed mode separately while waiting', () => {
    renderDevice(
      data({
        commands: [
          {
            id: 'c1',
            device_id: 'd1',
            created_at: '2026-10-26T09:59:58Z',
            expires_at: '2026-10-26T10:01:28Z',
            kind: 'set_mode',
            payload: { mode: 'MANUAL' },
            status: 'delivered',
            status_message: null,
            resolved_at: null,
          },
        ],
      }),
    );
    expect(screen.getByText('Requested')).toBeInTheDocument();
    expect(screen.getByText('Waiting for the device…')).toBeInTheDocument();
  });

  it('disables pump buttons outside MANUAL and when the selector overrides', () => {
    renderDevice(data({ state: state({ selector_position: 'OFF', mode: 'OFF' }) }));
    expect(screen.getByRole('button', { name: 'AUTO' })).toBeDisabled();
    for (const b of screen.getAllByRole('button', { name: /Turn (on|off)/ })) expect(b).toBeDisabled();
  });
});

describe('Calibration tab', () => {
  it('has pH, EC (K = 1), DO and ammonia sections and no temperature calibration', () => {
    renderDevice(data());
    fireEvent.click(screen.getByRole('tab', { name: 'Calibration' }));
    expect(screen.getByTestId('cal-ph')).toBeInTheDocument();
    expect(screen.getByTestId('cal-ec')).toBeInTheDocument();
    expect(screen.getByTestId('cal-do')).toBeInTheDocument();
    expect(screen.getByTestId('cal-nh3')).toBeInTheDocument();
    expect(screen.getByTestId('ec-k')).toHaveTextContent('1.0');
    expect(screen.queryByTestId('cal-temp')).toBeNull();
    expect(screen.getByText(/DO9100 handheld meter is a separate reference instrument/)).toBeInTheDocument();
  });

  it('sends calibration actions to the device', () => {
    const d = data();
    renderDevice(d);
    fireEvent.click(screen.getByRole('tab', { name: 'Calibration' }));
    fireEvent.click(within(screen.getByTestId('cal-ph')).getByRole('button', { name: 'Start' }));
    expect(d.sendCommand).toHaveBeenCalledWith('calibration', { sensor: 'ph', action: 'enter' });
  });
});

describe('language switch', () => {
  it('switches the interface to Filipino', () => {
    renderDevice(data());
    fireEvent.change(screen.getByLabelText('Language'), { target: { value: 'fil' } });
    expect(screen.getByRole('tab', { name: 'Kalidad ng Tubig' })).toBeInTheDocument();
  });
});
