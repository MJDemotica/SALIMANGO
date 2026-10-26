# Product Requirements Document — S.A.L.I.M.A.N.G.O.

**Solar Automated LoRa-Integrated Mud crab Aeration Network with Crab Grid Observation**

| | |
|---|---|
| **Status** | Draft v0.1 |
| **Date** | 2026-10-26 |
| **Owner** | MJDemotica |
| **Detailed specs** | [`README.md`](README.md), [`docs/`](docs/) — this PRD states *what* and *why*; the docs hold the engineering detail |

---

## 1. Problem

Mud crab (*Scylla serrata*) fattening is an important livelihood in Philippine mangrove communities. Three problems cause most of the losses:

1. **Cannibalism.** Crabs kill each other when crowded or while molting.
2. **Reactive water management.** Farmers check dissolved oxygen (DO), salinity, pH and temperature by hand, if at all, so they usually find a DO crash or rain dilution only after the crabs have died.
3. **No infrastructure at the pond.** There is no grid power and no Wi-Fi at the pond. The only power nearby comes from the household solar system at the farmer's house, so typical cloud/Wi-Fi IoT products don't work there.

## 2. Product Vision

A floating cage grid that keeps crabs apart, watches the water around the clock and **keeps the crabs alive without internet, cloud or an operator**. When the house link is up, the farmer can monitor and control it remotely.

## 3. Goals and Non-Goals

### Goals

| # | Goal | Measure |
|---|------|---------|
| G1 | Prevent cannibalism | One crab per box, 8 boxes per grid |
| G2 | Continuous water-quality sensing | DO, pH, EC/salinity and 2× temperature reported at least every 60 s |
| G3 | Automated life support | Aeration forced ON within one sample cycle when DO falls below 3 ppm, in any mode |
| G4 | Remote monitoring and control | Dashboard shows new values ≤ 60 s after the uplink; a command takes effect on the next uplink cycle and is ACKed |
| G5 | Fail-safe operation | Boot, reset, link loss or a forgotten MANUAL setting always ends in AUTO with aeration ON |
| G6 | Loss detection | The house bridge alarms locally within 90 s of a missed heartbeat, even with the internet down |
| G7 | Visual observation | The farmer can see the grid on a phone during pond visits (molting, predators, poachers) |
| G8 | Affordable | Full build within ≈ ₱53k–63k ([`docs/BOM.md`](docs/BOM.md)) |

### Non-Goals (v1)

