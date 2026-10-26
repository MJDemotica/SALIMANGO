# salinity sense — earlier EC / salinity sketches (kept for reference)

**For the float node, flash `firmware/salimango_node/` instead.** These sketches are kept unchanged
until the website is finalised. The probe is the DFRobot DFR0300 with cell constant **K = 1**.

| Sketch | Pins | Status |
|--------|------|--------|
| `salimango_ec_calibration_wifi` | EC 35, 1-Wire 19 (match the perfboard) | **Reused**: its trimmed-mean sampling, temperature-aware standard recognition (1.413 / 12.88 mS/cm), correction-factor limits, stability check and range flags are in the new firmware. Contains a Wi-Fi password and a hotspot password in plain text (in git history: change them). |
| `salimango_ec_calibration`, `salimango_ec_calibration2` | EC 35, 1-Wire 19 | Earlier versions of the above (serial only). Not used. |
| `EC_ESP32_DS18B20`, `salimango_dfrobot_calibration_salinity` | **EC 34** (perfboard: 35) | Not used; fixed 25 °C-based recognition windows. |
| `EC_ESP32_2xDS18B20` | **EC 34, 1-Wire on GPIO 4** (perfboard: 35 and 19; GPIO 4 is the selector M input) | Not used. |

All of them store the DFRobot correction factors ("kvalue", `kLow` / `kHigh`). The three
`salimango_ec_calibration*` sketches use NVS namespace `ec_cal`, which the new firmware reads, so a
calibration saved with them carries over (version 1). `EC_ESP32_DS18B20`, `EC_ESP32_2xDS18B20` and
`salimango_dfrobot_calibration_salinity` use namespace `ec`, which is **not** read: recalibrate after
flashing the new firmware if one of those was used last. These factors are software corrections around
1.0, not the cell constant.

None of these sketches converts conductivity to salinity. The new firmware uses PSS-78 from the
conductivity at the measured temperature. With K = 1 the usable range ends near 18 mS/cm
(≈ 11 ppt at 25 °C); above that the reading is reported as over range, never as a number.
