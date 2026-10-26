# Troubleshooting Guide

**Project:** Solar Automated LoRa-Integrated Mud Crab Aeration Network with Crab Grid Observation

Symptom first. Cut power at the breaker for anything wet near 220 V AC. The Serial Monitor command
`status` (115200 baud) shows readings, relays, Wi-Fi and Supabase status in one line.

## 1. Power and electrical

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| RCD trips repeatedly | Moisture in a connector or damaged insulation | Dry out, re-terminate with IP67/IP68 parts; electrician to verify |
| 12 V sags when pumps start | Undersized conductor or weak PSU | Measure inrush (TESTING PWR-03); air pumps already start 5 s apart |
| Node reboots when pumps start | Brownout | Branch fuses/decoupling; check the buck converter; the `boot` event shows the reset reason |
| Relay clicks but never fully releases | 5 V relay module with 3.3 V logic inputs | Measure with IN at 3.3 V ([`WIRING.md`](WIRING.md) §6); use a module rated for 3.3 V inputs if needed |

## 2. Flashing and boot

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `secrets.h: No such file` | Not created | Copy `secrets.example.h` to `secrets.h` ([`../firmware/README.md`](../firmware/README.md)) |
| `ArduinoJson.h: No such file` | Library missing | Install ArduinoJson 7 |
| Stuck at "Connecting……" | Not in download mode | Hold **BOOT** while uploading; check COM port and CH340/CP210x driver |
| Upload works, nothing in Serial Monitor | Baud rate | 115200, Newline |
| "Sketch too big" | Partition scheme | Default 4MB with spiffs (the firmware uses ~88 %) |

## 3. Wi-Fi and Supabase

| Symptom (LED / `status`) | Likely cause | Fix |
|--------------------------|--------------|-----|
| Fast blink, `Wi-Fi down` | Wrong SSID/password, 5 GHz network, out of range | Fix `secrets.h`; use 2.4 GHz; add a repeater/outdoor AP near the float |
| Slow blink, `HTTP 401` | Wrong anon key | Re-copy the anon key (not service_role) |
| Slow blink, `Unknown or revoked device token` | Wrong token, device deactivated, or token rotated | Use the token from provisioning; check `devices.active` |
| Slow blink, `Connection failed` | Wrong `SUPABASE_URL` or HTTPS blocked | Check the URL (`https://<ref>.supabase.co`) |
| `HTTP 400 … device_state_withheld_values` | Firmware/database mismatch | Flash the firmware from this repository and apply the current migration |
| Steady LED but the dashboard is stale | Browser clock or Realtime | Reload; the page uses the server's clock and polls every 30 s if Realtime drops |

## 4. Dashboard

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| "This site is not configured yet" | Vercel env vars missing | Set `VITE_SUPABASE_URL`, `VITE_SUPABASE_ANON_KEY`; redeploy |
| "Could not start a session" | Anonymous sign-ins disabled | Supabase Auth → enable anonymous sign-ins |
| "That code was not recognised" | Typo, or the code was rotated | Check the sticker; ask the admin for the current code |
| "This phone no longer has access" | Code rotated | Scan the new sticker |
| "The device has not reported yet" | ESP32 not online or wrong token | See §3 |
| Mode buttons disabled | Selector on the box at M or O | Turn it to A |
| Pump buttons disabled | Not in MANUAL, or data stale | Switch to MANUAL; check the device link |
| Request shows "No answer from the device" | Device offline when it was sent | Check §3; send again |
| "SIMULATED DATA" banner | The simulator is reporting with this device's token | Stop `npm run simulate`; use a separate test device for simulations |

## 5. Sensors

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| pH *Sensor fault* | Board unplugged (divider pulls the pin to 0 V) | Check J6 and the PH-4502C supply |
| pH drifts over days | Electrode ageing or dry storage | Recalibrate; store in KCl only |
| pH calibration refused: slope out of range | Worn/dry probe, wrong or contaminated buffer, offset pot far off | Fresh buffers; rinse; set pH 7 ≈ 2.50 V; replace the probe |
| Salinity *over range* | Above the K = 1 probe's ≈ 18 mS/cm (≈ 11 ppt) | Expected in sea-strength water; this probe cannot measure it |
| EC calibration refused: matches neither | Wrong solution, probe not immersed, or temperature probe not in the same solution | Check both probes in the standard |
| DO stuck or low | Membrane fouled, bubbles, no flow | Service membrane/electrolyte; provide flow |
| DS18B20 *fault* / −127 °C | Wiring or missing 4.7 kΩ pull-up (R9) | Check J3 and GPIO19 |
| Everything *approximate* | Temperature probe faulted | Fix the DS18B20; values recover automatically |
| Calibration lost after flashing | Calibrated with a sketch that used another NVS namespace | See the README in `ph sense/` or `salinity sense/`; recalibrate |

## 6. Actuators

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Relay never clicks | No common ground, IN not driven LOW, relay VCC missing | Check J1, the buck OUT+ to relay VCC, GPIO 25/27/33/13 |
| Second air pump does not run | Lead still on IN2 (defective K2) | Move the GPIO27 lead to IN5 (K5) |
| Pumps chatter at boot | Missing pull-ups R1–R4 | Fit 10 kΩ pull-ups to 3V3 |
| Bilge pumps never start in AUTO | Salinity over range (counts as not low), lockout, or not in AUTO | Check the salinity card, *Salinity correction* status, and the mode |
| Bilge locked out | Salinity did not recover after the set tries | Check the water source and valves; *Clear lockout* |
| Air pumps on in OFF | DO emergency | Intended; check DO and aeration |

## 7. Escalation and safety

1. Anything wet near 220 V: **cut the breaker** and wait for the electrician.
2. Keep the test log ([`TESTING.md`](TESTING.md) §9); export the CSV before changing retention.
3. If the DO probe is out of service, run the air pumps in MANUAL or keep AUTO (a missing DO reading
   counts as low, so AUTO aerates) and use the DO9100 meter until it is replaced.

## Removed / deferred

- Camera node troubleshooting removed with the camera.
- LoRa link (antenna, radio settings, ACK/NAK): deferred with LoRa; see the deferred section of
  [`FIRMWARE.md`](FIRMWARE.md).
