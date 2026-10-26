# Testing and Validation Plan

**Project:** Solar Automated LoRa-Integrated Mud Crab Aeration Network with Crab Grid Observation

Two kinds of evidence, kept apart:

- **Software verification** (§1) runs on a PC without the hardware. It shows the code, formulas,
  database rules and screens behave as specified.
- **Hardware validation** (§2–§8) has to be done on the assembled float. **None of it has been carried
  out for this revision.** Never test pumps in a pond with crabs stocked.

## 1. Automated tests (software only)

| Command (from) | What it covers | Result (2026-10-26) |
|----------------|----------------|---------------------|
| `npm test` (`web/`) | **Database** (`tests/db`, PGlite / PostgreSQL 18): migration applies; anon has no table access; secrets stored only as hashes; admin functions unreachable from browsers and devices; device token required; sticker code claim (any case/format) and revocation by rotation; command and settings validation; OFF confirmation; delivery, acks, expiry, supersede; one history row per minute; retention (keep all / window); a withheld status can never carry a value | 18 / 18 passed |
| | **Library** (`tests/lib.test.ts`): translations complete in English and Filipino; stale after 45 s; withheld readings never shown as values; target colouring; timed-out requests; CSV escaping and empty cells; chart gaps; settings validation | 12 / 12 passed |
| | **UI** (`tests/ui.test.tsx`, jsdom): no username/password field; five indicators; fault shown as "Sensor fault"; over-range salinity explained; stale banner with last-known values; SIMULATED banner; OFF confirmation; requested vs confirmed mode; pump buttons disabled outside MANUAL / under selector override; Calibration tab sections, K = 1 shown, no temperature calibration; calibration commands; language switch | 12 / 12 passed |
| `npm run typecheck`, `npm run lint`, `npm run build` (`web/`) | TypeScript, ESLint, production bundle | No errors; bundle contains no service-role key |
| `g++ … test_firmware_logic.cpp` (`firmware/tests/host`, see [`../firmware/README.md`](../firmware/README.md)) | pH conversion, buffers, fit and slope limits; EC K = 1 conversion, standard recognition, correction factor applied once; DO saturation and calibration; PSS-78 (S = 35 at 42.914 mS/cm, 15 °C and at 53.065 mS/cm, 25 °C); Emerson (5.38 % at pH 8, 25 °C; 0.40 % at pH 7, 20 °C); selector decoding; mode rules and 5-min revert; aeration hysteresis, minimum times, emergency; bilge start/recover/rest/timeout/lockout, over/under range, fault; relay staggering 5 s / 2 s | 101 checks, 0 failures |
| `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/salimango_node` | Firmware builds for ESP32 Dev Module, core 3.3.12 | Builds: 88 % flash, 21 % RAM |

These do **not** show that sensors read correctly, that relays switch, that the board is wired as the
guide says, or that the ESP32 reaches Supabase over the real Wi-Fi.

## 2. Test gates (hardware)

| Gate | Scope | Exit criteria |
|------|-------|---------------|
| G0 — Perfboard | Continuity and rails ([`WIRING.md`](WIRING.md) §8) | All checks pass with the ESP32 removed |
| G1 — Bench | Sensors, relays, selector, firmware, Wi-Fi, dashboard (pumps in buckets) | PWR, SEN, ACT, CTL, NET pass |
| G2 — Field pilot (no crabs) | 7-day soak in the pond | No unexplained downtime; calibration holds; Wi-Fi reaches the float |
| G3 — Stocked deployment | Demonstration scenarios | First-aid scenarios repeatable |

## 3. Power (PWR)

| ID | Procedure | Pass criteria |
|----|-----------|---------------|
| PWR-01 | Measure 12 V, 5 V, 3.3 V under pump-start load (both air pumps, 5 s apart) | ±5 %; no brownout reset |
| PWR-02 | Press the shore-end RCD test button | Trips; reset restores supply |
| PWR-03 | Measure each pump's running and inrush current | Within relay and branch-fuse ratings; record for fuse sizing |
| PWR-04 | Kill AC mid-run, restore | Boots, AUTO (selector at A), air pumps on, `boot` event on the dashboard |

## 4. Sensors (SEN)

