# Firmware Guide

**Project:** Solar Automated LoRa-Integrated Mud Crab Aeration Network with Crab Grid Observation
**Current phase:** one ESP32 float node, direct Wi-Fi to Supabase. LoRa and the house bridge are
deferred; the camera node is removed.

Source: [`firmware/salimango_node/`](../firmware/salimango_node/). How to install and upload:
[`firmware/README.md`](../firmware/README.md).

Guiding rules: **every control decision is made on the ESP32**; the network is never required to
keep the crabs alive; boot and any doubt fail toward **aeration ON**.

## 1. Structure

| File | Responsibility |
|------|----------------|
| `config.h` | The only pin map (from `perfboard_build_guide.pdf`), timing, defaults |
| `sensor_math.*` | pH, EC (K = 1), DO, PSS-78 salinity, Emerson NH₃ — pure functions, host-tested |
| `control_logic.*` | Selector decode, mode rules, DO aeration, bilge state machine, relay staggering — pure, host-tested |
| `sensors.*` | DS18B20, pH, EC, DO drivers; validation; calibration sessions; NVS storage |
| `cloud.*` | Wi-Fi + Supabase task; command/ack/event queues |
| `salimango_node.ino` | Main loop: read → derive → control → outputs → report; command handling; serial bench commands |

Two FreeRTOS contexts:

- **Arduino loop (core 1):** sensing, control, relays. Never waits on the network.
- **Cloud task (core 0):** Wi-Fi reconnects, one HTTPS call per report cycle, JSON parsing. Shares data
  with the loop only through a mutex-protected state buffer and queues.

Libraries: OneWire, DallasTemperature, ArduinoJson 7; Preferences, WiFi, HTTPClient and
NetworkClientSecure from the ESP32 core (3.3.12). TLS uses the core's built-in CA bundle
(`useBuiltinCACertBundle`); certificate checking is not disabled.

## 2. Control rules

| Rule | Behaviour | Source |
|------|-----------|--------|
| Boot / reset | Mode AUTO (unless the selector says M or O); air pumps ON until DO is confirmed good | FIRMWARE.md (previous) fail-safe rule |
| Selector authority | M → MANUAL, O → OFF regardless of the dashboard; A → the dashboard chooses AUTO / MANUAL / OFF; FAULT (both closed) → treated as A | Project decision; decoding from `selector_test` |
| MANUAL timeout | Back to AUTO after 5 min without a manual command (dashboard MANUAL and selector M alike) | Previous FIRMWARE.md |
| AUTO aeration | DO < 3.0 mg/L for 3 readings → K1 on, K5 5 s later; DO > 3.0 for 3 readings → both off together; ≥ 60 s on, ≥ 10 s off; an invalid DO counts as low | `DO_ESP32_DS18B20_WiFi4`; stagger and K5 per project decision |
| DO emergency | A **valid** DO < 3.0 mg/L for 3 readings forces K1 + K5 on in **every** mode, including OFF; cleared by 3 valid readings > 3.0; MANUAL requests to switch them off are rejected | Previous FIRMWARE.md / APP.md interlock |
| DO calibration | AUTO aeration and emergency evaluation are held while the DO probe is being calibrated (probe out of the water) | `DO_ESP32_DS18B20_WiFi4` |
| AUTO bilge | `salinity_rev.md`: salinity < salMin for 3 readings → K3 on, K4 2 s later; stop at ≥ salMin + salHyst (recovered) or after the maximum run (timeout); rest the minimum off time; after N timeouts in a row → LOCKOUT until salinity recovers or the operator clears it | [`salinity_rev.md`](../salinity_rev.md) |
| Bilge and probe range | Salinity above the K = 1 range (over range) counts as *not low* and as recovered; below 2 ppt (under range) counts as low; a missing reading stops a running cycle (an existing lockout is kept) | Project decision |
| Bilge outside AUTO | MANUAL: the operator switches K3/K4; OFF: off. A running AUTO cycle is abandoned when leaving AUTO | — |
| OFF | All relays off, except the DO emergency | — |
| Calibration session | Cancelled automatically after 30 min without saving | Added safety guard |
| Relay outputs | Driven HIGH (off) before `pinMode` at boot; external 10 kΩ pull-ups hold them off through reset | `relay_wifi_test_k1k4`, guide R1–R4 |

