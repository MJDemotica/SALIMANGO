# Wiring and Assembly Guide

**Project:** Solar Automated LoRa-Integrated Mud Crab Aeration Network with Crab Grid Observation
**Current phase:** direct ESP32 Wi-Fi → Supabase; LoRa deferred; camera removed.

This is the physical build reference. The **authoritative source for pins and wiring is
[`perfboard_build_guide.pdf`](../perfboard_build_guide.pdf)** (the board as built matches it). The
firmware's only pin definitions are in
[`firmware/salimango_node/config.h`](../firmware/salimango_node/config.h), which cites the guide's wire
numbers (W1–W34).

> **Verification status:** this document, the firmware and the guide agree with each other. That is
> software/documentation consistency only. The assembled perfboard has not been inspected or tested as
> part of this revision; use the continuity checklist (§8) on the real board.

## 1. Safety gates

1. The 220 V AC drop, RCD/breaker and earthing are installed and commissioned by a **licensed
   electrician** (Philippine Electrical Code). Builders work on 12 V / 5 V DC only.
2. No 12 V or 220 V on the perfboard (guide, *Mga paalala*).
3. Weatherproof everything: IP65/66 enclosures, IP68 glands, drip loops, no submerged joints.
4. Never test pumps in a pond with crabs stocked; use buckets on the bench.
5. The firmware switches the air pumps **ON at boot** (fail-safe). Disconnect the pumps when flashing
   on the bench.

## 2. Board and connectors (from the guide)

Board: dot perfboard 54 × 33. ESP32 DevKit 38-pin (ESP32-WROOM-32) on two 19-pin female headers.

| Connector | Terminals | Connects to |
|-----------|-----------|-------------|
| J1 Relay (5-pin header) | GND, IN1, IN2, IN3, IN4 | Relay module inputs |
| J2 Selector (3P screw) | M, A, GND | INScom LW28-20A M-O-A rotary selector |
| J3 DS18B20 (3P screw) | 3V3, DATA, GND | DS18B20 probe(s) |
| J4 DO (3P screw) | 5V, GND, A | DFRobot SEN0237-A signal board |
| J5 EC (3P screw) | 5V, GND, A | DFRobot DFR0300 (K = 1) signal board |
| J6 pH (3P screw) | V+, G, Po | PH-4502C board |
| E22 socket (7-pin) | M0, M1, RXD, TXD, AUX, VCC, GND | **Not fitted in this phase** (LoRa deferred) |

Rails (bottom side): 5V row 3, GND row 5, 3V3 row 7, 3V3 (B) row 29, GND (B) row 31. C1 470–1000 µF on
the 5V rail. The relay module's VCC comes from the buck converter OUT+, **not** from the perfboard.

## 3. Pin map

| Function | GPIO | Guide wiring | Firmware constant |
|----------|------|--------------|-------------------|
| pH analog (via divider) | **34** | J6 Po → R7 10 kΩ → node (W21); node → GPIO34 (W22); node → R8 → GND | `PIN_PH` |
| EC analog | **35** | J5 A → GPIO35 (W19) | `PIN_EC` |
| DO analog | **36 (VP)** | J4 A → VP (W17) | `PIN_DO` |
| DS18B20 1-Wire | **19** | J3 DATA → R9 4.7 kΩ to 3V3 (B) (W33); R9 → GPIO19 (W34) | `PIN_ONEWIRE` |
| Relay K1 — air pump 1 | **25** | J1 IN1 + R1 10 kΩ pull-up (W8, W9) | `PIN_RELAY_K1` |
| Relay K5 — air pump 2 | **27** | J1 IN2 + R2 10 kΩ pull-up (W10, W11); **relay-side lead moved to module IN5** | `PIN_RELAY_K5` |
| Relay K3 — bilge pump 1 | **33** | J1 IN3 + R3 10 kΩ pull-up (W12, W13) | `PIN_RELAY_K3` |
| Relay K4 — bilge pump 2 | **13** | J1 IN4 + R4 10 kΩ pull-up (W14, W15) | `PIN_RELAY_K4` |
| Selector M | **4** | J2 M + R5 10 kΩ pull-up (W28, W29) | `PIN_SEL_M` |
| Selector A | **21** | J2 A + R6 10 kΩ pull-up (W30, W31) | `PIN_SEL_A` |
| Status LED | 2 | DevKit onboard LED (no perfboard wire) | `PIN_STATUS_LED` |
| E22 RXD / TXD | 17 / 16 | W24 / W25 | Reserved, unused (LoRa deferred) |

