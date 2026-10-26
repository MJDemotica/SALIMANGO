/*
 * pH_ESP32_DS18B20_cmd.ino
 *
 * ESP32 port of the pH sensing code, with one DS18B20 for temperature
 * and serial-command calibration.
 *
 * pH conversion:
 *   pH = 7.0 + (PH7_VOLTAGE - V) / PH_SLOPE
 *   compensated pH = pH + (T - 25) * 0.03
 * PH7_VOLTAGE and PH_SLOPE are set by calibration and saved in flash.
 *
 * Serial Monitor: 115200 baud, line ending "Newline"
 * Serial Commands:
 *   enterph       -> enter calibration mode
 *   calph         -> capture the buffer the probe is in (4.01, 6.86 or 9.18,
 *                    recognized automatically). Repeat for each buffer.
 *   exitph        -> calculate, save to flash and exit calibration mode
 *
 *   1 buffer  -> corrects the offset only
 *   2 buffers -> corrects offset and slope
 *   3 buffers -> best-fit line through all three (recommended)
 *
 * Temperature code: S.A.L.I.M.A.N.G.O. DS18B20 handling
 *   - non-blocking (asynchronous) conversions
 *   - invalid readings rejected (-127 disconnected, 85.0 power-on value, out of range)
 *   - falls back to 25 °C (flagged [TEMP_FAULT]) if the sensor faults
 *
 * Required libraries (Arduino Library Manager):
 *   - OneWire
 *   - DallasTemperature
 */

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <math.h>

// ---------------- Pins ----------------
#define PH_PIN        35   // ADC1_CH7, input-only. Use ADC1 pins only: ADC2 doesn't work while Wi-Fi is on
constexpr uint8_t PIN_ONEWIRE = 19;   // 1-Wire bus, external 4.7 kOhm pull-up to 3.3 V

// ---------------- pH constants ----------------
#define DEFAULT_PH7_V 2.5      // module output (V) at pH 7 before calibration
#define DEFAULT_SLOPE 0.18     // V per pH unit before calibration
#define PH_TEMP_COEF  0.03     // pH units per °C (original code's compensation)
#define DIVIDER_RATIO 1.5      // 10k (top) + 20k (bottom) divider: Vmodule = Vpin * 1.5
                               // 10k/10k = 2.0 ; no divider = 1.0
#define PH_SAMPLES    10       // 10 x 10 ms = 100 ms window (cancels 60 Hz mains hum)
#define READ_INTERVAL 1000UL   // ms
#define SLOPE_MIN     0.12     // reject calibrations with a slope outside this range
#define SLOPE_MAX     0.24
#define STAB_COUNT    5        // readings used for the stability check (5 s)
#define STAB_TOL_V    0.010    // max spread (V) to count as STABLE

// ---------------- Temperature constants ----------------
constexpr uint32_t T_CONV_MS    = 750;   // DS18B20 12-bit conversion time

// ---------------- Buffer tables (pH vs temperature) ----------------
const float BUF_T[6]       = {15, 20, 25, 30, 35, 40};
const float BUF_NOMINAL[3] = {4.01, 6.86, 9.18};
const float BUF_TABLE[3][6] = {
  {4.00, 4.00, 4.01, 4.01, 4.02, 4.03},   // 4.01 phthalate
  {6.90, 6.88, 6.86, 6.85, 6.84, 6.84},   // 6.86 phosphate
  {9.28, 9.23, 9.18, 9.14, 9.10, 9.07}    // 9.18 borax
};

// ============================ Objects ==================================
OneWire           oneWire(PIN_ONEWIRE);
DallasTemperature ds(&oneWire);
Preferences       prefs;

// ======================== Temperature state ============================
DeviceAddress tAddr;
bool     tPresent = false;
bool     tValid   = false;
float    tC       = NAN;
uint32_t tReqAt   = 0;

float temperature = 25.0;   // compensation temperature actually used

// ============================ pH state =================================
float ph7V  = DEFAULT_PH7_V;
float slope = DEFAULT_SLOPE;
float voltage = 0;

float   vHist[STAB_COUNT];
uint8_t vN = 0, vIdx = 0;

bool  calMode = false;
bool  cap[3];
float capV[3], capRaw[3], capBuf[3], capT[3];

String cmdBuffer = "";

// ======================== Temperature helpers ==========================

// Uses the DS18B20 beside the pH probe; falls back to 25 °C if it faults.
bool compensationTemp(float &t) {
  if (tValid) { t = tC; return true; }
  t = 25.0f;                                   // no compensation; flagged
  return false;
}

void printAddress(const DeviceAddress a) {
  for (uint8_t i = 0; i < 8; i++) {
    if (a[i] < 16) Serial.print('0');
    Serial.print(a[i], HEX);
  }
}