| ID | Procedure | Pass criteria |
|----|-----------|---------------|
| SEN-01 | DS18B20 vs reference thermometer | ±0.5 °C |
| SEN-02 | pH calibration (6.86 + 9.18, optionally 4.01), then read each buffer | ±0.05 pH |
| SEN-03 | EC calibration (1.413 + 12.88 mS/cm), then read each standard | ±2 %; dashboard shows K = 1 and the new version |
| SEN-03b | Salinity vs refractometer in a sample below 11 ppt | Record the difference (validation, not calibration) |
| SEN-03c | EC in a solution above ≈ 18 mS/cm | Card shows *over range*, no number |
| SEN-04 | DO saturation calibration, then vs DO9100 handheld | ±0.3 mg/L |
| SEN-05 | Readings with Wi-Fi connected vs disconnected | Unchanged (ADC1-only inputs) |
| SEN-06 | Unplug each probe in turn | Card shows *Sensor fault* / *Not detected* (never 0) within one report; event raised |
| SEN-07 | Unplug the DS18B20 | pH/EC/DO marked *approximate*; salinity and NH₃ *not available*; captures refused |
| SEN-08 | Reboot after calibrating | Same version and values reported; history rows carry the cal id |

## 5. Actuators (ACT)

| ID | Procedure | Pass criteria |
|----|-----------|---------------|
| ACT-01 | MANUAL: switch K1, K5, K3, K4 from the dashboard | Correct relay and pump; dashboard shows *Done* and the confirmed state |
| ACT-02 | Measure delivered airflow per cage | 0.6–0.8 L/min per cage after regulation |
| ACT-03 | Bilge pump against a valved outlet | Flow; valve shuts fully |
| ACT-04 | AUTO aeration start | K5 starts 5 s after K1; both stop together |
| ACT-05 | Reset with pumps connected | No chatter before the firmware runs |
| ACT-06 | Relay input held at 3.3 V | Relay fully off (5 V module with 3.3 V logic) |

## 6. Control logic (CTL)

| ID | Procedure | Pass criteria |
|----|-----------|---------------|
| CTL-01 | Boot with the selector at A | AUTO; air pumps on |
| CTL-02 | Selector through M, O, A | Dashboard "Selector on the box" and mode follow (M = MANUAL, O = OFF, A = dashboard's choice) |
| CTL-03 | MANUAL and wait 5 min | Back to AUTO; event |
| CTL-04 | DO below 3 mg/L (low-DO water or test signal) in AUTO, MANUAL and OFF | K1 + K5 forced on in every mode; critical event; dashboard banner |
| CTL-05 | During CTL-04, request K1 off in MANUAL | Rejected with the reason |
| CTL-06 | Dashboard OFF with the selector at A | Confirmation dialog; all off; DO emergency still overrides |
| CTL-07 | Dashboard mode change with the selector at M or O | Buttons disabled / request rejected with the reason |
| CTL-08 | Salinity below salMin (closed test volume, sample < 11 ppt) | Bilge K3 then K4 after 2 s; stops at salMin + salHyst; rests |
| CTL-09 | Salinity that does not recover | Stops after the max run; after the set number of tries → lockout, critical event; *Clear lockout* works |
| CTL-10 | Change salMin on the dashboard | Device reports the new settings version; values used after reboot without Wi-Fi |
| CTL-11 | Calibration started and left | Cancelled after 30 min; event |

## 7. Network end to end (NET)

| ID | Procedure | Pass criteria |
|----|-----------|---------------|
| NET-01 | Report → Supabase → dashboard | New values within one report (≈ 10 s); **Live** |
| NET-02 | Command → device → ack | *Done* or *Refused…* shown within about 15 s |
| NET-03 | Unplug the router for 2 min, then restore | Float keeps controlling; dashboard stale banner; `cloud_restored` event with the outage length; queued events arrive |
| NET-04 | Request while the device is offline | *Not delivered* / *No answer from the device* after expiry, never shown as applied |
| NET-05 | Wi-Fi signal at the float position | RSSI recorded; reliable reporting |
| NET-06 | Rotate the sticker code | Old phones lose access; new code works |
| NET-07 | CSV export | Row count matches the period; empty cells for withheld values |

## 8. Field scenarios

1. **Rain dilution:** in an isolated or closed-loop test volume below the probe's 11 ppt limit, salinity
   falls below salMin → bilge runs → recovers or locks out (open-water correction is not claimed).
2. **DO depletion:** stop aeration in a test bucket until DO approaches 3 mg/L → the emergency forces
   aeration.
3. **Link loss:** switch the router off → the float stays in AUTO and keeps aerating; the dashboard
   shows stale data.

## 9. Test log template

| Date | Test ID | Firmware version / commit | Result | Notes |
|------|---------|---------------------------|--------|-------|
| | | | pass/fail | |

## Deferred: LoRa tests (future phase)

From the previous plan: LNK-01 bench P2P exchange both directions with CRC and ACK; LNK-02 RSSI/SNR at
the pond edge; LNK-03 walk test to the house (1–3 km LOS target); LNK-04 expired or wrong-CRC command
→ NAK; bridge heartbeat-loss alarm after 90 s.