**Relay K2:** the module's K2 channel has a defective terminal. The perfboard is unchanged; the lead
from J1 IN2 (GPIO27) is moved to the module's **IN5**, so GPIO27 drives **K5**. K2 is not used.

### Resistors

| Ref | Value (guide) | Function |
|-----|---------------|----------|
| R1–R4 | 10 kΩ | Pull-ups to 3V3 on relay inputs (relays OFF through reset) |
| R5, R6 | 10 kΩ | Selector pull-ups to 3V3 (B) |
| R7 | 10 kΩ | pH divider, series |
| R8 | **18 kΩ in the guide; 20 kΩ as built** | pH divider, to GND |
| R9 | 4.7 kΩ | 1-Wire pull-up |

The firmware uses the as-built 20 kΩ: divider ratio (10 + 20) / 20 = **1.5**
(`PH_DIVIDER_BOTTOM_OHMS` in `config.h`). With the guide's 18 kΩ the ratio would be 1.556. Recalibrate pH
whenever R8 changes.

## 4. Mode selector

INScom LW28-20A, contacts close to GND, external 10 kΩ pull-ups. Decoding was confirmed by the bench
beep test in [`DOSENSE/selector_test`](../DOSENSE/selector_test/selector_test.ino):

| Position | GPIO4 (M) | GPIO21 (A) | Firmware |
|----------|-----------|------------|----------|
| M | LOW | HIGH | MANUAL |
| O | HIGH | HIGH | OFF |
| A | HIGH | LOW | AUTO (dashboard may choose AUTO / MANUAL / OFF) |
| — | LOW | LOW | FAULT → treated as A, event raised |

A broken selector cable leaves both inputs HIGH, which reads as **O (OFF)**. Even then the DO
emergency still forces the air pumps on.

## 5. GPIO suitability (ESP32-WROOM-32)

| GPIO | Use | Check |
|------|-----|-------|
| 34, 35, 36 | Analog in | ADC1, input-only: correct for analog; keeps working with Wi-Fi on (ADC2 does not) |
| 19 | 1-Wire | General I/O, not strapping |
| 25, 27, 13 | Relay out | ADC2 pins used only as digital outputs (Wi-Fi blocks ADC2 *analog* reads only); not strapping |
| 33 | Relay out | General I/O, not strapping |
| 4, 21 | Selector in | Digital inputs with external pull-ups; not strapping pins on the ESP32-WROOM-32 |
| 2 | Onboard LED | Strapping pin, driven only by the DevKit LED after boot |
| 16, 17 | E22 UART | Free, unused this phase |
| 0, 5, 12, 15 | — | Strapping pins, not used |
| 1, 3 | — | UART0 (USB serial), not used |
| 6–11 | — | Internal flash, not exposed |

No pin is used twice. No interface in this design uses I²C or SPI.

## 6. Voltage and logic levels

| Item | Assessment | Status |
|------|-----------|--------|
| PH-4502C (5 V) → GPIO34 | 5 V × 20/30 = 3.33 V at the pin (3.21 V with 18 kΩ). The ADC saturates near 3.1 V; the firmware reports `over_range` above 3.1 V | OK by calculation; the extreme acid end is lost |
| DFR0300 EC board → GPIO35 | Output 0–3.4 V direct; above ~3.0 V the firmware reports `over_range` | OK by datasheet; within the ESP32 absolute maximum (3.6 V) |
| SEN0237-A DO board → GPIO36 | Output 0–3.0 V direct | OK by datasheet |
| DS18B20 | Powered from 3V3 with 4.7 kΩ to 3V3 | OK |
| Relay module (5 V SRD-05VDC, low-level trigger) ← 3.3 V GPIO | Optocoupler inputs referenced to the module's 5 V. A 3.3 V HIGH leaves ~1.7 V across the input LED, which on some modules keeps a relay partly on | **Measure on the real module:** with IN at 3.3 V the relay must be fully off. The bench relay test reportedly works; record the measurement |
| Relay coils | 5 V from the buck converter (guide), not the perfboard | Check the buck current rating for the energised coils |

The BOM lists a 12 V-coil relay module; the bench sketch and the team describe a **5 V** module
(SRD-05VDC). The 5 V module is assumed here.

## 7. Resolved and unresolved discrepancies