void fmtTemp(char *out, size_t n) {
  if (tValid) snprintf(out, n, "%.2f", tC);
  else        snprintf(out, n, "FAULT");
}

// Non-blocking: reads the finished conversion, then starts the next one
void pollTemps(uint32_t now) {
  if (now - tReqAt < T_CONV_MS) return;        // conversion still running
  if (!tPresent) {
    tValid = false;
  } else {
    float v = ds.getTempC(tAddr);
    // -127 = disconnected; 85.0 = power-on reset value (never a real pond reading)
    tValid = (v != DEVICE_DISCONNECTED_C) && (v != 85.0f) && (v > -10.0f) && (v < 60.0f);
    tC = v;
  }
  ds.requestTemperatures();                    // async: returns immediately
  tReqAt = now;
}

void setupTemps() {
  ds.begin();
  uint8_t n = ds.getDeviceCount();
  Serial.printf("\nDS18B20 devices found on GPIO %u: %u\n", PIN_ONEWIRE, n);
  tPresent = ds.getAddress(tAddr, 0);
  Serial.print("  Temp sensor: ");
  if (tPresent) { printAddress(tAddr); ds.setResolution(tAddr, 12); Serial.println(); }
  else Serial.println(F("NOT FOUND — check wiring and 4.7 kOhm pull-up"));
  ds.setWaitForConversion(false);
  ds.requestTemperatures();
  tReqAt = millis();
}

// ============================ pH functions =============================

float ph(float voltage) {
  return 7.0 + ((ph7V - voltage) / slope);
}

float compensate(float rawPH, float t) {
  return rawPH + (t - 25.0) * PH_TEMP_COEF;
}

// Average of PH_SAMPLES readings, returned as the module's output voltage (V)
float readPHVoltage() {
  uint32_t sum = 0;
  for (int i = 0; i < PH_SAMPLES; i++) {
    sum += analogReadMilliVolts(PH_PIN);       // eFuse-calibrated millivolts
    delay(10);
  }
  return (sum / (float)PH_SAMPLES) / 1000.0 * DIVIDER_RATIO;
}

// Buffer pH at temperature t (linear interpolation of the table)
float bufferAtTemp(int b, float t) {
  if (t <= BUF_T[0]) return BUF_TABLE[b][0];
  if (t >= BUF_T[5]) return BUF_TABLE[b][5];
  for (int i = 0; i < 5; i++) {
    if (t <= BUF_T[i + 1]) {
      float f = (t - BUF_T[i]) / (BUF_T[i + 1] - BUF_T[i]);
      return BUF_TABLE[b][i] + f * (BUF_TABLE[b][i + 1] - BUF_TABLE[b][i]);
    }
  }
  return BUF_NOMINAL[b];
}

void pushVoltage(float v) {
  vHist[vIdx] = v;
  vIdx = (vIdx + 1) % STAB_COUNT;
  if (vN < STAB_COUNT) vN++;
}

bool isStable() {
  if (vN < STAB_COUNT) return false;
  float mn = vHist[0], mx = vHist[0];
  for (uint8_t i = 1; i < STAB_COUNT; i++) { mn = min(mn, vHist[i]); mx = max(mx, vHist[i]); }
  return (mx - mn) <= STAB_TOL_V;
}

// ======================== Calibration commands =========================

void cmdEnter() {
  calMode = true;
  for (int i = 0; i < 3; i++) cap[i] = false;
  Serial.println();
  Serial.println(F(">>>Enter pH Calibration Mode<<<"));
  Serial.println(F(">>>Put the probe + temp sensor in a buffer (4.01, 6.86 or 9.18),"));
  Serial.println(F("   wait for [STABLE], then type calph<<<"));
  Serial.println();
}

void cmdCal() {
  if (!calMode) { Serial.println(F(">>>Type enterph first<<<")); return; }
  if (!tValid) {
    Serial.println(F(">>>REJECTED: no valid temperature reading. Check the DS18B20.<<<"));
    return;
  }
  if (!isStable()) {
    Serial.println(F(">>>REJECTED: reading not stable yet. Wait for [STABLE], then retry.<<<"));
    return;
  }

  // Recognize the buffer: nearest one to the current reading, within ±1.0 pH
  int   b = -1;
  float est = compensate(ph(voltage), temperature);
  float best = 99;
  for (int i = 0; i < 3; i++) {
    float d = fabsf(est - BUF_NOMINAL[i]);
    if (d < best) { best = d; b = i; }
  }
  if (best > 1.0) {
    Serial.printf(">>>REJECTED: reading (pH %.2f, V %.3f) doesn't match 4.01, 6.86 or 9.18.\n"
                  "   Check the buffer and probe, then retry.<<<\n", est, voltage);
    return;
  }

  cap[b]    = true;
  capV[b]   = voltage;
  capT[b]   = temperature;
  capBuf[b] = bufferAtTemp(b, temperature);
  // Raw pH the formula must give so that the compensated pH equals the buffer value
  capRaw[b] = capBuf[b] - (temperature - 25.0) * PH_TEMP_COEF;

  int n = cap[0] + cap[1] + cap[2];
  Serial.printf("\n>>>Captured %.2f buffer (= pH %.2f at %.2f C), V = %.3f V  [%d buffer(s) so far]\n",
                BUF_NOMINAL[b], capBuf[b], temperature, voltage, n);
  Serial.println(F("   Rinse, move to the next buffer and type calph, or type exitph to save<<<\n"));
}

