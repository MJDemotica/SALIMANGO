# Change Log

## Revision 2026-10 — Wi-Fi + Supabase + React, K = 1, perfboard pinout

Status labels: **Implemented and tested** (automated tests or a build ran and passed) · **Implemented
but unverified** (code exists, not yet run on the real hardware or a live service) · **Pending** ·
**Deferred**. "Tested" here always means software tests; no hardware was tested in this revision.

### Mandatory changes

| # | Change | Previous behaviour | New behaviour | Status | Action required |
|---|--------|--------------------|---------------|--------|-----------------|
| 1 | **Firebase replaced with Supabase** | Planned Next.js + Firebase RTDB/Auth (docs only; no code existed) | Supabase schema with RLS, device/browser RPCs, Realtime (`supabase/migrations/20261026000000_salimango_initial.sql`) | Implemented and tested (PGlite, 18 tests); not yet applied to a live project | Create the project, run the migration, enable anonymous sign-ins |
| 2 | **Camera removed** | ESP32-CAM-MB node, AP streaming, camera page, power and BOM entries (docs only) | No camera code, page or schema; docs/BOM mark it removed | Implemented and tested (no camera code in the active tree) | Update the drawio/CirKit images |
| 3 | **LoRa deferred** | Float + house bridge over E22-900T22D, CRC-8 frames, heartbeat alarm (docs only) | Not implemented; E22 socket (GPIO16/17) reserved; `cloud.h` is the transport boundary; previous design kept in "Deferred" sections | Deferred | — |
| 4 | **Direct ESP32 Wi-Fi** | Wi-Fi off on the float; bridge relays | Float joins the house Wi-Fi and calls `device_report` over verified HTTPS every 10 s (2 s when busy), in its own task | Implemented but unverified | Check Wi-Fi coverage at the float; flash and run NET tests |
| 5 | **QR code / typed code access, no login page** | Firebase Email/Password farmer accounts | Sticker QR → `/?code=…` or typed code → anonymous session + `claim_device_access`; the code opens the whole site including control (team decision); revocation by `admin_rotate_access_code` | Implemented and tested (DB + UI tests) | Provision devices and print stickers |
| 6 | **DFRobot Analog EC configured with K = 1** | Docs/BOM: "K = 10, 0–100 mS/cm"; bench sketches used the K = 1 DFR0300 transfer function without naming K | `EC_CELL_CONSTANT_K = 1` in `sensor_math.h` (with a compile-time check), applied once in `ecRawMsCm()`; calibration factors kept separate; docs/BOM corrected | Implemented and tested (host tests); physical calibration pending | Calibrate with 1.413 and 12.88 mS/cm |
| 7 | **Pins reconciled with `perfboard_build_guide.pdf`** | Sketches disagreed with the guide (DO 34, relay 26, pH 35, EC 34, 1-Wire 4); docs had a wrong selector decoding | Single pin map in `config.h` matching the guide; docs updated | Implemented (software/documentation consistency); board not inspected | Run the continuity checklist (`WIRING.md` §8) |

### Change 6 detail — EC cell constant K = 1

| Location | Before | After |
|----------|--------|-------|
| `firmware/salimango_node/sensor_math.h` | — (new) | `EC_CELL_CONSTANT_K = 1.0f`, `static_assert`; RES2 820, ECREF 200, 1.85 %/°C, standards 1.413 / 12.88 mS/cm, correction window 0.5–1.5 (all from `salimango_ec_calibration_wifi`) |
| `sensor_math.cpp` `ecRawMsCm()` | `1000 × mV / 820 / 200` in each bench sketch | `K × 1000 × mV / (820 × 200)` — the only place K is applied |
| `sensors.cpp` EC driver | `kLow` / `kHigh` called "K" | Same NVS keys, documented as DFRobot correction factors ("kvalue"), not the cell constant |
| Report `cal.ec.params.cell_constant_k` | — | `1`, shown on the dashboard's Calibration tab |
| README, Components, BOM #5/#24, CALIBRATION, STACKS, SYSTEM-ARCHITECTURE | "K = 10", "0–100 mS/cm" | K = 1, 0–20 mS/cm (≈ 18 mS/cm / ≈ 11 ppt usable on the ESP32) |

Active-pipeline verification: host tests check the K = 1 conversion, that an ideal K = 1 cell gives a
correction factor of 1.0, and that a correction is applied once. The live pipeline on the real probe is
**unverified**.

### Change 7 detail — GPIO assignments

