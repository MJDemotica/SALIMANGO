/*
 * pH_ESP32_DS18B20.ino
 *
 * ESP32 port of the pH sensing code, with one DS18B20 for temperature.
 *
 * pH conversion (unchanged from the original):
 *   pH = 7.0 + (2.5 - V) / 0.18
 *   compensated pH = pH + (T - 25) * 0.03
 * To calibrate, adjust PH7_VOLTAGE (voltage in pH 7 buffer) and
 * PH_SLOPE (volts per pH unit) using the V reading printed on Serial.
 *
 * Temperature code: S.A.L.I.M.A.N.G.O. DS18B20 handling
 *   - non-blocking (asynchronous) conversions
 *   - invalid readings rejected (-127 disconnected, 85.0 power-on value, out of range)
 *   - falls back to 25 °C (flagged [TEMP_FAULT]) if the sensor faults
 *
 * Required libraries (Arduino Library Manager):
 *   - OneWire
 *   - DallasTemperature
 *
 * Serial Monitor: 115200 baud
 */

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>

// ---------------- Pins ----------------
#define PH_PIN        35   // ADC1_CH7, input-only. Use ADC1 pins only: ADC2 doesn't work while Wi-Fi is on
constexpr uint8_t PIN_ONEWIRE = 19;   // 1-Wire bus, external 4.7 kOhm pull-up to 3.3 V

// ---------------- pH constants ----------------
#define PH7_VOLTAGE   2.5      // module output (V) at pH 7  -> adjust during calibration
#define PH_SLOPE      0.18     // V per pH unit              -> adjust during calibration
#define PH_TEMP_COEF  0.03     // pH units per °C (original code's compensation)
#define DIVIDER_RATIO 1.5      // 10k (top) + 20k (bottom) divider: Vmodule = Vpin * 1.5
                               // set to 1.0 if the module output goes straight to the ESP32
#define PH_SAMPLES    10
#define READ_INTERVAL 3000UL   // ms

// ---------------- Temperature constants ----------------
constexpr uint32_t T_CONV_MS    = 750;   // DS18B20 12-bit conversion time

// ============================ Objects ==================================
OneWire           oneWire(PIN_ONEWIRE);
DallasTemperature ds(&oneWire);

// ======================== Temperature state ============================
DeviceAddress tAddr;
bool     tPresent = false;
bool     tValid   = false;
float    tC       = NAN;
uint32_t tReqAt   = 0;

float temperature = 25.0;   // compensation temperature actually used

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
  return 7.0 + ((PH7_VOLTAGE - voltage) / PH_SLOPE);
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

// =========================== setup / loop ==============================

void setup() {
  Serial.begin(115200);
  delay(500);

  // ADC: 12-bit, 11 dB attenuation (~0.15 to ~3.1 V usable range)
  analogReadResolution(12);
  analogSetPinAttenuation(PH_PIN, ADC_11db);

  setupTemps();
  Serial.println();
}

void loop() {
  static unsigned long timepoint = millis();
  uint32_t now = millis();

  pollTemps(now);

  if (now - timepoint > READ_INTERVAL) {
    timepoint = now;

    bool  tOK     = compensationTemp(temperature);
    float voltage = readPHVoltage();
    float rawPH   = ph(voltage);
    float compPH  = rawPH + (temperature - 25.0) * PH_TEMP_COEF;

    char s1[10];
    fmtTemp(s1, sizeof(s1));

    Serial.printf("T=%s Tcomp=%.2f^C  V:%.3fV  pH=%.2f", s1, temperature, voltage, compPH);
    if (!tOK) Serial.print(" [TEMP_FAULT]");
    Serial.println();
  }
}
