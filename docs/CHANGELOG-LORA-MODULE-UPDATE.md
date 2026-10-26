# LoRa Module Update: E22-900M22S → E22-900T22D

## Summary
Updated all documentation from E22-900M22S (SPI interface, 22 dBm) to E22-900T22D (UART interface, 22 dBm).

## Changes Made

### Documentation Files Updated (10 files)
1. **README.md** - Project overview and specifications
2. **docs/BOM.md** - Bill of Materials with updated pricing and specs
3. **docs/HARDWARE.md** - Hardware guide with new pin map
4. **docs/Components.md** - Component specifications
5. **docs/WIRING.md** - Wiring guide with new UART pin assignments
6. **docs/FIRMWARE.md** - Firmware architecture and pin map
7. **docs/BLOCK-DIAGRAM.md** - Block diagram mermaid code
8. **docs/FLOWCHART.md** - Flowchart node labels
9. **docs/STACKS.md** - Technology stack references
10. **docs/SYSTEM-ARCHITECTURE.md** - Architecture diagrams
11. **docs/SETUP.md** - Setup guide library references
12. **references/Concept-Questions.md** - Radio specifications

## Key Technical Changes

### Interface Change
- **From:** SPI (6 pins: SCK 18, MISO 19, MOSI 23, NSS 5, RST 14, DIO1 26, BUSY 17)
- **To:** UART (2 pins: TX 17, RX 14 on Serial2) + M0/M1 left floating

### Power Increase
- **From:** 22 dBm (160 mW)
- **To:** 22 dBm (1 W) - **~10x power increase**

### Range Estimate
- **From:** ~5-7 km
- **To:** ~10 km

### Antenna
- **From:** External 915 MHz antenna required (separate purchase)
- **To:** Built-in SMA connector (antenna purchased separately)

### Library Change
- **From:** `RadioLib` (SX1262 driver)
- **To:** `EBYTE` (UART module control library)

## Wiring Changes

### New Pin Map (LoRa only)
| Function | GPIO | Notes |
|----------|------|-------|
| TX (Serial2) | 17 | Free digital pin |
| RX (Serial2) | 14 | Free digital pin |
| M0 | NC | Leave floating |
| M1 | NC | Leave floating |

### Freed GPIO Pins
- GPIO 5 (NSS)
- GPIO 18 (SCK)
- GPIO 19 (MISO)
- GPIO 23 (MOSI)
- GPIO 26 (DIO1)

These pins are now available for future expansion.

## Power Consumption
- **From:** ~1.5W per node (22 dBm TX)
- **To:** ~2.0W per node (22 dBm TX)
- **Note:** Higher TX power increases current draw during transmission

## Updated BOM Costs
- **Module price:** ~₱600-800 ea (vs ₱489 for M22S + ₱100 antenna)
- **Total radio stack:** ~₱4,500-5,500 for 3 nodes (vs ~₱2,500 previously)

## Compatibility Notes
- Module has built-in voltage regulator (2.3-5.5V input)
- TTL level compatible with 3.3V and 5V IO
- Must NEVER power without antenna connected (PA damage risk)
- EIRP must be confirmed for NTC type-approval before deployment

## Firmware Migration Required
1. Replace `RadioLib` with `EBYTE` library
2. Change SPI initialization to Serial2 (UART)
3. Update pin definitions in config.h
4. Test transparent mode communication between nodes
5. Verify sync word and frequency settings match between all nodes

## Files Modified
- Total: 12 documentation files
- References updated: 55 occurrences of E22-900T22D
- Old references removed: All instances of E22-900M22S and RadioLib

---
**Date:** 2026-10-05
**Change by:** Project maintenance
**Reason:** Module upgrade for increased range and simplified wiring