| Function | Guide (W#) | Firmware (`config.h`) | Discrepancy resolved |
|----------|-----------|-----------------------|----------------------|
| pH | GPIO34 (W22) | 34 | pH sketches used 35 |
| EC | GPIO35 (W19) | 35 | three EC sketches used 34 |
| DO | GPIO36/VP (W17) | 36 | DO sketch used 34 |
| DS18B20 | GPIO19 (W34) | 19 | `EC_ESP32_2xDS18B20` used 4 |
| Relay K1 air 1 | GPIO25 (W9) | 25 | DO sketch relay on 26 |
| Relay K5 air 2 | GPIO27 (W11, J1 IN2) | 27 | K2 defective → lead moved to IN5 |
| Relay K3 bilge 1 | GPIO33 (W13) | 33 | — |
| Relay K4 bilge 2 | GPIO13 (W15) | 13 | — |
| Selector M / A | GPIO4 / 21 (W29 / W31) | 4 / 21 | old WIRING.md decoding corrected to the bench-tested one |
| E22 RXD / TXD | GPIO17 / 16 (W24 / W25) | reserved | old docs "TX 16 / RX 17" |
| pH divider R8 | 18 kΩ | ratio 1.5 (20 kΩ as built) | team-confirmed as-built value; guide unchanged |

Unresolved: 5 V relay module with 3.3 V inputs (measure), CirKit project and images not updated.

### Other changes

| Module | Previous | New | Status |
|--------|----------|-----|--------|
| Firmware `salimango_node/` | Separate bench sketches per sensor | One program: pH (`ph_cali_no_ten_k2` algorithm), EC (`salimango_ec_calibration_wifi`), DO (`DO_ESP32_DS18B20_WiFi4`), selector (`selector_test`), relays; NVS namespaces kept so earlier calibrations load | Implemented and tested (build + 101 host checks); unverified on hardware |
| AUTO aeration | One relay (GPIO26) | K1 + K5, K5 5 s later, off together (team) | Implemented and tested (host) |
| DO emergency | MANUAL override only (docs) | Valid DO < 3 mg/L forces K1 + K5 in every mode including OFF (team) | Implemented and tested (host) |
| AUTO bilge | Not implemented | `salinity_rev.md` state machine; K3 then K4 2 s later; over range = not low/recovered; dashboard-adjustable parameters | Implemented and tested (host, DB) |
| Salinity | Not computed | PSS-78 on the ESP32 from EC at temperature | Implemented and tested (host) |
| Ammonia | Text only ("pH + temperature estimate the fraction"); no formula existed | Emerson et al. (1975) on the ESP32; shown as a fraction | Implemented and tested (host: 5.38 % at pH 8, 25 °C) |
| pH temperature handling | Two variants in the sketches (Nernst scaling vs +0.03 pH/°C) | Nernst scaling (`ph_cali_no_ten_k2`); the additive variant is not used | Implemented and tested (host) |
| pH buffer label | PRD listed 9.16 | **9.18** (team-confirmed; matches the code and BOM) | — |
| Calibration persistence | NVS per sketch | NVS + version + random id; recorded in `calibration_records`; history rows carry the id | Implemented and tested (DB); unverified on hardware |
| Dashboard `web/` | None | React app: Water Quality Sensing, Calibration, Settings & Data; English/Filipino; charts; CSV | Implemented and tested (42 tests, build, lint) |
| Retention | Trim to 7 days (docs) | Keep forever (default) or delete after N days, per device | Implemented and tested (DB) |
| Provisioning / simulator | None | `npm run provision`, `npm run simulate` (always flagged simulated) | Implemented; provision dry-run tested |
| Legacy sketches | — | Unchanged; README added in each folder (superseded, pin differences, credentials) | Implemented |
| Docs | Next.js/Firebase/LoRa/camera design | README, APP, BLOCK-DIAGRAM, CALIBRATION, Components, BOM, FIRMWARE, FLOWCHART, SETUP, STACKS, SYSTEM-ARCHITECTURE, TESTING, TROUBLESHOOTING, WIRING revised; LoRa material kept in "Deferred" sections | Implemented |

### Files

Created: `firmware/salimango_node/*`, `firmware/tests/host/test_firmware_logic.cpp`, `firmware/README.md`,
`supabase/migrations/20261026000000_salimango_initial.sql`, `web/**`, `docs/CHANGELOG.md`,
`DOSENSE/README.md`, `ph sense/README.md`, `salinity sense/README.md`.
Modified: `README.md`, `.gitignore`, the `docs/` files listed above.
Not modified (kept on request until the website is finalised): all bench sketches,
`crab-grid-smart-aeration (2).md` (an older concatenated copy of the previous docs — now out of date),
`docs/CHANGELOG-LORA-MODULE-UPDATE.md`, `SYSTEM FLOWCHART SALIMANGO3.drawio.png`, `wiring/*`, `model/*`,
`Solar Automated LoRa-Integrated Mud crab Aeration Network with Crab Grid Observation.pdf`.
Deleted: none.

### Security notes

- Wi-Fi passwords are hard-coded in `DOSENSE/DO_ESP32_DS18B20_WiFi4`, `DOSENSE/relay_wifi_test_k1k4`
  and `salinity sense/salimango_ec_calibration_wifi` and are in the git history: **change those router
  passwords**.
- `firmware/**/secrets.h` and `web/provisioning/` are git-ignored.

## Earlier changes

- 2026-10-05 — LoRa module E22-900M22S → E22-900T22D (documentation): see
  [`CHANGELOG-LORA-MODULE-UPDATE.md`](CHANGELOG-LORA-MODULE-UPDATE.md). Deferred with LoRa.