Within each pump group only one pump may start per stagger interval (air 5 s, bilge 2 s); stopping
is immediate.

## 3. Timing

| Timer | Value | `config.h` |
|-------|-------|------------|
| Sensor read + control | 3 s (1 s while calibrating; control decisions stay at 3 s) | `CONTROL_INTERVAL_MS`, `CAL_READ_INTERVAL_MS` |
| EC sampling | 40 ms samples, 1 s trimmed-mean window (25 samples, 5 trimmed each side) | `EC_SAMPLE_MS`, `EC_WINDOW` |
| pH / DO sampling | 10 samples × 10 ms per reading (cancels 60 Hz hum) | `PH_SAMPLES`, `DO_SAMPLES` |
| DS18B20 | 12-bit, non-blocking 750 ms conversion | — |
| Report to Supabase | Every 10 s; every 2 s while calibrating or right after a command; backoff 5 → 60 s on failure | `REPORT_*` |
| Wi-Fi retry | 15 s | `WIFI_RETRY_MS` |
| History row in the database | At most one per minute (server side) | migration |

## 4. Sensor pipeline

1. **Temperature:** DS18B20 probe 1 (probe 2 on the same bus is the fallback). −127, 85.0 and values
   outside −10…60 °C are rejected. Without a valid temperature, pH, EC and DO use 25 °C and are marked
   `degraded`; salinity and NH₃ are not computed.
2. **pH (GPIO34):** module voltage = pin mV × 1.5 (as-built divider);
   pH = 7 + (V₇ − V) / (slope × (T + 273.15) / (T_cal + 273.15)). Pin < 50 mV → `fault` (board
   unplugged); > 3.1 V → `over_range`; outside 0–14 → `fault`.
3. **EC (GPIO35):** raw = **K** × 1000 × mV / (820 × 200) with **K = 1** (`EC_CELL_CONSTANT_K`, applied
   once). EC at the measured temperature = raw × correction factor (DFRobot "kvalue", auto-ranged between
   the low- and high-range factors). EC25 = EC / (1 + 0.0185 (T − 25)). raw > 20 mS/cm or > 3.0 V →
   `over_range` (the K = 1 ceiling).
4. **Salinity:** UNESCO PSS-78 from EC at the measured temperature (not EC25), surface pressure.
   S < 2 → `under_range`; S > 42 → `over_range`; temperature outside −2…35 °C or no valid EC/temperature
   → `unavailable`.
5. **DO (GPIO36):** DO = mV × DO_sat(T) / V_sat(T), DFRobot saturation table, one- or two-point
   calibration. > 3.1 V → `over_range`; non-physical → `fault`.
6. **NH₃ fraction:** Emerson et al. (1975): pKa = 0.09018 + 2729.92 / (T + 273.2);
   f = 1 / (10^(pKa − pH) + 1). Only with `ok` pH and valid temperature; reported only for pH 6–10 and
   0–40 °C. A fraction, not a concentration (no TAN is measured).

A value is never sent when its status is `fault`, `over_range` or `not_present`; the database rejects
a report that tries.

## 5. Calibration storage

| Sensor | NVS namespace | Keys | Default (uncalibrated) |
|--------|---------------|------|------------------------|
| pH | `ph` | `ph7V`, `slope`, `calT`, `pts`, `ver`, `calId` | 2.50 V, 0.18 V/pH, 25 °C |
| EC | `ec_cal` | `kLow`, `kHigh`, `std`, `ver`, `calId` | 1.0 / 1.0 |
| DO | `do_cal` | `pts`, `v1`, `t1`, `v2`, `t2`, `ver`, `calId` | 1600 mV @ 25 °C |
| Settings | `settings` | `ver`, `salMin`, `salMax`, `salHyst`, `maxRun`, `minOff`, `maxFails` | 28 / 32 / 2 ppt, 15 / 30 min, 3 |

- Values are validated at boot (pH slope 0.12–0.24 V/pH; EC factors 0.5–1.5; DO 100–3100 mV, two-point
  ≥ 5 °C apart). Invalid stored values are ignored, the defaults are used and `stored_invalid` is
  reported.
