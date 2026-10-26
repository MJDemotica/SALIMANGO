/*
 * EC_ESP32_DS18B20.ino
 *
 * ESP32 port of the DFRobot_EC sample code for
 * Gravity: Analog Electrical Conductivity Sensor / Meter Kit V2 (K=1.0), SKU: DFR0300,
 * with one DS18B20 temperature sensor for automatic temperature compensation.
 *
 * Based on DFRobot_EC (https://github.com/DFRobot/DFRobot_EC), LGPL.
 * The EC math and calibration logic are reproduced here so they work on ESP32:
 * the original library's EEPROM writes don't persist on ESP32 (no EEPROM.commit()),
 * so the K values are stored with Preferences (NVS flash) instead.
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
 * Serial Monitor: 115200 baud, line ending "Newline" (or "Both NL & CR")
 * Serial Commands:
 *   enterec -> enter the calibration mode
 *   calec   -> calibrate with the standard buffer solution
 *              (1413 us/cm and 12.88 ms/cm are recognized automatically)
 *   exitec  -> save the calibrated parameters and exit calibration mode
 *   kvalue  -> print the stored K values
 */

#include <Arduino.h>
#include <Preferences.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>

// ---------------- Pins ----------------
#define EC_PIN        34   // ADC1_CH6, input-only. Use ADC1 pins only: ADC2 doesn't work while Wi-Fi is on
constexpr uint8_t PIN_ONEWIRE = 19;   // 1-Wire bus, external 4.7 kOhm pull-up to 3.3 V

// ---------------- EC constants (same as DFRobot_EC library) ----------------
#define RES2          820.0
#define ECREF         200.0
#define TEMP_COEF     0.0185   // temperature compensation coefficient used by DFRobot
#define ADC_SAMPLES   32       // averaging to reduce ESP32 ADC noise
#define READ_INTERVAL 1000UL   // ms

// ---------------- Temperature constants ----------------
constexpr uint32_t T_CONV_MS    = 750;   // DS18B20 12-bit conversion time

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

// =========================== EC state ==================================
float kvalue = 1.0, kvalueLow = 1.0, kvalueHigh = 1.0;
float voltage = 0, rawEC = 0, ecValue = 0;
float temperature = 25.0;   // compensation temperature actually used

bool calibrationMode = false;
bool calibrated = false;
String cmdBuffer = "";

// ======================== Temperature helpers ==========================

// Uses the DS18B20 mounted beside the EC cell; falls back to 25 °C if it faults.
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

// =========================== EC functions ==============================

// Average several readings; analogReadMilliVolts() uses the ESP32's factory ADC calibration
float readVoltage_mV() {
  uint32_t sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogReadMilliVolts(EC_PIN);
    delayMicroseconds(200);
  }
  return (float)sum / ADC_SAMPLES;
}

// Same math as DFRobot_EC::readEC(); voltage in mV, result in mS/cm
float readEC(float voltage_mV, float temperature) {
  rawEC = 1000.0 * voltage_mV / RES2 / ECREF;
  float valueTemp = rawEC * kvalue;
  // automatic range shift: first range (0,2), second range (2,20)
  if (valueTemp > 2.5) {
    kvalue = kvalueHigh;
  } else if (valueTemp < 2.0) {
    kvalue = kvalueLow;
  }
  float value = rawEC * kvalue;
  value = value / (1.0 + TEMP_COEF * (temperature - 25.0));   // temperature compensation
  return value;
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;

  if (cmd == "ENTEREC") {
    if (!calibrationMode) {
      calibrationMode = true;
      calibrated = false;
      Serial.println();
      Serial.println(">>>Enter EC Calibration Mode<<<");
      Serial.println(">>>Please put the probe into the 1413us/cm or 12.88ms/cm buffer solution<<<");
      Serial.println();
    }
  }
  else if (cmd == "CALEC") {
    if (!calibrationMode) {
      Serial.println(">>>Type enterec first<<<");
      return;
    }
    float compSolution;
    bool isLow;
    if (rawEC > 0.9 && rawEC < 1.9) {          // 1.413 mS/cm buffer
      compSolution = 1.413 * (1.0 + TEMP_COEF * (temperature - 25.0));
      isLow = true;
    } else if (rawEC > 9.0 && rawEC < 16.8) {  // 12.88 mS/cm buffer
      compSolution = 12.88 * (1.0 + TEMP_COEF * (temperature - 25.0));
      isLow = false;
    } else {
      Serial.println();
      Serial.print(">>>Buffer Solution Error, Try Again<<<  (rawEC = ");
      Serial.print(rawEC, 3);
      Serial.println(")");
      Serial.println();
      return;
    }

    float kTemp = RES2 * ECREF * compSolution / 1000.0 / voltage;
    if (kTemp > 0.5 && kTemp < 1.5) {
      if (isLow) kvalueLow = kTemp;
      else       kvalueHigh = kTemp;
      calibrated = true;
      Serial.println();
      Serial.print(isLow ? ">>>Low range (1.413) " : ">>>High range (12.88) ");
      Serial.print("calibration successful, K = ");
      Serial.print(kTemp, 4);
      Serial.println(". Type exitec to save and exit<<<");
      Serial.println();
    } else {
      Serial.println();
      Serial.print(">>>Failed, computed K out of range (");
      Serial.print(kTemp, 4);
      Serial.println("). Check probe/wiring and try again<<<");
      Serial.println();
    }
  }
  else if (cmd == "EXITEC") {
    if (!calibrationMode) return;
    if (calibrated) {
      prefs.putFloat("kLow", kvalueLow);
      prefs.putFloat("kHigh", kvalueHigh);
      Serial.println();
      Serial.println(">>>Calibration Successful, parameters saved<<<");
    } else {
      Serial.println();
      Serial.println(">>>Calibration Failed, nothing saved<<<");
    }
    Serial.println(">>>Exit EC Calibration Mode<<<");
    Serial.println();
    calibrationMode = false;
    calibrated = false;
  }
  else if (cmd == "KVALUE") {
    Serial.print("kvalueLow = ");
    Serial.print(kvalueLow, 4);
    Serial.print("   kvalueHigh = ");
    Serial.println(kvalueHigh, 4);
  }
}

void readSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdBuffer.length() > 0) {
        handleCommand(cmdBuffer);
        cmdBuffer = "";
      }
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
  analogSetPinAttenuation(EC_PIN, ADC_11db);

  // Load K values from flash (default 1.0 if never calibrated)
  prefs.begin("ec", false);
  kvalueLow  = prefs.getFloat("kLow", 1.0);
  kvalueHigh = prefs.getFloat("kHigh", 1.0);
  kvalue = kvalueLow;

  setupTemps();

  Serial.print("kvalueLow = ");  Serial.print(kvalueLow, 4);
  Serial.print("   kvalueHigh = "); Serial.println(kvalueHigh, 4);
  Serial.println();
}

void loop() {
  static unsigned long timepoint = millis();
  uint32_t now = millis();

  pollTemps(now);
  readSerialCommands();

  if (now - timepoint > READ_INTERVAL) {
    timepoint = now;

    bool tOK = compensationTemp(temperature);
    voltage = readVoltage_mV();
    ecValue = readEC(voltage, temperature);

    char s1[10];
    fmtTemp(s1, sizeof(s1));

    Serial.printf("T=%s Tcomp=%.2f^C  V:%.0fmV  EC:%.3fms/cm", s1, temperature, voltage, ecValue);
    if (!tOK) Serial.print(" [TEMP_FAULT]");
    Serial.println();
  }
}