- **Guaranteed open-water salinity correction.** Tidal flushing and sinking brine defeat dosing in slotted cages. Salinity correction is only a *controlled test* in an isolated or closed-loop volume.
- **Absolute ammonia (NH₃) measurement.** Only a pH/temperature-derived toxicity-*risk* indicator. Absolute NH₃ needs TAN or a lab test.
- **Video over LoRa** or remote camera streaming. The camera is reachable only through its local Wi-Fi AP.
- **LoRaWAN / TTN / ChirpStack.** The link is point-to-point only.
- **On-site battery backup** (optional later; see §9).
- **Multi-grid / multi-farm fleet management** beyond a basic device registry.
- **Email/SMS alert delivery** (planned later; v1 uses in-app banners and the bridge's local alarm).

## 4. Users

| Persona | Needs |
|---------|-------|
| **Farmer (operator)** | Glanceable water status, trust that the crabs are safe overnight, a simple way to run pumps for maintenance/harvest, an alarm when something is wrong |
| **Admin / researcher (thesis team)** | Calibration tracking, threshold tuning, history and CSV export for analysis, device/user management |
| **Licensed electrician** | A clearly specified mains drop and protection scheme to commission and sign off |

## 5. System Overview

```
Next.js dashboard (Vercel) ⇄ Firebase RTDB + Auth
            ⇅ home Wi-Fi
House bridge node (ESP32 + E22-900T22D)  ── local alarm
            ⇅ LoRa 915 MHz P2P
Float node (ESP32 + E22-900T22D) ── sensors · 8-ch relays · air & bilge pumps
            ⇡ 220 V AC from house solar → RCD → 12 V 30 A PSU
ESP32-CAM-MB (Wi-Fi AP only, phone during pond visits)
```

Full diagrams: [`docs/SYSTEM-ARCHITECTURE.md`](docs/SYSTEM-ARCHITECTURE.md), [`docs/BLOCK-DIAGRAM.md`](docs/BLOCK-DIAGRAM.md), [`docs/FLOWCHART.md`](docs/FLOWCHART.md).

## 6. Functional Requirements

Priority: **P0** = required for stocked deployment, **P1** = required for thesis demo, **P2** = nice to have.

### 6.1 Float Node — Sensing

| ID | Requirement | Pri |
|----|-------------|-----|
| FR-S1 | Measure DO (SEN0237-A galvanic probe) with temperature compensation | P0 |
| FR-S2 | Measure pH (E-201-C + PH-4502C) through a 5 V→3.3 V divider or ADS1115, temperature-compensated | P0 |
| FR-S3 | Measure EC and convert it to salinity using PSS-78 / manufacturer conversion, not a fixed factor | P0 |
| FR-S4 | Read 2× DS18B20 on one 1-Wire bus, rejecting −127 / 85.0 / out-of-range values | P0 |
| FR-S5 | Sample every 1 s and report a 15 s average to suppress ADC noise | P1 |
| FR-S6 | Set a **sensor-fault** flag for physically impossible readings or an unplugged probe, within one heartbeat | P0 |
| FR-S7 | Persist calibration constants in NVS so a reboot never loses calibration | P0 |
| FR-S8 | Use ADC1 pins only for analog inputs (34/35/36); keep Wi-Fi off on the float | P0 |
| FR-S9 | Derive an NH₃ toxicity-risk indicator from pH + temperature | P2 |

### 6.2 Float Node — Control

| ID | Requirement | Pri |
|----|-------------|-----|
| FR-C1 | Support **AUTO** (default), **MANUAL** and **OFF** modes | P0 |
| FR-C2 | Boot, reset or link loss → AUTO with aeration ON | P0 |
| FR-C3 | MANUAL auto-reverts to AUTO after `manualRevertMin` (default 5 min, range 1–30) without a fresh command | P0 |
| FR-C4 | DO < `doEmergency` (default 3.0 ppm) overrides every mode: aeration ON, local alarm, immediate emergency uplink | P0 |
| FR-C5 | Reject any command that would turn aeration fully off while DO < `doEmergency` (NAK) | P0 |
| FR-C6 | In AUTO, run aerators to hold DO ≥ `doTarget`; alternate the two air pumps for N+1 redundancy | P0 |
| FR-C7 | In AUTO, run the bilge/flush routine when salinity < `salMin` (test-enclosure function only) | P1 |
| FR-C8 | Read the physical M-O-A rotary selector (GPIO 4/21); the local selector outranks the dashboard | P0 |
| FR-C9 | Drive relay GPIOs to OFF before pin init; no relay chatter at boot | P0 |
| FR-C10 | Clamp every threshold received from the dashboard to its allowed range ([`docs/FIRMWARE.md`](docs/FIRMWARE.md) §9) | P0 |
| FR-C11 | Optional night aeration schedule (`aerationOn` / `aerationOff`) | P2 |

### 6.3 LoRa Link and Protocol

| ID | Requirement | Pri |
|----|-------------|-----|
| FR-L1 | P2P on 915 MHz, SF9 / 125 kHz / CR 4-5, private sync word, the same on every node | P0 |
| FR-L2 | 17-byte uplink with ID, sequence, mode/flags, pump bitmap, 5 scaled readings, CRC-8 | P0 |
| FR-L3 | 8-byte downlink with ID, sequence, mode, pump bitmap, expiry, CRC-8 | P0 |
| FR-L4 | 4-byte ACK/NAK; NAK on wrong ID, bad CRC, expired or interlock-violating commands | P0 |
| FR-L5 | Heartbeat uplink at least every 60 s; mode and pump state echoed in every frame | P0 |
| FR-L6 | Config downlink to carry threshold updates | P1 |
| FR-L7 | TX power stays within the NTC SRD EIRP limit | P0 |

### 6.4 House Bridge Node

| ID | Requirement | Pri |
|----|-------------|-----|
| FR-B1 | Verify each uplink (ID + CRC) and write it to `devices/<id>/state`; append to trimmed `logs/` | P0 |
| FR-B2 | Poll `commands/<id>/queue` each cycle, relay fresh commands, mark them `sent`/`acked`/`naked`/`expired` | P0 |
| FR-B3 | **Local alarm** (LED + buzzer) and RTDB alert after 90 s without an uplink, independent of the internet | P0 |
| FR-B4 | Reconnect to Wi-Fi/Firebase on its own after outages without losing LoRa reception | P1 |

### 6.5 Dashboard Webapp

| ID | Requirement | Pri |
|----|-------------|-----|
| FR-D1 | Firebase Auth login; `farmer` and `admin` roles; Auth-only database rules | P0 |
| FR-D2 | Live cards for DO, pH, salinity/EC, temp ×2, mode, pumps, RSSI/SNR, last seen; **STALE** banner when `lastSeen` > 90 s | P0 |
| FR-D3 | Mode selector with confirmations; OFF requires a typed confirmation | P0 |
| FR-D4 | Per-pump overrides, enabled only in MANUAL; show the ACK/NAK result, expiry and MANUAL revert countdown | P0 |
| FR-D5 | Threshold editor with range validation ([`docs/APP.md`](docs/APP.md) §6) | P1 |
| FR-D6 | Alerts: DO emergency, threshold breach (2 consecutive uplinks), heartbeat loss, sensor fault, command NAK/expired | P0 |
| FR-D7 | History charts (24 h / 7 d) and CSV export | P1 |
| FR-D8 | Device registry, user management, calibration due-date reminders | P2 |
| FR-D9 | Camera page with instructions for joining the `crabcam-<id>` AP | P2 |
| FR-D10 | Audit trail: every command and threshold edit records `issuedBy` and a timestamp | P1 |

### 6.6 Camera Node

| ID | Requirement | Pri |
|----|-------------|-----|
| FR-V1 | ESP32-CAM-MB (OV2640) overhead view served over its own Wi-Fi AP | P1 |
| FR-V2 | Powered from the 5 V LM2596S rail (≥ 2 A), stable with no brownout resets | P1 |

### 6.7 Structure and Power

| ID | Requirement | Pri |
|----|-------------|-----|
| FR-P1 | 8 shaded, slotted fattening boxes on foam pontoons rated ≥ 50 kg each; PVC is conduit, never flotation | P0 |
| FR-P2 | Separate air and water headers in the PVC grid; regulated airflow of 0.6–0.8 L/min per cage | P0 |
| FR-P3 | 220 V AC drop with a 30 mA RCD + 2-pole breaker at the house end, IP67/68 terminations, no submerged joints | P0 |
| FR-P4 | 12 V 30 A PSU on the float with fused branches sized from *measured* pump current and inrush | P0 |
| FR-P5 | Licensed-electrician commissioning and Philippine Electrical Code review before stocking | P0 |

## 7. Non-Functional Requirements

| Area | Requirement |
|------|-------------|
| **Safety (life support)** | Crab survival never depends on the internet, the cloud, the bridge or the operator. Interlocks are enforced in firmware *and* the UI. |
| **Reliability** | A 7-day unstocked field soak with no unexplained downtime and calibration holding (gate G3) |
| **Latency** | Telemetry ≤ 60 s to the dashboard; commands ≤ 1 uplink cycle; DO emergency immediate |
| **Accuracy** | Temp ±0.5 °C · pH ±0.05 after calibration · EC ±2 % of standard · DO ±0.3 ppm vs handheld meter |
| **Range** | Stable link from the pond to the house (target 1–3 km LOS); RSSI/SNR baseline recorded |
| **Power** | Night load ≈ 50–60 W at 12 V (to verify by measurement) |
| **Cost** | Stay on the Firebase Spark and Vercel Hobby free tiers; trim logs to about 7 days |
| **Security** | Service-account key kept server-side only (Vercel env vars), never in client code; authenticated reads/writes only |
| **Environment** | Brackish water, UV, humidity: IP65 enclosure, Grade 316/nylon fasteners, UV-stable frame |
| **Regulatory** | NTC 915 MHz SRD EIRP/type-approval confirmed; PEC-compliant mains installation |
| **Maintainability** | Every field deployment is a tagged git version so known-good firmware can be re-flashed at the pond |

## 8. Release Plan and Acceptance

Releases follow the purchase phases in [`README.md`](README.md#bom--budget) and the test gates in [`docs/TESTING.md`](docs/TESTING.md). No gate advances until the previous one passes.

| Milestone | Scope | Exit criteria |
|-----------|-------|---------------|
| **M1 — Bench** (Phase 1, ~₱2.9k) | 2× ESP32 + E22, DS18B20, relays, one bilge pump, selector; control loop + LoRa link | Gate G1: PWR, SEN, ACT, LNK, CTL tests pass |
| **M2 — Remote dashboard** (Phase 2, ~₱7.1–7.7k) | Bridge node, EC sensor, Firebase + Next.js | NET-01…NET-05 pass |
| **M3 — Power to the float** (Phase 3, ~₱3.1–5.0k) | AC drop, RCD, 12 V PSU, 5 V module | PWR-01…PWR-04 pass; electrician sign-off |
| **M4 — Full feature set** (Phase 4, ~₱34.9–46.4k) | DO, pH, camera, boxes, floats, PVC grid, air pumps | Gate G2 (integrated bench) then G3 (7-day soak, no crabs) |
| **M5 — Stocked deployment** | Thesis demo scenarios: rain dilution, DO depletion, link loss | Gate G4: first-aid scenarios repeatable |

**Definition of done for "life-support ready"** (before any crab is stocked): CTL-01…CTL-06, LNK-04, NET-03 and PWR-04 all pass on the final build.

## 9. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|-----------|
| No on-site battery: tripped breaker or cut cable stops aeration | Crab mortality overnight | Bridge heartbeat alarm at 90 s; confirm the inverter stays on 24/7; optional 12 V UPS / supercapacitor |
| Single DO probe | Missed DO crash or false emergency | Probe on the central hub with water movement; handheld DO meter; spare membrane/electrolyte; fallback night aeration schedule |
| Remote control depends on the internet + bridge | No remote control during outages | Float stays autonomous in AUTO; local selector; MANUAL auto-revert |
| Salinity dosing ineffective in open cages | Overclaimed feature | Scoped as a closed-loop test function only (Non-Goal) |
| Sensor drift in brackish water | Wrong decisions | Calibration schedule with dashboard reminders; refractometer cross-check |
| Mains near water | Electrocution | RCD, earthing, IP67/68, no submerged joints, licensed electrician, monthly RCD test |
| Regulatory (NTC EIRP) | Non-compliant radio | Measure EIRP; confirm type-approval before deployment |

## 10. Open Questions / Inconsistencies to Resolve

These come from comparing the docs with the prototype sketches in `DOSENSE/`, `ph sense/` and `salinity sense/`:

1. **EC probe cell constant.** The README/BOM specify DFRobot **K=10 (0–100 mS/cm)**, but the calibration sketches target **DFR0300 K=1** (≈ 0–20 mS/cm). Brackish/sea water (up to ~53 mS/cm) is beyond a K=1 probe's range. Decide which probe is final and update either the sketches or the docs.
2. **Relay module coil voltage.** The docs say a **12 V coil** module; `relay_wifi_test_k1k4.ino` describes a **5 V SRD-05VDC** active-low module. Confirm which part is in the build and which supply rail drives it.
3. **Wi-Fi on the float.** Several bench sketches (DO, relay, EC) use Wi-Fi web pages. That's fine as bench tooling, but the production float firmware must keep Wi-Fi off (FR-S8). Confirm these sketches stay bench-only.
4. **Bilge-pump role in AUTO.** Is low-salinity flushing (FR-C7) enabled in the stocked deployment, or only in the test enclosure?
5. **Heartbeat interval.** Is the 60 s default final, or will a low-power build stretch it to 5–15 min (which changes the 90 s alarm and command latency)?
6. **pH front end.** Resistor divider (minimum) or ADS1115 (precision) for the final build? The three pH sketches (`ph_cali_*`) need consolidating into one.
7. **Alert delivery.** Which channel (email, SMS, push) comes after in-app banners, and does it have to work when the dashboard is closed?
8. **Firmware layout.** `firmware/main_controller`, `house_bridge`, `camera_node`, `shared/` and `dashboard/` don't exist yet. The current sketches should be migrated into that structure.

## 11. References

- [`docs/APP.md`](docs/APP.md) — dashboard spec, RTDB schema, interlocks, threshold ranges
- [`docs/FIRMWARE.md`](docs/FIRMWARE.md) — control rules, packet protocol, timing, config defaults
- [`docs/TESTING.md`](docs/TESTING.md) — test gates and acceptance criteria
- [`docs/WIRING.md`](docs/WIRING.md) — verified pin map and assembly
- [`docs/BOM.md`](docs/BOM.md) — parts, prices, power budget
- [`docs/CALIBRATION.md`](docs/CALIBRATION.md), [`docs/SETUP.md`](docs/SETUP.md), [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md), [`docs/STACKS.md`](docs/STACKS.md)
- `Solar Automated LoRa-Integrated Mud crab Aeration Network with Crab Grid Observation.pdf` — thesis document