| # | Item | Previous source | Resolution |
|---|------|-----------------|------------|
| 1 | DO input | `DO_ESP32_DS18B20_WiFi4`: GPIO34 | **GPIO36** (guide W17) |
| 2 | DO relay | Same sketch: GPIO26 | **GPIO25** = K1 (guide W9) |
| 3 | pH input | All pH sketches: GPIO35 | **GPIO34** (guide W22) |
| 4 | EC input | Three EC sketches: GPIO34 | **GPIO35** (guide W19) |
| 5 | 1-Wire | `EC_ESP32_2xDS18B20`: GPIO4 (conflicts with selector M) | **GPIO19** (guide W34) |
| 6 | Selector decoding | Old WIRING.md: AUTO = both HIGH, OFF = both LOW | **Bench-tested decoding** (§4) |
| 7 | pH divider R8 | Guide 18 kΩ | **As built 20 kΩ** (team-confirmed); firmware ratio 1.5 |
| 8 | Relay channel 2 | K2 defective | Lead moved to **IN5**; firmware calls it K5 on GPIO27 |
| 9 | LoRa UART | Old docs: GPIO16 TX / 17 RX "Serial2" | Guide: GPIO17 → E22 RXD, GPIO16 ← E22 TXD; unused this phase |
| 10 | Mode/link LEDs | Old docs: external mode LED on GPIO22 | Not on the perfboard; only the onboard LED (GPIO2) is used |
| 11 | Relay module coil | BOM: 12 V coil; bench sketch: 5 V | 5 V module; **logic-level compatibility to be measured** (§6) |

**Unresolved:** #11 (measurement), and the CirKit project (`wiring/Smart Crab Aeration.ckt`,
`wiring/circuit_image.png`), which has not been updated and may still show the old pins, the camera or
LoRa.

## 8. Continuity checklist (from the guide, before plugging in the ESP32)

- [ ] No beep between 5V, GND and 3V3 rails
- [ ] J1 IN1 ↔ GPIO25 header pin; IN2 ↔ 27; IN3 ↔ 33; IN4 ↔ 13
- [ ] J4 A ↔ VP (GPIO36); J5 A ↔ GPIO35
- [ ] J6 Po ↔ GPIO34 ≈ 10 kΩ; GPIO34 ↔ GND ≈ 20 kΩ as built (18 kΩ per guide)
- [ ] J2 M ↔ GPIO4; J2 A ↔ GPIO21; J3 DATA ↔ GPIO19
- [ ] Each IN1–IN4 ↔ 3V3 ≈ 10 kΩ; J3 DATA ↔ 3V3 ≈ 4.7 kΩ
- [ ] ESP32 on laptop USB: 5V rail ≈ 5 V, 3V3 rail ≈ 3.3 V
- [ ] `DOSENSE/relay_wifi_test_k1k4` and `DOSENSE/selector_test` behave as on the breadboard
      (the "K2" button now switches K5)

## 9. Power wiring

| Segment | Spec | Notes |
|---------|------|-------|
| House → float | 220 V AC, 3-core 2.5 mm² earthed outdoor run | Shore-end 30 mA RCD + 2-pole breaker; licensed electrician |
| PSU | 12 V 30 A switching PSU | Above the waterline in the IP65/66 enclosure |
| 12 V distribution | Main fuse → per-branch fuses | Size from measured pump inrush ([`BOM.md`](BOM.md) #21) |
| 5 V | Buck converter 12 V → 5 V | ESP32 5V pin and relay module VCC |
| Grounds | 12 V negative, ESP32 GND, relay GND, sensor GND | Common star point |

## 10. Pre-energize checklist

1. Continuity checklist (§8) passed.
2. Polarity checked on all rails.
3. Relay inputs pulled up (relays OFF) with the ESP32 removed.
4. Relay fully OFF confirmed with IN held at 3.3 V (§6).
5. Branch fuses fitted; glands tight; drip loops formed.
6. Bench supply first, mains last, and the electrician's sign-off before 220 V reaches the float.

## Deferred: LoRa (future phase)

The perfboard keeps the E22-900T22D socket (column L′, rows 14–20): M0/M1 to GND (bridge 14,L′→15,L′
and W23), RXD ← GPIO17 (W24), TXD → GPIO16 (W25), VCC to 5V (W26), GND (W27). Leave the module out in
this phase; if it is fitted later, attach its antenna before powering it (transmitting without an
antenna can damage the PA), and confirm NTC 915 MHz type-approval and EIRP.
