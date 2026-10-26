# Components List

**Project:** Solar Automated LoRa-Integrated Mud Crab Aeration Network with Crab Grid Observation

> **Current phase (Oct 2026 revision):** ESP32 → house Wi-Fi → Supabase → React dashboard. The camera is removed, LoRa and the house bridge are deferred, and the EC probe is the DFRobot DFR0300 with **K = 1**. Rows changed for this revision are marked *(rev. 2026-10)*; see [`CHANGELOG.md`](CHANGELOG.md).

> Every count and specification below was **validated against web sources on Sept 2026** — SEAFDEC/AQD (Philippines) mud crab culture standards, FAO mud crab manual, DFRobot sensor datasheets, and Shopee/Lazada PH listings. Rationale for each correction is stated inline; see [`BOM.md`](BOM.md) for the verified product links.

---

## Physical Structure

| Component | Spec / Count | Validated? | Purpose / Notes |
|-----------|--------------|-----------|-----------------|
| Floating Cages | **8 × individual crab fattening boxes** (plastic, with slots/holes for water exchange) | Correct count | 1 crab per box = anti-cannibalism, matching commercial practice (individual boxes/cages are the standard mud crab fattening method). Mud crabs tolerate crowding **in water** — the constraint is separation, not volume, so 8 units is adequate for a 500 sqm prototype. |
| Cage size (target) | ≥ 45 × 40 × 35 cm water-filled, e.g. Allied box (457 × 406 × 406 mm) | Sized | Holds a 0.5–1.5 kg fattening crab without contact with hot plastic walls; FAO/industry boxes range from 31 × 26 × 10 cm up to 458 × 406 × 354 mm. |
| Floaters | **Ready-made foam-filled pontoon floats** (HDPE/LLDPE shell, one-piece foam-filled) — **8–12 units**, NOT hollow PVC pipes | **Corrected** | Size pontoons from the measured dry above-water load, not from the weight of water inside a slotted submerged cage. Water in a slotted cage is continuous with the estuary and is not a dead load while submerged; the crab and plastic have their own buoyancy. Use manufacturer-rated floats (target ≥50 kg each), verify the actual box/frame dry mass and load distribution, then apply at least a 2:1 safety factor. A sealed hollow 4″ PVC pipe gives only ~2.6 kg/m and is not primary flotation. The pipe grid is **conduit only, never flotation** — water-filled pipe adds ~1 kg per litre and must be included in the dry/wet load test. |
| Cage frames / gantry | UV-stabilized PVC/FRP preferred; bamboo or marine plywood only with marine-grade sealing | **Material correction** | Raw bamboo and ordinary plywood rot, split, and attract marine borers in a warm brackish mangrove. If wood is retained, seal all faces and end grain with marine epoxy/PU, keep sacrificial contact details replaceable, and specify Grade 316 stainless or nylon fasteners. |
| Air / water routing | **The PVC pipe grid is the distribution network**: a separate air header (drop tube + stone per cage) and a water header fed by the bilge pumps (valved outlet per cage); flexible hose runs pump→cage as an option/backup | **Never one lumen for both** — water floods an air line and kills aeration, while air pockets choke water flow. Keep the air circuit above the waterline or add a drip loop. A 25 mm (1") header with 8 × 6–8 mm outlets balances flow; split the grid in two, one half per bilge pump. Add flush ports — salt and algae will settle in the runs. |
| Sensor hub | Probe cluster **centred on the grid**, holding EC/pH/DO/2× DS18B20 at mid-depth (~20–30 cm below the surface) | Centred = balanced float load and symmetric cable runs. Mid-depth readings are representative: the surface is skewed by aeration and sun, the bottom by sludge and salinity stratification. Keep it clear of the aerator plume, provide gentle flow across the DO membrane before readings, and make it removable for calibration. One DO probe remains a single-point risk. |
| Water circulation | Air-driven through the pipe grid, plus controlled circulation/flushing through the water header | **Scope correction** | Bubbling creates movement inside each cage; the valved outlets let the circulation pumps flush or mix a test volume. Reliable salinity correction is not claimed for open slotted cages because tidal flushing and dense brine sinking defeat dosing. Use temporary isolation sleeves or a closed-loop/RAS test if salinity control is an objective. |

## Electronics & Hardware

| Component | Spec / Count | Validated? | Purpose / Notes |
|-----------|--------------|-----------|-----------------|
| Controller *(rev. 2026-10)* | **ESP32 DevKit (38-pin, ESP32-WROOM-32)** on the perfboard (`perfboard_build_guide.pdf`), centred in the setup, IP65 enclosure | OK | Runs all sensing, calibration and control locally and reports to Supabase over the **house Wi-Fi** (2.4 GHz). The perfboard keeps an empty E22-900T22D socket on GPIO16/17 for a future LoRa phase. Check the Wi-Fi signal at the float before deployment. |
| Water Quality Sensors *(rev. 2026-10)* | 4 sensor types: EC/salinity (**DFR0300, K = 1**), pH, dissolved oxygen (**one probe**), temperature (DS18B20 on one 1-Wire bus; a second probe is optional) | Validated ranges with limits | **EC K = 1 (team-confirmed probe):** DFRobot range 0–20 mS/cm; on the ESP32 ADC reliable to about 18 mS/cm ≈ **11 ppt at 25 °C**. The 28–32 ppt target (≈ 43–50 mS/cm) is **above this probe's range**: such readings are reported as *over range*, not as a number. Salinity uses PSS-78 from conductivity and temperature, not a fixed factor (35 ppt ≈ 53 mS/cm at 25 °C). **pH E-201-C** covers the 7.5–8.5 brackish target. **DO galvanic probe (SEN0237-A)** supports the ≥5 ppm target, but one probe is a single-point failure; provide flow across the membrane, a handheld backup, and spare membrane/electrolyte. DS18B20 covers the 27–30 °C band. |
| LoRa ⇄ WiFi Bridge — **deferred (rev. 2026-10)** | **A second ESP32 DevKit + E22-900T22D (915 MHz) at the house** where WiFi/internet exists — the "bridge node" | **Not used in the current phase**; kept as the option for sites where the house Wi-Fi does not reach the float. Previous design: replaced the LoRaWAN gateway (Sept 2026) | The ₱10,645 LoRaWAN gateway is gone. The bridge would receive float uplinks and push them to the backend (Firebase in the old design; Supabase now); queued commands are pulled from the webapp and sent back down over LoRa — plain point-to-point radio, no TTN/ChirpStack server. E22-900T22D delivers +22 dBm on the 915 MHz SRD band (~5 km range). The 1–3 km LOS estimate is a test target, not a regulatory guarantee; confirm NTC type-approval and measured EIRP before deployment. |
| Aerator | **2 × RESUN MPQ-903 (MPQ-03) 12V DC electromagnetic air pump — 35W, 68 L/min open-flow, 0.068 MPa, ~3.5A, 6 outlets** (NOT 2× mini aquarium pumps) | **Corrected** | Mini pumps (~0.5–2 L/min each) cannot aerate 8 cages. The biological target is 1.5–2 L/min per 100 L, so 8 cages × ~40 L ≈ 320 L needs about **4.8–6.4 L/min delivered total** (roughly 0.6–0.8 L/min per cage). The 68 L/min MPQ-03 is deliberately far above the target to cover depth, tubing and stone losses; the earlier plan was one unit on duty with the second on N+1 standby (alternating duty). *(rev. 2026-10)* The current firmware runs **both** pumps when DO is low (K1, then K5 5 s later; off together), per the team; alternating duty is not implemented. Use a manifold, needle valves and per-branch flow measurement so excess air is not dumped into the cages. |
| Air stones / tubing | 8–10 × air stones + 20 m 4 mm silicone tubing + gang valve manifold | Added | Distributes air from the 2 pumps into each of the 8 cages. |
| Circulation / Flush Pump | **2 × 12V Marine Bilge Pump 1100 GPH** (submersible) | **Function corrected** | Provides circulation/flushing and can support a controlled salinity experiment. It does not reliably correct salinity in open slotted cages because tidal exchange flushes the dose and dense brine sinks. Use isolation sleeves or a closed-loop/RAS enclosure for a defensible salinity-control test; verify pump current against the relay and fuse rating. |
| Relay Module *(rev. 2026-10)* | **8-channel**, optocoupler, low-level trigger; the module in use is **5 V (SRD-05VDC)** (BOM listing says 12 V coil) | Chosen | 2 air pumps + 2 bilge pumps = 4 loads. Channels used: **K1** air pump 1, **K5** air pump 2 (K2 has a defective terminal; its lead moved to IN5), **K3** bilge 1, **K4** bilge 2. Verify each relay is fully off with a 3.3 V input ([`WIRING.md`](WIRING.md) §6). |
| Camera — **removed (rev. 2026-10)** | ESP32-CAM-MB overhead camera | Not pursued | Removed from the current design (no firmware, dashboard page or power budget). Previous purpose: monitor molting, deter predators, spot poachers. |
| Control Interface *(rev. 2026-10)* | **Local M-O-A rotary selector (INScom LW28-20A)** + onboard status LED, plus dashboard control | The selector is the local authority for a life-support load: at M or O it overrides the dashboard; at A the dashboard chooses AUTO / MANUAL / OFF. Mode and relay states are reported every 10 s. |
| Dashboard / Webapp *(rev. 2026-10)* | **React (Vite) on Vercel + Supabase** (PostgreSQL, Realtime, anonymous Auth) | Replaces Next.js + Firebase. Access by QR sticker or typed code (no login). Live readings, AUTO/MANUAL/OFF, pumps, calibration, salinity settings, charts, CSV export ([`APP.md`](APP.md)). |
| Power Source | **220V AC drop from the household solar system** (array + battery bank **> 600 Ah** + inverter, available 24/7) + **12V 30 A DC supply** on the float | **Corrected** | The site already sits on a household solar installation with a battery bank over 600 Ah, so panels, MPPT and an on-site battery are unnecessary. Night aeration is a planning estimate of 1 × MPQ-03 (35 W) on duty + node 1.5 W + bilge duty ≈ 40–60 W ≈ **480–720 Wh/night ≈ 3.5–5 A at 12V**; verify inverter efficiency and actual duty cycle. On-site work is only a shore-end 30 mA RCD + earthed outdoor AC run + 12V 30 A PSU (see BOM §4), installed and commissioned by a licensed electrician. With no on-site battery, the AC drop is the single point of failure; a power loss shows on the dashboard as stale data and a `boot` event when power returns *(rev. 2026-10: there is no house bridge to alarm)*. |

### Controller pin map — ESP32 38-pin DevKit *(rev. 2026-10)*

The authoritative pin map is [`perfboard_build_guide.pdf`](../perfboard_build_guide.pdf), reproduced
with wire numbers and suitability checks in [`WIRING.md`](WIRING.md) §3–§5 and defined in firmware only
in `firmware/salimango_node/config.h`.

| Function | GPIO |
|----------|------|
| pH (via 10 kΩ / 20 kΩ divider as built; the guide lists 18 kΩ) | 34 (ADC1) |
| EC (DFR0300, K = 1) | 35 (ADC1) |
| DO (SEN0237-A) | 36 / VP (ADC1) |
| DS18B20 1-Wire (4.7 kΩ pull-up) | 19 |
| Relay K1 air pump 1 / K5 air pump 2 / K3 bilge 1 / K4 bilge 2 | 25 / 27 / 33 / 13 (10 kΩ pull-ups) |
| Selector M / A (10 kΩ pull-ups) | 4 / 21 |
| Onboard status LED | 2 |
| E22 RXD / TXD (LoRa, deferred, not fitted) | 17 / 16 |

Earlier revisions of this table listed LoRa on Serial2 as "TX 16 / RX 17", a mode LED on GPIO22 and a
selector decoding (AUTO = both HIGH) that does not match the bench-tested switch; see
[`WIRING.md`](WIRING.md) §7 for every resolved discrepancy.

---

## Validated Water-Quality Targets (deployment reference)

From **SEAFDEC/AQD (Philippines)** mud crab culture standards:

| Parameter | Target / Safe Range | Sensor |
|-----------|--------------------|--------|
| Dissolved Oxygen | **≥ 5 ppm** | DFRobot SEN0237-A galvanic DO probe |
| Salinity | **28–32 ppt** (grow-out tolerates 10–34 ppt) | DFRobot DFR0300 EC, **K = 1** (reliable to ≈ 11 ppt on the ESP32; the target band is above its range); PSS-78 conversion. At 25 °C, 35 ppt seawater is approximately 53 mS/cm; do not treat 12.88 mS/cm as 35 ppt. |
| Temperature | **27–30 °C** | DS18B20 stainless waterproof probe (second probe optional) |
| pH | **7.5–8.5** | E-201-C electrode + PH-4502C board |
| NH₃ (unionized) | **Cannot be calculated from pH + temperature alone**. pH + temperature estimate the toxic fraction/risk; absolute NH₃ requires measured Total Ammonia Nitrogen (TAN) or a laboratory test. | Derived fraction only: Emerson et al. (1975), shown on the dashboard as % NH₃ of total ammonia |


## Deployment Corrections and Verification Gates

1. **Buoyancy:** weigh the empty boxes, frame, pumps, electronics and water-retaining pipes; measure the actual float rating and load distribution. Do not use the weight of water inside a slotted submerged cage as a pontoon load. Apply at least a 2:1 safety factor and physically load-test the assembled grid before stocking.
2. **Aeration:** the MPQ-03's 68 L/min rating is open-flow, not guaranteed delivered flow. Measure each branch at operating depth with air stones installed; target approximately 4.8–6.4 L/min total for 320 L and regulate excess flow.
3. **Salinity:** an open slotted cage is hydraulically connected to the estuary. Brine dosing is a controlled experiment only unless the cage is temporarily isolated or the test is closed-loop. Record tide, current, dose, and pre/post salinity at multiple points.
4. **Electrical:** the 220 V AC drop is a licensed-electrician installation. Require shore-end 30 mA RCD protection, weatherproof connectors, drip loops, no submerged joints, conductor/fuse coordination, earthing verification, and a monthly RCD test.
6. **Protocol** *(rev. 2026-10)*: the float reports every 10 s over HTTPS with a per-device token; commands carry an id and a 90 s expiry and are acknowledged accepted/rejected; the DO emergency is handled locally in every mode. (The LoRa frame protocol with CRC-8 and ACK/NAK is deferred with LoRa; see [`FIRMWARE.md`](FIRMWARE.md).)
7. **Wi-Fi coverage** *(rev. 2026-10)*: measure the house Wi-Fi signal at the float position; add an outdoor repeater/access point if needed.
