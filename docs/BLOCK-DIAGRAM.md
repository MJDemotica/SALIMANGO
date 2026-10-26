# Hardware Block Diagram

**Project:** Solar Automated LoRa-Integrated Mud Crab Aeration Network with Crab Grid Observation

Float-node hardware for the current phase. Pins follow
[`perfboard_build_guide.pdf`](../perfboard_build_guide.pdf) as listed in [`WIRING.md`](WIRING.md) §3.

## 1. Block diagram

```mermaid
flowchart LR
    subgraph PWR["Power"]
        AC["220 V AC from household solar, 24/7"] --> RCD["30 mA RCD + 2-pole breaker, shore end"]
        RCD --> PSU["12 V 30 A switching PSU"]
        PSU --> BUCK["Buck 12 V to 5 V"]
    end

    subgraph CTRL["Controller (perfboard)"]
        CORE["ESP32 DevKit 38-pin, ESP32-WROOM-32"]
    end

    subgraph SEN["Sensors"]
        S1["pH board, Po -> 10k/20k divider -> GPIO34 (ADC1)"]
        S2["EC board DFR0300 K = 1 -> GPIO35 (ADC1)"]
        S3["DO board SEN0237-A -> GPIO36/VP (ADC1)"]
        S4["DS18B20 1-Wire -> GPIO19, 4.7k pull-up"]
    end

    subgraph RLY["Relay module, low-level trigger, 10k pull-ups"]
        K1["K1 <- GPIO25"]
        K5["K5 <- GPIO27 (lead moved from IN2)"]
        K3["K3 <- GPIO33"]
        K4["K4 <- GPIO13"]
    end

    subgraph ACT["Actuators"]
        P1["Air pump 1, RESUN MPQ-03"]
        P2["Air pump 2, RESUN MPQ-03"]
        P3["Bilge pump 1, 1100 GPH"]
        P4["Bilge pump 2, 1100 GPH"]
    end

    subgraph LOC["Local interface"]
        H1["M-O-A selector, GPIO4 (M) and GPIO21 (A)"]
        H2["Onboard LED GPIO2: link status"]
    end

    WIFI["House Wi-Fi router, 2.4 GHz"]
    E22["E22 socket GPIO16/17: not fitted (LoRa deferred)"]

    PSU --> P1
    PSU --> P2
    PSU --> P3
    PSU --> P4
    BUCK --> CORE
    BUCK --> RLY
    S1 --> CORE
    S2 --> CORE
    S3 --> CORE
    S4 --> CORE
    H1 --> CORE
    CORE --> H2
    CORE --> K1
    CORE --> K5
    CORE --> K3
    CORE --> K4
    K1 --> P1
    K5 --> P2
    K3 --> P3
    K4 --> P4
    CORE <-. "Wi-Fi, HTTPS to Supabase" .-> WIFI
    CORE -.- E22
```

**Figure 1.** Float-node hardware block diagram (current phase).

## 2. Block descriptions

| Block | Function | Key interfaces |
|-------|----------|----------------|
| Power | 220 V AC from the household solar system, shore-end RCD, 12 V PSU, 5 V buck | Electrician-installed AC; 12 V to the pumps through relays and fuses; 5 V to the ESP32 and relay module VCC |
| Controller | ESP32-WROOM-32 on the perfboard: sensing, calibration, control, Wi-Fi | Three ADC1 inputs, 1-Wire, four relay outputs, two selector inputs |
| Sensors | pH, EC (K = 1), DO, temperature | Analog on GPIO34/35/36 (ADC1, Wi-Fi-safe); DS18B20 on GPIO19 |
| Relay module | Low-level trigger; K1, K5, K3, K4 used (K2 defective) | 10 kΩ pull-ups keep relays off through reset |
| Actuators | Two RESUN MPQ-03 air pumps (switched together, 5 s apart), two 1100 GPH bilge pumps (2 s apart) | Branch-fused 12 V |
| Local interface | M-O-A selector (local authority), onboard LED | Active-LOW inputs on GPIO4/21 |
| Wi-Fi | House router | Must reach the float; 2.4 GHz |

## 3. Design notes

1. All analog inputs are on **ADC1**, which keeps working while Wi-Fi is on.
2. No load on a boot-strapping pin; GPIO0, 5, 12, 15 and UART0 (1/3) are unused.
3. Relay outputs are driven HIGH before initialisation and held off by pull-ups through reset; the
   firmware then turns the air pumps on (fail-safe aeration at boot).
4. The relay module is 5 V with 3.3 V logic inputs; confirm each relay is fully off with the input at
   3.3 V ([`WIRING.md`](WIRING.md) §6).

## Removed and deferred blocks

- **Camera node (ESP32-CAM-MB, OV2640)** and its 5 V USB module: removed in this phase.
- **LoRa radio (E22-900T22D) and house bridge node:** deferred; the perfboard's E22 socket stays empty.
