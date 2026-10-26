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

## Build and flash

1. Arduino IDE 2 with **esp32 by Espressif** (tested with core 3.3.12), board **ESP32 Dev Module**.
2. Libraries (Library Manager): **OneWire**, **DallasTemperature**, **ArduinoJson** (7.x).
3. Copy `salimango_node/secrets.example.h` to `salimango_node/secrets.h` and fill it in. Use the device
   token printed by `npm run provision` in `web/`. `secrets.h` is git-ignored; never commit it and never
   put the Supabase service-role key in it.
4. Open `salimango_node/salimango_node.ino` and upload. Serial Monitor: 115200 baud, newline.

Command-line build:

```
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/salimango_node
```

## Tests without hardware

```
cd firmware/tests/host
g++ -std=c++17 -I../../salimango_node test_firmware_logic.cpp ../../salimango_node/sensor_math.cpp ../../salimango_node/control_logic.cpp -o tests
./tests
```

These check the formulas and control rules only. They do not prove that the sensors, relays or
Wi-Fi work on the assembled board.

## Serial bench commands

`status` · `enterph` `calph` `exitph` `abortph` · `enterec` `calec` `exitec` `abortec` ·
`enterdo` `caldo` `exitdo` `abortdo` · `clearlockout`

The pH, EC and DO calibrations use the same flash storage as the earlier bench sketches, so a
calibration saved with those sketches is picked up (it is reported as version 1).
