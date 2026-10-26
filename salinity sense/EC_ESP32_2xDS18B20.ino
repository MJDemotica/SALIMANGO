/*
 * EC_ESP32_2xDS18B20.ino
 *
 * ESP32 port of the DFRobot_EC sample code for
 * Gravity: Analog Electrical Conductivity Sensor / Meter Kit V2 (K=1.0), SKU: DFR0300,
 * with two DS18B20 temperature sensors on one OneWire bus.
 *
 * Based on DFRobot_EC (https://github.com/DFRobot/DFRobot_EC), LGPL.
 * The EC math and calibration logic are reproduced here so they work on ESP32:
 * the original library's EEPROM writes don't persist on ESP32 (no EEPROM.commit()),
 * so the K values are stored with Preferences (NVS flash) instead.
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
 *
 * Temperature sensors:
 *   Sensor index 0 = water probe (used for EC temperature compensation)
 *   Sensor index 1 = second sensor (e.g. air / enclosure), reported only
 *   DS18B20s are ordered by their ROM address, so check which physical probe
 *   is which at boot (see the addresses printed in setup) and swap
 *   WATER_INDEX / AIR_INDEX if needed.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ---------------- Pins ----------------
#define EC_PIN        34   // ADC1_CH6 (input-only). Use ADC1 pins only: ADC2 doesn't work while Wi-Fi is on
#define ONE_WIRE_PIN  4    // DQ of both DS18B20s, 4.7k pull-up to 3.3V

// ---------------- Sensor order ----------------
#define WATER_INDEX   0
#define AIR_INDEX     1

// ---------------- EC constants (same as DFRobot_EC library) ----------------
#define RES2          820.0
#define ECREF         200.0
#define TEMP_COEF     0.0185   // temperature compensation coefficient used by DFRobot
#define ADC_SAMPLES   32       // averaging to reduce ESP32 ADC noise
#define READ_INTERVAL 1000UL   // ms

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature tempSensors(&oneWire);
DeviceAddress waterAddr, airAddr;
bool haveWater = false, haveAir = false;

Preferences prefs;

float kvalue = 1.0, kvalueLow = 1.0, kvalueHigh = 1.0;
float voltage = 0, rawEC = 0, ecValue = 0;
float waterTemp = 25.0;   // fallback for compensation if the probe is missing
float airTemp = NAN;

bool calibrationMode = false;
bool calibrated = false;
String cmdBuffer = "";

// ---------------------------------------------------------------------------

void printAddress(const DeviceAddress addr) {
  for (uint8_t i = 0; i < 8; i++) {
    if (addr[i] < 16) Serial.print("0");
    Serial.print(addr[i], HEX);
  }
}

// Average several readings; analogReadMilliVolts() uses the ESP32's factory ADC calibration
float readVoltage_mV() {
  uint32_t sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogReadMilliVolts(EC_PIN);
    delayMicroseconds(200);
  }
  return (float)sum / ADC_SAMPLES;
}

// Reads the results of the previous conversion, then starts a new one (non-blocking)
void readTemperatures() {
  if (haveWater) {
    float t = tempSensors.getTempC(waterAddr);
    if (t != DEVICE_DISCONNECTED_C) {
      waterTemp = t;
    } else {
      Serial.println("WARNING: water temp sensor read failed, using last value");
    }
  }
  if (haveAir) {
    float t = tempSensors.getTempC(airAddr);
    airTemp = (t != DEVICE_DISCONNECTED_C) ? t : NAN;
  }
  tempSensors.requestTemperatures();
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
      compSolution = 1.413 * (1.0 + TEMP_COEF * (waterTemp - 25.0));
      isLow = true;
    } else if (rawEC > 9.0 && rawEC < 16.8) {  // 12.88 mS/cm buffer
      compSolution = 12.88 * (1.0 + TEMP_COEF * (waterTemp - 25.0));
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

// ---------------------------------------------------------------------------

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

  // Temperature sensors
  tempSensors.begin();
  tempSensors.setWaitForConversion(false);   // non-blocking conversions
  int count = tempSensors.getDeviceCount();
  Serial.print("DS18B20 sensors found: ");
  Serial.println(count);

  haveWater = tempSensors.getAddress(waterAddr, WATER_INDEX);
  haveAir   = tempSensors.getAddress(airAddr, AIR_INDEX);

  if (haveWater) {
    tempSensors.setResolution(waterAddr, 12);
    Serial.print("  Water probe (index "); Serial.print(WATER_INDEX); Serial.print("): ");
    printAddress(waterAddr); Serial.println();
  } else {
    Serial.println("  WARNING: water probe not found, compensating at 25.0 C");
  }
  if (haveAir) {
    tempSensors.setResolution(airAddr, 12);
    Serial.print("  Air probe   (index "); Serial.print(AIR_INDEX); Serial.print("): ");
    printAddress(airAddr); Serial.println();
  } else {
    Serial.println("  WARNING: second temperature sensor not found");
  }

  Serial.print("kvalueLow = ");  Serial.print(kvalueLow, 4);
  Serial.print("   kvalueHigh = "); Serial.println(kvalueHigh, 4);
  Serial.println();

  tempSensors.requestTemperatures();   // start the first conversion
}

void loop() {
  static unsigned long timepoint = millis();

  readSerialCommands();

  if (millis() - timepoint > READ_INTERVAL) {
    timepoint = millis();

    readTemperatures();
    voltage = readVoltage_mV();
    ecValue = readEC(voltage, waterTemp);

    Serial.print("Water:");
    Serial.print(waterTemp, 2);
    Serial.print("^C  Air:");
    if (isnan(airTemp)) Serial.print("--");
    else Serial.print(airTemp, 2);
    Serial.print("^C  V:");
    Serial.print(voltage, 0);
    Serial.print("mV  EC:");
    Serial.print(ecValue, 3);
    Serial.println("ms/cm");
  }
}
