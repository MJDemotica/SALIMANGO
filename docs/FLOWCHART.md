# System Flowchart

**Project:** Solar Automated LoRa-Integrated Mud Crab Aeration Network with Crab Grid Observation

One control cycle of the current phase: sensing and local control on the ESP32, the Wi-Fi report to
Supabase, and dashboard commands. Rules are detailed in [`FIRMWARE.md`](FIRMWARE.md) §2.

## 1. Flowchart

```mermaid
flowchart TD
    subgraph NODE["Float node - ESP32 (control loop, core 1)"]
        A([Power on or reset]) --> B[Relay pins HIGH = off, then init sensors, load calibration and settings from NVS]
        B --> C["Mode from selector; AUTO unless the selector is at M or O; air pumps ON"]
        C --> D["Read DS18B20, pH, EC, DO; validate; compute PSS-78 salinity and Emerson NH3 fraction"]
        D --> E{Selector position}
        E -->|O| M0[Mode OFF]
        E -->|M| M1[Mode MANUAL]
        E -->|A or fault| M2[Mode requested by the dashboard, default AUTO]
        M1 --> T{5 min without a manual command?}
        M2 --> T
        T -->|Yes| M3[Back to AUTO]
        T -->|No| F
        M0 --> F
        M3 --> F
        F{Valid DO below 3.0 mg/L for 3 readings?}
        F -->|Yes| G[DO emergency: K1 + K5 forced ON in every mode, event]
        F -->|No| H{Mode}
        G --> H
        H -->|AUTO| I["DO below 3.0 for 3 readings: K1, then K5 after 5 s; above 3.0 for 3 readings: both off"]
        I --> J{Salinity below salMin for 3 readings?}
        J -->|Yes, bilge idle| K[K3, then K4 after 2 s]
        K --> K2{Salinity at salMin + salHyst, or max run reached?}
        K2 -->|Recovered| K3[Stop, rest]
        K2 -->|Timeout| K4{Too many timeouts?}
        K4 -->|Yes| K5[Lockout + critical event]
        K4 -->|No| K3
        J -->|No| O
        K3 --> O
        K5 --> O
        H -->|MANUAL| L[Relays as commanded from the dashboard]
        H -->|OFF| N[All relays off except the DO emergency]
        L --> O
        N --> O
        O["Apply relay outputs; publish state"] --> P{Command received?}
        P -->|Yes| Q{Valid for the current selector, mode and DO interlock?}
        Q -->|Yes| R["Apply; ack accepted"]
        Q -->|No| S[Ack rejected with the reason]
        P -->|No| D
        R --> D
        S --> D
    end

    subgraph CLOUD["Float node - cloud task (core 0)"]
        U[Every 10 s, 2 s when busy: POST device_report with state, acks, events] --> V{Response OK?}
        V -->|Yes| W[Queue commands and new settings for the control loop]
        V -->|No| X["Keep acks and events, retry with backoff; LED blinks"]
    end

    subgraph SB["Supabase"]
        Y["Store state, history once a minute, events, calibration records; resolve acks; expire old requests"]
        Y --> Z[Realtime to the dashboard]
    end

    subgraph DASH["React dashboard (Vercel)"]
        AA[Live cards, charts, stale and simulated banners] --> AB[Operator requests mode, pumps, calibration or settings]
        AB --> AC[request_command / update_device_settings validate and queue]
    end

    O --> U
    U --> Y
    Z --> AA
    AC --> Y
    W --> P
```

**Figure 1.** Operational flowchart of the current phase.

## 2. Flow description

1. **Boot.** Relays are held off during initialisation, then the air pumps start (fail-safe).
   Calibration and settings come from flash, so the node works without the network.
2. **Sensing.** Each reading is validated; a fault or out-of-range value is reported as such, never as
   a number. Salinity and the NH₃ fraction need a valid temperature.
3. **Mode.** The selector decides at M and O; at A the dashboard chooses. MANUAL returns to AUTO after
   5 minutes without a command.
4. **DO emergency.** Evaluated in every mode, before anything else.
5. **AUTO.** Aeration by DO; salinity correction by the `salinity_rev.md` state machine, evaluated
   independently of the DO branch.
6. **Commands.** Validated twice (database, then device) and acknowledged with a reason.
7. **Reporting.** A separate task sends the state; network problems never block the control loop.

## 3. Fail-safe notes

- Boot, reset and Wi-Fi loss never switch aeration off.
- A missing DO reading counts as low for AUTO aeration.
- The selector always overrides the dashboard at the device.
- Stale data on the dashboard is shown as stale; an old mode is never shown as confirmed.

The image `SYSTEM FLOWCHART SALIMANGO3.drawio.png` in the repository root shows the previous
(LoRa / camera) design and has not been updated.
