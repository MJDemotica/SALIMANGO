<div align="center">

# Solar Automated LoRa-Integrated Mud Crab Aeration Network with Crab Grid Observation

**S.A.L.I.M.A.N.G.O.** — a house-powered, remotely monitored smart aquaculture system for mud crab
fattening in mangrove areas: individual floating cages, continuous water-quality monitoring, automatic
aeration and salinity correction, and a phone-friendly dashboard.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-red.svg)](https://www.espressif.com/)
[![Backend: Supabase](https://img.shields.io/badge/Backend-Supabase-3ecf8e.svg)](https://supabase.com/)
[![Frontend: React](https://img.shields.io/badge/Frontend-React-61dafb.svg)](https://react.dev/)
[![Status: In Development](https://img.shields.io/badge/Status-In%20Development-orange.svg)]()

**Author:** [QPPD](https://www.github.com/qppd)

</div>

> **Current phase (revision 2026-10).** The float node reports **directly over the house Wi-Fi to
> Supabase**, and farmers open a **React** dashboard by scanning the QR sticker on the box. **LoRa is
> deferred** to a future phase (the project title is kept), the **camera is removed**, and Firebase is
> replaced by Supabase. What changed and what is still unverified: [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

---

## Table of Contents

- [About the Project](#about-the-project)
- [Architecture Decisions](#architecture-decisions)
- [System Architecture](#system-architecture)
- [Water Quality Sensing](#water-quality-sensing)
- [Control Modes](#control-modes)
- [Electronics & Hardware](#electronics--hardware)
- [Power System](#power-system)
- [Getting Started](#getting-started)
- [Project Structure](#project-structure)
- [Implementation Status](#implementation-status)
- [Known Limitations](#known-limitations)
- [Roadmap](#roadmap)
- [Contributing](#contributing) · [License](#license) · [Contact](#contact)

---

## About the Project

Mud crab (*Scylla serrata*) fattening is a vital livelihood in Southeast Asian mangrove communities, but
crabs are cannibalistic when crowded or molting, and water quality is usually checked by hand. This
project:

1. Physically separates crabs in individual floating boxes on a pontoon grid.
2. Continuously measures **pH, salinity, water temperature and dissolved oxygen**, and estimates the
   **ammonia toxicity** fraction from pH and temperature.
3. Runs aeration and salinity correction **automatically on the controller**, with a local selector
   switch as the fail-safe.
4. Shows live readings, history and controls on a dashboard opened from a **QR code** — no usernames or
   passwords to remember.

Power comes from the household's existing 24/7 solar installation (220 V AC → 12 V DC on the float).

---

## Architecture Decisions

| Area | Current-phase requirement |
|------|---------------------------|
| Frontend | React.js (Vite + TypeScript) on Vercel |
| Backend | Supabase (PostgreSQL, Realtime, row-level security) |
| Communication | Direct ESP32 Wi-Fi (HTTPS) |
| Separate house bridge | Not required |
| Camera | Removed |
| LoRa | Deferred |
| Website access | QR code or typable code |
| EC probe | DFRobot Analog EC (DFR0300), K = 1 |
| Pin assignments | Referenced against [`perfboard_build_guide.pdf`](perfboard_build_guide.pdf) |
| Calibration | Existing sensor-specific routines reused and persistently integrated |

---

## System Architecture

```
  ┌───────────────────────────────┐        ┌──────────────────────────────┐
  │ React dashboard (Vercel)      │◄──────►│ Supabase                     │
  │ QR sticker / typed code       │Realtime│ PostgreSQL + RLS + Realtime  │
  │ Water Quality · Calibration · │  RPC   │ state, history, commands,    │
  │ Settings & Data (CSV)         │        │ calibration, events          │
  └───────────────────────────────┘        └──────────────▲───────────────┘
                                                          │ HTTPS every 10 s
                                                          │ (house Wi-Fi, 2.4 GHz)
                                           ┌──────────────┴───────────────┐
                                           │ ESP32 float node (perfboard) │
                                           │ sensing · calibration ·      │
                                           │ AUTO/MANUAL/OFF · safety     │
                                           └──┬──────────┬─────────────┬──┘
                     ┌────────────────────────┘          │             └──────────────┐
          ┌──────────┴─────────┐           ┌────────────┴──────────┐   ┌──────────────┴──────┐
          │ pH  (GPIO34)       │           │ Relay module          │   │ M-O-A selector      │
          │ EC K=1 (GPIO35)    │           │ K1/K5 air pumps       │   │ GPIO4 / GPIO21      │
          │ DO  (GPIO36)       │           │ K3/K4 bilge pumps     │   │ local authority     │
          │ DS18B20 (GPIO19)   │           └───────────────────────┘   └─────────────────────┘
          └────────────────────┘
```

Details: [`docs/SYSTEM-ARCHITECTURE.md`](docs/SYSTEM-ARCHITECTURE.md),
[`docs/BLOCK-DIAGRAM.md`](docs/BLOCK-DIAGRAM.md), [`docs/FLOWCHART.md`](docs/FLOWCHART.md).

---

## Water Quality Sensing

| Indicator | Sensor / method | Dashboard |
|-----------|-----------------|-----------|
| pH | E-201-C + PH-4502C, Nernst-scaled 1–3 buffer calibration (4.01 / 6.86 / 9.18) | Value, status, target 7.5–8.5, calibration |
| Salinity | DFRobot DFR0300 (K = 1) conductivity, temperature-compensated; **PSS-78** practical salinity | ppt + conductivity (mS/cm) + EC25; *over range* above ≈ 11 ppt |
| Temperature | DS18B20 | °C; compensates pH, EC/salinity and DO; no calibration |
| Dissolved oxygen | DFRobot SEN0237-A, saturation calibration | mg/L; < 3 critical, < 5 warning |
| Ammonia toxicity | **Emerson et al. (1975)** unionized NH₃ fraction from pH + temperature | % NH₃ (a fraction, not a concentration — no TAN is measured) |

All five are computed on the ESP32 and stored with an explicit status, so a failed sensor is never
shown as 0. Calibration: [`docs/CALIBRATION.md`](docs/CALIBRATION.md).

---

## Control Modes

| Mode | Pumps | Notes |
|------|-------|-------|
| **AUTO** (default) | Low DO → both air pumps (K1, then K5 5 s later); low salinity → both bilge pumps (K3, then K4 2 s later) with hysteresis, max run, rest and lockout | Runs entirely on the ESP32 |
| **MANUAL** | Switched one by one from the dashboard | Returns to AUTO after 5 min without a command |
| **OFF** | All off | For servicing; requires confirmation |

- The **M-O-A selector** on the box decides at M and O; at A the dashboard chooses.
- A **valid DO below 3 mg/L** forces both air pumps on in **every** mode, including OFF.
- Boot, reset and Wi-Fi loss never switch aeration off. The float keeps controlling without the network.
- The dashboard shows the **requested** and the **confirmed** mode separately, and every command's
  outcome (done / refused with a reason / no answer).

Rules: [`docs/FIRMWARE.md`](docs/FIRMWARE.md) §2. Salinity rule source: [`salinity_rev.md`](salinity_rev.md).

---

## Electronics & Hardware

| Component | Specification | Purpose |
|-----------|---------------|---------|
| ESP32 DevKit 38-pin (ESP32-WROOM-32) on the perfboard | Wi-Fi, ADC1 analog inputs | Sensing, control, reporting |
| DFRobot DFR0300 EC | **K = 1**, 0–20 mS/cm (≈ 18 mS/cm usable on the ESP32) | Conductivity → salinity |
| E-201-C + PH-4502C | 5 V board through a 10 kΩ / 20 kΩ divider | pH |
| DFRobot SEN0237-A | Galvanic DO probe, one unit | Dissolved oxygen |
| DS18B20 | Waterproof, 1-Wire | Temperature |
| Relay module | 8-channel, 5 V, low-level trigger; K1, K5, K3, K4 used (K2 defective) | Pump switching |
| 2 × RESUN MPQ-03 air pumps | 12 V, 35 W, 68 L/min open flow | Aeration |
| 2 × 12 V bilge pumps | 1100 GPH | Circulation / controlled salinity correction |
| INScom LW28-20A selector | M-O-A | Local mode authority |

Pin map and wiring: [`docs/WIRING.md`](docs/WIRING.md) (from `perfboard_build_guide.pdf`).
Specifications and rationale: [`docs/Components.md`](docs/Components.md). Prices: [`docs/BOM.md`](docs/BOM.md).

Salinity correction in open slotted cages is not guaranteed (tidal exchange and dense brine defeat
dosing); it is a controlled experiment in an isolated or closed-loop volume.

---

## Power System

| Component | Specification |
|-----------|---------------|
| AC source | 220 V AC from the household solar system (array + > 600 Ah bank + inverter), 24/7 |
| AC protection | Shore-end 30 mA RCD + 2-pole breaker, outdoor-rated earthed cable, IP67/IP68 terminations; licensed electrician |
| On-site DC | 12 V 30 A PSU → pumps through relays and branch fuses; 12 V → 5 V buck for the ESP32 and relay module |

No on-site battery: a power cut stops aeration. The dashboard shows it as stale data and a `boot` event
when power returns. See [`docs/BOM.md`](docs/BOM.md) for the power budget.

---

## Getting Started

1. **Supabase:** create a project, run [`supabase/migrations/20261026000000_salimango_initial.sql`](supabase/migrations/20261026000000_salimango_initial.sql), enable anonymous sign-ins.
2. **Vercel:** import the repo with root directory `web`, set `VITE_SUPABASE_URL` and `VITE_SUPABASE_ANON_KEY`.
3. **Provision** each float node: `cd web && npm install && npm run provision -- --name "Float node 1" --url https://<your-site>.vercel.app --sql` → QR sticker, access code, device token.
4. **Flash the ESP32:** [`firmware/README.md`](firmware/README.md).
5. **Calibrate** pH, EC and DO before trusting any reading.

Full guide: [`docs/SETUP.md`](docs/SETUP.md).

---

## Project Structure

```
salimango/
├── README.md                     # This file
├── perfboard_build_guide.pdf     # Authoritative perfboard pin map and wiring
├── salinity_rev.md               # Bilge / salinity-correction rule (source of the AUTO logic)
├── PRD.md                        # Revision requirements
├── firmware/
│   ├── README.md                 # Upload guide
│   ├── salimango_node/           # The ESP32 program (config.h = the only pin map)
│   └── tests/host/               # Host unit tests for formulas and control rules
├── web/                          # React dashboard (Vite + TypeScript)
│   ├── src/                      # App, components, hooks, lib
│   ├── scripts/                  # provision-device.mjs, simulate-device.mjs
│   └── tests/                    # Database (PGlite), library and UI tests
├── supabase/migrations/          # Database schema, RLS, functions
├── docs/                         # Builder and thesis documentation, CHANGELOG
├── DOSENSE/, ph sense/, salinity sense/   # Earlier bench sketches (reference; see each README)
├── model/                        # Fusion 360 model and renders
├── wiring/                       # CirKit project (not updated for this revision)
└── LICENSE
```

---

## Implementation Status

| Item | Status |
|------|--------|
| Supabase schema, RLS, device/browser RPCs | Implemented and tested (PGlite) — not yet run on a live project |
| React dashboard (two required tabs + Settings & Data, QR/code access, Filipino/English, charts, CSV) | Implemented and tested (build, lint, jsdom tests) — not yet tried on a live deployment |
| Firmware build, formulas and control rules | Implemented and tested (compiles; host tests) |
| Firmware on the assembled board (sensors, relays, selector, Wi-Fi, Supabase) | Implemented but unverified |
| Physical calibration of pH, EC, DO | Pending (procedures in `docs/CALIBRATION.md`) |
| Relay module 3.3 V logic compatibility | Pending measurement |
| Alternating duty of the two air pumps | Not implemented (both run together, per the team) |
| LoRa link and house bridge | Deferred |
| Camera | Removed |

---

## Known Limitations

| Limitation | Consequence |
|-----------|-------------|
| K = 1 EC probe reads only to ≈ 18 mS/cm (≈ 11 ppt) | The 28–32 ppt target band shows *over range*; the bilge treats it as "not low" |
| Shared sticker code | Anyone who sees the sticker can view and control the pond until the code is rotated |
| One DO probe | Single point of failure; cross-check with the DO9100 meter |
| No measured TAN | Ammonia is shown as a toxic fraction, not a concentration |
| Wi-Fi must reach the float | Otherwise the node runs locally and the dashboard shows stale data |
| Emerson (1975) is a fresh-water equilibrium | Slight overestimate of the NH₃ fraction in brackish water |

---

## Roadmap

- [x] Concept, component selection, BOM
- [x] Perfboard build guide
- [x] Firmware, Supabase backend and React dashboard (revision 2026-10)
- [ ] Deploy Supabase + Vercel; provision and print the sticker
- [ ] Bench tests on the assembled board ([`docs/TESTING.md`](docs/TESTING.md))
- [ ] Sensor calibration and cross-checks
- [ ] Licensed-electrician commissioning of the AC drop
- [ ] Field pilot without crabs, then stocked deployment
- [ ] Future phase: LoRa link for sites beyond Wi-Fi range

---

## Contributing

Contributions are welcome. Fork, create a feature branch, commit, and open a Pull Request.

## License

Distributed under the MIT License. See [`LICENSE`](LICENSE).

## Contact

**QPPD** — [github.com/qppd](https://www.github.com/qppd) ·
Project: [github.com/qppd/crab-grid-smart-aeration](https://github.com/qppd/crab-grid-smart-aeration)
