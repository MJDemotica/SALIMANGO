// S.A.L.I.M.A.N.G.O. float node — single source of configuration.
//
// PIN MAP: perfboard_build_guide.pdf is the authoritative reference (the built
// board matches it). Wire numbers (W#) refer to the guide's wire list.
// Board: ESP32 DevKit 38-pin (ESP32-WROOM-32).
#pragma once

#include <stdint.h>

#define FIRMWARE_VERSION "2.0.0"

// ------------------------------------------------------------------ pins ----
// Analog inputs: all on ADC1 (input-only pins), which keeps working while
// Wi-Fi is on. ADC2 must not be used for analog reads with Wi-Fi.
constexpr uint8_t PIN_PH = 34;  // J6 Po -> R7 10k -> node -> GPIO34 (W21, W22); node -> R8 -> GND
constexpr uint8_t PIN_EC = 35;  // J5 A -> GPIO35 (W19)
constexpr uint8_t PIN_DO = 36;  // J4 A -> VP/GPIO36 (W17)

// 1-Wire DS18B20 bus: J3 DATA -> GPIO19 with R9 4.7k pull-up to 3V3 (W33, W34).
constexpr uint8_t PIN_ONEWIRE = 19;

// Relay inputs (low-level trigger: LOW = relay ON). Each has a 10k pull-up to
// 3V3 on the perfboard (R1–R4) so the relays stay OFF through reset.
// The relay module's K2 channel has a defective terminal: the lead from J1 IN2
// (GPIO27) is moved to the module's IN5, so GPIO27 drives K5.
constexpr uint8_t PIN_RELAY_K1 = 25;  // J1 IN1 (W8, W9)   air pump 1
constexpr uint8_t PIN_RELAY_K5 = 27;  // J1 IN2 (W10, W11) air pump 2 -> module IN5
constexpr uint8_t PIN_RELAY_K3 = 33;  // J1 IN3 (W12, W13) bilge pump 1
constexpr uint8_t PIN_RELAY_K4 = 13;  // J1 IN4 (W14, W15) bilge pump 2

// Mode selector (INScom LW28-20A M-O-A), contacts close to GND; 10k pull-ups
// R5/R6 to 3V3. Position M pulls GPIO4 LOW, A pulls GPIO21 LOW, O leaves both HIGH.
constexpr uint8_t PIN_SEL_M = 4;   // J2 M (W28, W29)
constexpr uint8_t PIN_SEL_A = 21;  // J2 A (W30, W31)

// Onboard LED of the DevKit (link status). Set to 0xFF to disable.
constexpr uint8_t PIN_STATUS_LED = 2;

// GPIO16/17 (E22 socket, W24/W25) are reserved for LoRa, which is deferred in
// this phase: the firmware does not use them and the E22 module is not fitted.

// --------------------------------------------------------- pH (PH-4502C) ----
// Divider between the 5 V pH board and GPIO34. The perfboard guide lists
// R8 = 18k; the board as built uses 20k (team-confirmed), so the ratio is
// (10k + 20k) / 20k = 1.5. Change PH_DIVIDER_BOTTOM_OHMS if R8 is replaced.
constexpr float PH_DIVIDER_TOP_OHMS = 10000.0f;
constexpr float PH_DIVIDER_BOTTOM_OHMS = 20000.0f;
constexpr float PH_DIVIDER_RATIO = (PH_DIVIDER_TOP_OHMS + PH_DIVIDER_BOTTOM_OHMS) / PH_DIVIDER_BOTTOM_OHMS;
constexpr uint8_t PH_SAMPLES = 10;          // 10 x 10 ms = 100 ms window (cancels 60 Hz hum)
constexpr uint8_t PH_STABLE_COUNT = 5;      // readings used for the stability check
constexpr float PH_STABLE_TOL_V = 0.005f;   // max spread (V) to count as stable
constexpr float PH_PIN_MIN_MV = 50.0f;      // below: board unplugged (divider pulls to GND)
constexpr float PH_PIN_MAX_MV = 3100.0f;    // above: ADC saturated

// ------------------------------------------------- EC (DFR0300, K = 1) ------
// Cell constant and transfer-function constants live in sensor_math.h.
constexpr uint32_t EC_SAMPLE_MS = 40;   // one ADC sample every 40 ms
constexpr uint8_t EC_WINDOW = 25;       // 25 x 40 ms = 1 s window
constexpr uint8_t EC_TRIM = 5;          // drop 5 lowest + 5 highest
constexpr uint8_t EC_STABLE_WINDOWS = 5;
constexpr float EC_STABLE_TOL = 0.01f;  // ±1 % spread (min 5 mV)

// --------------------------------------------------- DO (SEN0237-A) ---------
constexpr uint8_t DO_SAMPLES = 10;
constexpr float DO_PIN_MAX_MV = 3100.0f;

// ------------------------------------------------------------- control ------
constexpr uint32_t CONTROL_INTERVAL_MS = 3000;     // DO/bilge decisions (3 readings ≈ 9 s)
constexpr uint32_t CAL_READ_INTERVAL_MS = 1000;    // faster readings while calibrating
constexpr uint32_t MANUAL_TIMEOUT_MS = 5UL * 60UL * 1000UL;  // MANUAL -> AUTO
constexpr uint32_t AIR_STAGGER_MS = 5000;          // K1 then K5
constexpr uint32_t BILGE_STAGGER_MS = 2000;        // K3 then K4 (salinity_rev.md)
constexpr uint32_t SELECTOR_DEBOUNCE_MS = 50;
constexpr uint32_t CAL_SESSION_TIMEOUT_MS = 30UL * 60UL * 1000UL;  // auto-abort a forgotten session

// Bilge defaults until the dashboard settings arrive (docs/APP.md, salinity_rev.md).
constexpr float DEFAULT_SAL_MIN_PPT = 28.0f;
constexpr float DEFAULT_SAL_MAX_PPT = 32.0f;
constexpr float DEFAULT_SAL_HYST_PPT = 2.0f;
constexpr uint16_t DEFAULT_BILGE_MAX_RUN_MIN = 15;
constexpr uint16_t DEFAULT_BILGE_MIN_OFF_MIN = 30;
constexpr uint8_t DEFAULT_BILGE_MAX_FAILS = 3;

// Emerson ammonia fraction is reported only inside this input domain.
constexpr float NH3_MIN_T_C = 0.0f, NH3_MAX_T_C = 40.0f;
constexpr float NH3_MIN_PH = 6.0f, NH3_MAX_PH = 10.0f;

// ---------------------------------------------------------- cloud link ------
constexpr uint32_t REPORT_INTERVAL_MS = 10000;      // normal
constexpr uint32_t REPORT_FAST_INTERVAL_MS = 2000;  // calibration / pending commands
constexpr uint32_t REPORT_MAX_BACKOFF_MS = 60000;
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint16_t HTTP_TIMEOUT_MS = 8000;