- Every save increments `ver` and draws a new random `calId`. The report carries both; the database
  keeps one `calibration_records` row per `calId`, and every history row records the `cal_id` in force.
- Namespaces match the bench sketches, so a calibration saved with `ph_cali_no_ten_k2`,
  `salimango_ec_calibration*` or `DO_ESP32_DS18B20_WiFi4` is loaded and reported as version 1.
- Settings received from the dashboard are range-checked again and stored, so AUTO uses them after a
  reboot without Wi-Fi.

## 6. Telemetry contract (device → Supabase)

`POST {SUPABASE_URL}/rest/v1/rpc/device_report` with headers `apikey` and `Authorization: Bearer`
set to the **anon** key, body `{"p_token": DEVICE_TOKEN, "p_report": {...}}`:

```json
{
  "seq": 42, "uptime_s": 3600, "fw": "2.0.0", "simulated": false, "rssi": -61, "settings_version": 2,
  "mode": "AUTO", "mode_source": "selector", "selector": "AUTO", "manual_expires_in_s": null,
  "do_emergency": false,
  "relays": { "k1": true, "k5": true, "k3": false, "k4": false },
  "bilge": { "state": "IDLE", "fail_count": 0 },
  "ph":       { "value": 7.91, "status": "ok", "mv": 1580 },
  "ec":       { "ec_ms_cm": 12.1, "ec25_ms_cm": 11.2, "status": "ok", "mv": 1980.5 },
  "salinity": { "value": 6.9, "status": "ok" },
  "temp":     { "value": 29.12, "status": "ok", "t2": null },
  "do":       { "value": 6.2, "status": "ok", "mv": 1100 },
  "nh3":      { "fraction": 0.0712, "status": "ok" },
  "cal": {
    "ph": { "state": "calibrated", "version": 2, "cal_id": 123456789,
            "params": { "ph7_v": 2.51, "slope_v_per_ph": 0.172, "cal_temp_c": 26.1, "points": 2, "divider_ratio": 1.5 },
            "session": null },
    "ec": { "state": "default", "version": 0, "params": { "cell_constant_k": 1, "corr_low": 1, "corr_high": 1, "temp_coef_per_c": 0.0185 }, "session": null },
    "do": { "state": "default", "version": 0, "params": { "points": 1, "v1_mv": 1600, "t1_c": 25 }, "session": null }
  },
  "faults": [],
  "acks":   [{ "id": "<command uuid>", "status": "accepted", "message": "Pump states applied" }],
  "events": [{ "severity": "critical", "kind": "do_emergency", "message": "..." }]
}
```

Status values: `ok`, `degraded`, `over_range`, `fault`, `not_present` (measured);
`ok`, `over_range`, `under_range`, `unavailable` (derived). While calibrating, `cal.<sensor>.session`
carries the live stability flag and captured points.

The response is `{"commands": [...], "settings": {...}, "server_time": "..."}`.

## 7. Commands (Supabase → device)

| Kind | Payload | Device checks before acting |
|------|---------|-----------------------------|
| `set_mode` | `{"mode": "AUTO" \| "MANUAL" \| "OFF", "confirm": true}` (confirm required for OFF) | Rejected when the selector is at M or O (except re-entering MANUAL at M) |
| `set_relays` | any of `k1`, `k5`, `k3`, `k4` → `true`/`false` | Only in MANUAL; switching K1/K5 off is rejected during a DO emergency |
| `calibration` | `{"sensor": "ph" \| "ec" \| "do", "action": "enter" \| "capture" \| "save" \| "abort"}` | Same validation as the serial commands (stability, temperature, buffer/standard recognition, slope/factor limits) |
| `clear_bilge_lockout` | `{}` | Only if the bilge is locked out |

The database validates the same payloads first (`request_command`). Commands expire 90 s after they
are requested; the device rejects one that arrives already expired, acknowledges every command
(`accepted` / `rejected` + message), and de-duplicates re-deliveries by id (the last 8 are remembered).

## 8. Events

Sent with the next report: boot (with reset reason), selector moves and faults, mode changes and
MANUAL timeouts, DO emergency start/end, bilge start/recovered/timeout/lockout, sensor fault and
recovery, calibration saves and timeouts, settings applied/rejected, and cloud link restored (with
the outage length). Up to 16 events are queued while offline; the oldest are dropped first.

