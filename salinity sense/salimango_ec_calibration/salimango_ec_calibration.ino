/*
  S.A.L.I.M.A.N.G.O. — EC calibration + dual DS18B20 temperature compensation
  ---------------------------------------------------------------------------
  Target   : ESP32 DevKit 38-pin (ESP32-WROOM-32), Arduino-ESP32 core 2.x / 3.x
  Sensor   : DFRobot Gravity Analog EC Sensor V2, K=1 (DFR0300), 0–3.4 V output
  Temp     : 2x waterproof DS18B20 on one shared 1-Wire bus
  Pins     : EC analog  -> GPIO 35 (ADC1_CH7, input-only)      [WIRING.md §3]
             1-Wire bus -> GPIO 19, external 4.7 kOhm pull-up to 3.3 V
  Libraries: OneWire, DallasTemperature (Library Manager); Preferences (built-in)

  Conversion uses the same two-point K-value algorithm as DFRobot_EC
  (RES2 = 820, ECREF = 200, 1.85 %/°C compensation, 1.413 / 12.88 mS/cm buffers),
  re-implemented so that:
    - the ESP32 ADC is read through analogReadMilliVolts() (eFuse-calibrated mV),
    - K values persist in NVS (Preferences) per FIRMWARE.md §7,
    - nothing blocks loop(): no delay(); DS18B20 runs asynchronously.

  Serial Monitor: 115200 baud, line ending = "Newline".
  Commands: help | enter | cal | exit | abort | show | reset
*/

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <math.h>

// ===================== Pin map (docs/WIRING.md §3) =====================
constexpr uint8_t PIN_EC      = 35;
constexpr uint8_t PIN_ONEWIRE = 19;

// ============ DFR0300 (K=1) constants — DFRobot_EC algorithm ===========
constexpr float RES2        = 820.0f;
constexpr float ECREF       = 200.0f;
constexpr float TEMP_COEF   = 0.0185f;   // per °C, referenced to 25 °C
constexpr float K_MIN       = 0.5f;      // valid K window for a K=1 probe
constexpr float K_MAX       = 1.5f;
constexpr float STD_LOW     = 1.413f;    // mS/cm @ 25 °C
constexpr float STD_HIGH    = 12.88f;    // mS/cm @ 25 °C
constexpr float RAW_MAX     = 20.0f;     // K=1 usable ceiling, mS/cm
constexpr float ADC_CLIP_MV = 3000.0f;   // ESP32 ADC goes non-linear above ~3.1 V

// ============================ Timing ===================================
constexpr uint32_t EC_SAMPLE_MS = 40;    // one ADC sample every 40 ms
constexpr uint8_t  EC_WINDOW    = 25;    // 25 x 40 ms = 1 s window
constexpr uint8_t  EC_TRIM      = 5;     // drop 5 lowest + 5 highest
constexpr uint32_t T_CONV_MS    = 750;   // DS18B20 12-bit conversion time
constexpr uint8_t  STAB_WINDOWS = 5;     // 5 s history for stability check
constexpr float    STAB_TOL     = 0.01f; // ±1 % spread (min 5 mV)
constexpr float    T_MISMATCH_C = 0.5f;  // TESTING.md SEN-01 tolerance

// ============================ Objects ==================================
OneWire           oneWire(PIN_ONEWIRE);
DallasTemperature ds(&oneWire);
Preferences       prefs;

// ======================== Temperature state ============================
DeviceAddress tAddr[2];
bool     tPresent[2] = {false, false};
bool     tValid[2]   = {false, false};
float    tC[2]       = {NAN, NAN};
uint32_t tReqAt      = 0;

// =========================== EC state ==================================
float    ecBuf[EC_WINDOW];
uint8_t  ecIdx        = 0;
uint32_t ecLastSample = 0;
float    mvAvg        = NAN;
float    mvHist[STAB_WINDOWS];
uint8_t  histN = 0, histIdx = 0;
bool     windowReady  = false;

// ======================== Calibration state ============================
float kLow = 1.0f, kHigh = 1.0f, kActive = 1.0f;
bool  calMode  = false;
float pendLow  = NAN;
float pendHigh = NAN;

// ======================== Serial line buffer ===========================
char    cmdLine[24];
uint8_t cmdLen = 0;

// ============================ Helpers ==================================
float trimmedMean(const float *src, uint8_t n, uint8_t trim) {
  float a[EC_WINDOW];
  memcpy(a, src, n * sizeof(float));
  for (uint8_t i = 1; i < n; i++) {            // insertion sort (n = 25)
    float v = a[i];
    int8_t j = i - 1;
    while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
    a[j + 1] = v;
  }
  float s = 0;
  for (uint8_t i = trim; i < n - trim; i++) s += a[i];
  return s / (n - 2 * trim);
}

float rawEC(float mv) {                        // mS/cm at K = 1, uncompensated
  return 1000.0f * mv / RES2 / ECREF;
}

