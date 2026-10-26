# Firmware

`salimango_node/` is the single ESP32 program for the float node. It reads pH, EC (salinity), DO and
temperature; runs AUTO / MANUAL / OFF control locally; and reports to Supabase over Wi-Fi. Pins
follow `perfboard_build_guide.pdf` and are defined only in `salimango_node/config.h`.

| File | Contents |
|------|----------|
| `config.h` | Pin map (with PDF wire numbers), timing, defaults |
| `secrets.example.h` | Template for `secrets.h` (Wi-Fi, Supabase URL/anon key, device token) |
| `sensor_math.*` | pH, EC (K = 1), DO, PSS-78 salinity and Emerson NH₃ formulas — no Arduino code |
| `control_logic.*` | Selector decode, mode rules, DO aeration, bilge state machine, relay staggering — no Arduino code |
| `sensors.*` | DS18B20, pH, EC, DO drivers, calibration sessions and NVS storage |
| `cloud.*` | Wi-Fi and the Supabase report/command task (core 0) |
| `salimango_node.ino` | Wiring it together: control loop, commands, events, serial bench commands |

## Uploading the firmware to the ESP32

### Before you start

- **Supabase Project URL and anon key**: Supabase → **Project Settings → API**.
- **Device token**: printed by `npm run provision` in `web/` (file `web/provisioning/<CODE>/secrets.h`).
  See [`docs/SETUP.md`](../docs/SETUP.md).
- **House Wi-Fi name and password**: the network must be **2.4 GHz** (the ESP32 cannot join 5 GHz).

> **Safety:** at boot the firmware turns the air pumps ON immediately (K1, then K5 5 s later), the
> fail-safe default. For the first upload, disconnect the pumps or the 12 V supply to the relay module,
> and power the ESP32 from the laptop USB only (buck converter USB unplugged), as the perfboard guide's
> testing checklist says.

### 1. Install the software (one time)

1. Install **Arduino IDE 2**.
2. **Tools → Board → Boards Manager**, search **esp32**, install **esp32 by Espressif Systems**
   (built and tested with 3.3.12).
3. **Tools → Manage Libraries**, install **OneWire**, **DallasTemperature** and **ArduinoJson** (7.x).
4. If no COM port appears when the ESP32 is plugged in, install the **CH340** or **CP210x** USB driver
   (the chip name is printed near the USB connector).

### 2. Create `secrets.h`

Copy `salimango_node/secrets.example.h` to `salimango_node/secrets.h` and fill it in:

```cpp
#define WIFI_SSID "YourHouseWiFi"
#define WIFI_PASS "your-wifi-password"
#define SUPABASE_URL "https://xxxxxxxx.supabase.co"
#define SUPABASE_ANON_KEY "eyJhbGciOi..."      // the anon / publishable key
#define DEVICE_TOKEN "Abc123..."              // from web/provisioning/<CODE>/secrets.h
```

Use the **anon** key, never the **service_role** key. `secrets.h` is git-ignored: never commit it.

### 3. Open the sketch

**File → Open** → `firmware/salimango_node/salimango_node.ino`. The other files in the folder
(`cloud.cpp`, `config.h`, `sensors.cpp`, `sensor_math.cpp`, `control_logic.cpp`, `secrets.h`) open as
tabs and are compiled together; keep them in the same folder.

### 4. Board and port

| Setting | Value |
|---|---|
| Tools → Board | **ESP32 Dev Module** |
| Tools → Port | The COM port that appears when the ESP32 is plugged in |
| Upload Speed | 921600 (use 115200 if uploads fail) |
| Partition Scheme | Default 4MB with spiffs (the firmware uses about 88 % of the app space) |

### 5. Upload

1. **Verify (✓)** first; it should end with "Sketch uses … 88%".
2. **Upload (→)**. If it stops at `Connecting......`, hold the **BOOT** button on the ESP32 until the
   upload starts.
3. "Hard resetting via RTS pin…" means the upload is finished.

Command-line alternative:

```
arduino-cli compile --upload -p COM5 --fqbn esp32:esp32:esp32 firmware/salimango_node
```

### 6. Check it in the Serial Monitor (115200 baud, Newline)

```
S.A.L.I.M.A.N.G.O. float node v2.0.0
DS18B20 on GPIO 19: 1 found
pH calibration: stored ... (or DEFAULT if never calibrated)
EC calibration (K = 1.0): ...
DO calibration: ...
[event:boot] Firmware 2.0.0 started ...
```

Type **`status`** for the readings, relay states and Wi-Fi / Supabase status.

| What you see | Meaning |
|---|---|
| `Wi-Fi up ... Supabase ok` | Working; the dashboard shows **Live** within about 10 s |
| `Wi-Fi down` | Wrong SSID/password, a 5 GHz network, or weak signal at the pond |
| `HTTP 401` or `Unknown or revoked device token` | Wrong anon key or device token in `secrets.h` |
| `Connection failed` | Wrong `SUPABASE_URL`, or the network blocks HTTPS |
| `DS18B20 ... 0 found` | Temperature probe wiring or the 4.7 kΩ pull-up (R9) |

Onboard blue LED: **steady** = reporting to Supabase · **slow blink** = Wi-Fi up, Supabase not reached ·
**fast blink** = no Wi-Fi.

### 7. After the first upload

- Reconnect the pumps and their supply, then test each relay from the dashboard in **MANUAL**.
- Calibrate pH, EC and DO from the dashboard's **Calibration** tab (or over serial, below).
- Editing `secrets.h` (for example a new Wi-Fi password) requires uploading again.

## Tests without hardware

```
cd firmware/tests/host
g++ -std=c++17 -I../../salimango_node test_firmware_logic.cpp ../../salimango_node/sensor_math.cpp ../../salimango_node/control_logic.cpp -o tests
./tests
```

These check the formulas and control rules only. They do not prove that the sensors, relays or
Wi-Fi work on the assembled board.

## Adding LoRa later (deferred in this phase)

LoRa is not implemented. The firmware is arranged so it can be added without touching sensing or
control:

- All communication goes through the six functions in `cloud.h` (`publishState`, `popCommand`,
  `pushAck`, `pushEvent`, `takeSettings`, `status`). The control loop never calls Wi-Fi or HTTP
  directly. A LoRa link (for example to a house bridge that forwards to the same `device_report`
  RPC) would be a second implementation of that interface.
- GPIO 16/17 are kept free for the E22 module's UART, matching the perfboard's E22 socket (W24/W25);
  `config.h` reserves them.
- The report is plain JSON; a LoRa frame would need a compact encoding, and the bridge would rebuild
  the same JSON so the database and dashboard stay unchanged.

The earlier sketches in `DOSENSE/`, `ph sense/` and `salinity sense/` are kept for reference (see the
README in each folder); several use pins that differ from the perfboard.

## Serial bench commands

`status` · `enterph` `calph` `exitph` `abortph` · `enterec` `calec` `exitec` `abortec` ·
`enterdo` `caldo` `exitdo` `abortdo` · `clearlockout`

The pH, EC and DO calibrations use the same flash storage as the earlier bench sketches, so a
calibration saved with those sketches is picked up (it is reported as version 1).