## 9. Network loss

Sensing, AUTO control, the DO emergency, the selector and the MANUAL timeout all keep working. The
onboard LED blinks (slow: Wi-Fi up but Supabase unreachable; fast: no Wi-Fi). The dashboard shows the
last values as stale; it never shows an old mode as confirmed.

## 10. Serial bench commands

115200 baud, newline: `status` · `enterph` `calph` `exitph` `abortph` · `enterec` `calec` `exitec`
`abortec` · `enterdo` `caldo` `exitdo` `abortdo` · `clearlockout`.

## 11. Verification status

| Item | Status |
|------|--------|
| Compiles for ESP32 Dev Module (core 3.3.12): 88 % flash, 21 % RAM | Implemented and tested (build) |
| Formulas and control rules: 101 host checks (`firmware/tests/host`) | Implemented and tested (host) |
| Report/command contract against the database (PGlite tests in `web/tests/db`) | Implemented and tested (fixture shaped like the firmware report) |
| Sensor readings, calibration, relays, selector, Wi-Fi and Supabase on the real board | **Implemented but unverified** (not flashed in this revision) |

## Deferred: LoRa (future phase)

The previous design used an EBYTE E22-900T22D on the float and a second ESP32 + E22 "house bridge"
(915 MHz point-to-point, CRC-8 frames, ACK/NAK, 60 s heartbeat). It is not implemented in this phase.
To add it: implement the `cloud.h` interface over LoRa, keep GPIO16/17 for the E22 UART, and have the
bridge forward the same JSON to `device_report` so the database and dashboard need no change. The
previous design is kept below for that phase (see also
[`CHANGELOG-LORA-MODULE-UPDATE.md`](CHANGELOG-LORA-MODULE-UPDATE.md)). It predates this revision:
re-check it against the JSON contract (§6), the relay set K1/K5/K3/K4 and the guide's UART wiring
(GPIO17 → E22 RXD, GPIO16 ← E22 TXD) before using it.

### Previous radio configuration (identical on every node)

| Setting | Value |
|---------|-------|
| Frequency | 915.0 MHz (PH NTC licence-free SRD band — confirm measured EIRP and type-approval) |
| Spreading factor | SF9 (SF7–12 supported) |
| Bandwidth / coding rate | 125 kHz / 4-5 |
| Preamble | 8 |
| Sync word | 0x3444 (private; never LoRaWAN public) |
| CRC | On |
| TX power | Within the NTC EIRP limit |

### Previous packet protocol

All frames end in **CRC-8 (poly 0x07)** over the preceding bytes; multi-byte values little-endian,
scaled ×100.

Uplink, float → bridge (17 bytes):

| Byte | Field | Notes |
|------|-------|-------|
| 0 | Device ID | 1 = float |
| 1–2 | Sequence number | uint16 |
| 3 | Flags | Bits 0–1 mode (0 AUTO, 1 MANUAL, 2 OFF); bit 2 emergency; bit 3 sensor fault |
| 4 | Pump bitmap | Old layout: bit 0 air1, bit 1 air2, bit 2 bilge1, bit 3 bilge2 |
| 5–6 | DO (ppm ×100) | uint16 |
| 7–8 | pH (×100) | uint16 |
| 9–10 | EC (mS/cm ×100) | uint16 |
| 11–12 | Temperature 1 (°C ×100) | int16 |
| 13–14 | Temperature 2 (°C ×100) | int16 |
| 15 | Status/reserved | Uplink interval, uptime bucket |
| 16 | CRC-8 | Over bytes 0–15 |

Downlink, bridge → float (8 bytes): byte 0 device ID; 1–2 command sequence; 3 mode (0 AUTO, 1 MANUAL,
2 OFF, 255 no change); 4 pump bitmap (0xFF no change); 5–6 expiry (s); 7 CRC-8.

ACK/NAK, float → bridge (4 bytes): byte 0 device ID; byte 1 `0x06` ACK or `0x15` NAK; byte 2 echoed
command sequence (low byte); byte 3 CRC-8.

Previous timing: 60 s heartbeat uplink, immediate emergency uplink when DO < 3 ppm, bridge
heartbeat-loss alarm after 90 s without an uplink.