float ecAt25(float mv, float tempC) {          // calibrated, compensated to 25 °C
  float raw = rawEC(mv);
  float trial = raw * kActive;                 // auto-range with hysteresis
  if (trial > 2.5f)      kActive = kHigh;
  else if (trial < 2.0f) kActive = kLow;
  return raw * kActive / (1.0f + TEMP_COEF * (tempC - 25.0f));
}

// Probe 1 is the probe mounted beside the EC cell; probe 2 is the fallback.
bool compensationTemp(float &t) {
  if (tValid[0]) { t = tC[0]; return true; }
  if (tValid[1]) { t = tC[1]; return true; }
  t = 25.0f;                                   // no compensation; flagged
  return false;
}

bool isStable() {
  if (histN < STAB_WINDOWS) return false;
  float mn = mvHist[0], mx = mvHist[0], sum = 0;
  for (uint8_t i = 0; i < STAB_WINDOWS; i++) {
    mn = min(mn, mvHist[i]);
    mx = max(mx, mvHist[i]);
    sum += mvHist[i];
  }
  float mean = sum / STAB_WINDOWS;
  return (mx - mn) <= max(STAB_TOL * mean, 5.0f);
}

void printAddress(const DeviceAddress a) {
  for (uint8_t i = 0; i < 8; i++) {
    if (a[i] < 16) Serial.print('0');
    Serial.print(a[i], HEX);
  }
}

void fmtTemp(char *out, size_t n, uint8_t i) {
  if (tValid[i]) snprintf(out, n, "%.2f", tC[i]);
  else           snprintf(out, n, "FAULT");
}

// ======================== Non-blocking pollers =========================
void pollEC(uint32_t now) {
  if (now - ecLastSample < EC_SAMPLE_MS) return;
  ecLastSample = now;
  ecBuf[ecIdx++] = (float)analogReadMilliVolts(PIN_EC);
  if (ecIdx >= EC_WINDOW) {
    ecIdx = 0;
    mvAvg = trimmedMean(ecBuf, EC_WINDOW, EC_TRIM);
    mvHist[histIdx] = mvAvg;
    histIdx = (histIdx + 1) % STAB_WINDOWS;
    if (histN < STAB_WINDOWS) histN++;
    windowReady = true;
  }
}

void pollTemps(uint32_t now) {
  if (now - tReqAt < T_CONV_MS) return;        // conversion still running
  for (uint8_t i = 0; i < 2; i++) {
    if (!tPresent[i]) { tValid[i] = false; continue; }
    float v = ds.getTempC(tAddr[i]);
    // -127 = disconnected; 85.0 = power-on reset value (never a real pond reading)
    tValid[i] = (v != DEVICE_DISCONNECTED_C) && (v != 85.0f) && (v > -10.0f) && (v < 60.0f);
    tC[i] = v;
  }
  ds.requestTemperatures();                    // async: returns immediately
  tReqAt = now;
}

// ======================== Calibration commands =========================
void printHelp() {
  Serial.println(F("\nCommands: help | enter | cal | exit | abort | show | reset"));
  Serial.println(F("  enter : start calibration mode"));
  Serial.println(F("  cal   : capture K for the buffer the probe is in (1.413 or 12.88 mS/cm)"));
  Serial.println(F("  exit  : save captured K values to NVS and leave calibration mode"));
  Serial.println(F("  abort : leave calibration mode without saving"));
  Serial.println(F("  show  : print stored K values"));
  Serial.println(F("  reset : restore K values to 1.000 (uncalibrated)\n"));
}

void cmdCal() {
  if (!calMode) { Serial.println(F("[CAL] Type 'enter' first.")); return; }
  float t;
  if (!compensationTemp(t)) {
    Serial.println(F("[CAL] REJECTED: no valid DS18B20 reading — fix the 1-Wire bus first."));
    return;
  }
  if (isnan(mvAvg) || !isStable()) {
    Serial.println(F("[CAL] REJECTED: reading not stable yet. Wait for STABLE, then retry."));
    return;
  }
  float raw = rawEC(mvAvg);
  float stdVal;
  bool  isLow;
  if (raw > 0.9f && raw < 1.9f)        { stdVal = STD_LOW;  isLow = true;  }
  else if (raw > 9.0f && raw < 16.8f)  { stdVal = STD_HIGH; isLow = false; }
  else {
    Serial.printf("[CAL] REJECTED: raw %.3f mS/cm matches neither buffer window "
                  "(0.9–1.9 or 9–16.8). Check solution, probe immersion, wiring.\n", raw);
    return;
  }
  float compStd = stdVal * (1.0f + TEMP_COEF * (t - 25.0f));
  float k = RES2 * ECREF * compStd / 1000.0f / mvAvg;
  if (k < K_MIN || k > K_MAX) {
    Serial.printf("[CAL] REJECTED: K = %.4f outside %.1f–%.1f. Probe fouled, wrong buffer, "
                  "or wrong sensor (K=10?).\n", k, K_MIN, K_MAX);
    return;
  }
  if (isLow) pendLow = k; else pendHigh = k;
  Serial.printf("[CAL] %s buffer %.3f mS/cm @ %.2f °C -> V = %.1f mV, K = %.4f (pending; type 'exit' to save)\n",
                isLow ? "LOW" : "HIGH", stdVal, t, mvAvg, k);
}