void cmdExit() {
  if (!calMode) return;
  calMode = false;

  int n = cap[0] + cap[1] + cap[2];
  if (n == 0) {
    Serial.println(F("\n>>>No buffer captured, nothing saved. Exit pH Calibration Mode<<<\n"));
    return;
  }

  float newPh7V, newSlope;
  if (n == 1) {
    // Offset only: keep the existing slope
    int b = cap[0] ? 0 : (cap[1] ? 1 : 2);
    newSlope = slope;
    newPh7V  = capV[b] + slope * (capRaw[b] - 7.0);
  } else {
    // Least-squares line V = a + k * rawPH through the captured points
    float sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < 3; i++) {
      if (!cap[i]) continue;
      sx += capRaw[i]; sy += capV[i];
      sxx += capRaw[i] * capRaw[i]; sxy += capRaw[i] * capV[i];
    }
    float k = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    float a = (sy - k * sx) / n;
    newSlope = -k;
    newPh7V  = a + k * 7.0;
  }

  if (newSlope < SLOPE_MIN || newSlope > SLOPE_MAX) {
    Serial.printf("\n>>>Calibration FAILED: slope %.4f V/pH outside %.2f–%.2f.\n"
                  "   Check buffers, rinse between them, or the probe may be worn. Nothing saved.<<<\n\n",
                  newSlope, SLOPE_MIN, SLOPE_MAX);
    return;
  }

  ph7V = newPh7V;
  slope = newSlope;
  prefs.putFloat("ph7V", ph7V);
  prefs.putFloat("slope", slope);

  Serial.printf("\n>>>Calibration Successful (%d point%s), saved:\n"
                "   PH7_VOLTAGE = %.4f V, PH_SLOPE = %.4f V/pH\n"
                "   Exit pH Calibration Mode<<<\n\n",
                n, n > 1 ? "s" : "", ph7V, slope);
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;

  if      (cmd == "ENTERPH") cmdEnter();
  else if (cmd == "CALPH")   cmdCal();
  else if (cmd == "EXITPH")  cmdExit();
  else Serial.println(F("Commands: enterph | calph | exitph"));
}

void readSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdBuffer.length() > 0) { handleCommand(cmdBuffer); cmdBuffer = ""; }
    } else if (cmdBuffer.length() < 20) {
      cmdBuffer += c;
    }
  }
}

// =========================== setup / loop ==============================

void setup() {
  Serial.begin(115200);
  delay(500);

  // ADC: 12-bit, 11 dB attenuation (~0.15 to ~3.1 V usable range)
  analogReadResolution(12);
  analogSetPinAttenuation(PH_PIN, ADC_11db);

  // Load calibration from flash (defaults if never calibrated)
  prefs.begin("ph_cmd", false);
  ph7V  = prefs.getFloat("ph7V",  DEFAULT_PH7_V);
  slope = prefs.getFloat("slope", DEFAULT_SLOPE);

  setupTemps();
  Serial.printf("Stored calibration: PH7_VOLTAGE = %.4f V, PH_SLOPE = %.4f V/pH\n", ph7V, slope);
  Serial.println(F("Commands: enterph | calph | exitph\n"));
}

void loop() {
  static unsigned long timepoint = millis();
  uint32_t now = millis();

  pollTemps(now);
  readSerialCommands();

  if (now - timepoint > READ_INTERVAL) {
    timepoint = now;

    bool  tOK     = compensationTemp(temperature);
    voltage       = readPHVoltage();
    pushVoltage(voltage);
    float rawPH   = ph(voltage);
    float compPH  = compensate(rawPH, temperature);

    char s1[10];
    fmtTemp(s1, sizeof(s1));

    Serial.printf("T=%s Tcomp=%.2f^C  V:%.3fV  pH=%.2f", s1, temperature, voltage, compPH);
    if (!tOK) Serial.print(" [TEMP_FAULT]");
    if (calMode) Serial.print(isStable() ? " [CAL][STABLE]" : " [CAL][settling]");
    Serial.println();
  }
}