void cmdExit() {
  if (!calMode) { Serial.println(F("[CAL] Not in calibration mode.")); return; }
  if (!isnan(pendLow))  { kLow  = pendLow;  prefs.putFloat("kLow",  kLow);  }
  if (!isnan(pendHigh)) { kHigh = pendHigh; prefs.putFloat("kHigh", kHigh); }
  if (isnan(pendLow) || isnan(pendHigh)) {
    Serial.println(F("[CAL] WARNING: only one point captured — the other K keeps its previous value."));
  }
  kActive = kLow;
  calMode = false;
  pendLow = pendHigh = NAN;
  Serial.printf("[CAL] Saved to NVS: kLow = %.4f, kHigh = %.4f\n", kLow, kHigh);
}

void handleCommand(const char *c) {
  if      (!strcmp(c, "help"))  printHelp();
  else if (!strcmp(c, "enter")) {
    calMode = true; pendLow = pendHigh = NAN;
    Serial.println(F("[CAL] Calibration mode. Immerse EC probe + probe T1 in a buffer, wait for STABLE, type 'cal'."));
  }
  else if (!strcmp(c, "cal"))   cmdCal();
  else if (!strcmp(c, "exit"))  cmdExit();
  else if (!strcmp(c, "abort")) {
    calMode = false; pendLow = pendHigh = NAN;
    Serial.println(F("[CAL] Aborted. Nothing saved."));
  }
  else if (!strcmp(c, "show"))  Serial.printf("[CAL] kLow = %.4f, kHigh = %.4f\n", kLow, kHigh);
  else if (!strcmp(c, "reset")) {
    kLow = kHigh = kActive = 1.0f;
    prefs.putFloat("kLow", kLow); prefs.putFloat("kHigh", kHigh);
    Serial.println(F("[CAL] K values reset to 1.0000 (uncalibrated)."));
  }
  else Serial.printf("Unknown command '%s' — type 'help'.\n", c);
}

void pollSerial() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (cmdLen) { cmdLine[cmdLen] = '\0'; handleCommand(cmdLine); cmdLen = 0; }
    } else if (cmdLen < sizeof(cmdLine) - 1) {
      cmdLine[cmdLen++] = (char)tolower(ch);
    }
  }
}

// ============================== Report =================================
void report() {
  float t;
  bool  tOK = compensationTemp(t);
  float raw = rawEC(mvAvg);
  float ec  = ecAt25(mvAvg, t);
  char  s1[10], s2[10];
  fmtTemp(s1, sizeof(s1), 0);
  fmtTemp(s2, sizeof(s2), 1);

  Serial.printf("T1=%s T2=%s Tcomp=%.2f | V=%.1f mV raw=%.3f EC25=%.3f mS/cm K=%.4f",
                s1, s2, t, mvAvg, raw, ec, kActive);
  if (!tOK) Serial.print(F(" [TEMP_FAULT]"));
  if (tValid[0] && tValid[1] && fabsf(tC[0] - tC[1]) > T_MISMATCH_C) Serial.print(F(" [T_MISMATCH]"));
  if (mvAvg > ADC_CLIP_MV) Serial.print(F(" [ADC_CLIP]"));
  if (raw > RAW_MAX)       Serial.print(F(" [OVER_RANGE_K1]"));
  if (calMode) Serial.print(isStable() ? F(" [CAL][STABLE]") : F(" [CAL][settling]"));
  Serial.println();
}

// =========================== setup / loop ==============================
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  // Arduino-ESP32 defaults to 11 dB attenuation (~0–3.1 V usable), required
  // for the 0–3.4 V DFR0300 output. Do not lower the attenuation.
  pinMode(PIN_EC, INPUT);

  prefs.begin("ec_cal", false);
  kLow    = prefs.getFloat("kLow",  1.0f);
  kHigh   = prefs.getFloat("kHigh", 1.0f);
  kActive = kLow;

  ds.begin();
  uint8_t n = ds.getDeviceCount();
  Serial.printf("\nDS18B20 devices found on GPIO %u: %u\n", PIN_ONEWIRE, n);
  for (uint8_t i = 0; i < 2; i++) {
    tPresent[i] = ds.getAddress(tAddr[i], i);
    Serial.printf("  T%u: ", i + 1);
    if (tPresent[i]) { printAddress(tAddr[i]); ds.setResolution(tAddr[i], 12); Serial.println(); }
    else Serial.println(F("NOT FOUND — check wiring and 4.7 kOhm pull-up"));
  }
  ds.setWaitForConversion(false);
  ds.requestTemperatures();
  tReqAt = millis();

  Serial.printf("Stored K: kLow = %.4f, kHigh = %.4f\n", kLow, kHigh);
  printHelp();
}

void loop() {
  uint32_t now = millis();
  pollEC(now);
  pollTemps(now);
  pollSerial();
  if (windowReady) { windowReady = false; report(); }
}
