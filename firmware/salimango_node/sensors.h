// Hardware sensor drivers: sampling, validation, calibration sessions and
// calibration storage (NVS). Formulas are in sensor_math.*.
//
// NVS namespaces match the bench sketches so an existing bench calibration is
// picked up: "ph" (ph_cali_no_ten_k2), "ec_cal" (salimango_ec_calibration*),
// "do_cal" (DO_ESP32_DS18B20_WiFi4).
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Preferences.h>

#include "config.h"
#include "sensor_math.h"

// Status vocabulary shared with the database (supabase/migrations).
enum class SensorStatus : uint8_t { OK, DEGRADED, OVER_RANGE, FAULT, NOT_PRESENT };
const char *sensorStatusName(SensorStatus s);

// Messages returned by calibration actions (shown on Serial and in the dashboard).
struct CalMessage {
  char text[200];
};
void calSay(CalMessage &m, const char *fmt, ...);

// Common calibration bookkeeping (version counter + random id per save).
struct CalIdentity {
  bool calibrated = false;  // false = factory defaults in use
  bool invalidStored = false;
  uint32_t version = 0;
  uint32_t calId = 0;
};

// ----------------------------------------------------------- temperature ----
// DS18B20 on the shared 1-Wire bus (perfboard J3). Probe 1 is used for
// compensation; an optional probe 2 on the same bus is the fallback.
class TemperatureBus {
 public:
  void begin(uint8_t pin);
  void poll(uint32_t now);  // non-blocking conversions
  // Compensation temperature; false (and 25 °C) when no probe is valid.
  bool compensation(float &tempC) const;
  bool valid(uint8_t i) const { return i < 2 && valid_[i]; }
  float value(uint8_t i) const { return i < 2 ? t_[i] : NAN; }
  uint8_t found() const { return found_; }

 private:
  OneWire *bus_ = nullptr;
  DallasTemperature *ds_ = nullptr;
  DeviceAddress addr_[2];
  bool present_[2] = {false, false};
  bool valid_[2] = {false, false};
  float t_[2] = {NAN, NAN};
  uint8_t found_ = 0;
  uint32_t requestedAt_ = 0;
};

// ------------------------------------------------------------------- pH -----
struct PhReading {
  float ph;  // NaN when withheld
  SensorStatus status;
  float pinMv;
  float moduleV;
};

class PhSensor {
 public:
  void begin();
  PhReading read(float tempC, bool tempOk);  // ~100 ms (blocking ADC window)
  bool calActive() const { return calMode_; }
  bool calEnter(CalMessage &m);
  bool calCapture(float tempC, bool tempOk, CalMessage &m);
  bool calSave(CalMessage &m);
  bool calAbort(CalMessage &m);
  void toJson(JsonObject o) const;
  bool stable() const;

 private:
  void save();
  Preferences prefs_;
  CalIdentity id_;
  float ph7V_ = sm::PH_DEFAULT_PH7_V, slope_ = sm::PH_DEFAULT_SLOPE, calT_ = sm::PH_DEFAULT_CAL_T;
  uint8_t lastPoints_ = 0;
  float lastV_ = NAN;
  float hist_[PH_STABLE_COUNT];
  uint8_t histN_ = 0, histIdx_ = 0;
  bool calMode_ = false;
  sm::PhCalPoint pts_[sm::PH_BUFFER_COUNT];
};

// ------------------------------------------------------------------- EC -----
struct EcReading {
  float ecMsCm;    // at the measurement temperature (calibrated, uncompensated)
  float ec25MsCm;  // compensated to 25 °C (linear 1.85 %/°C)
  SensorStatus status;
  float mv;
};

class EcSensor {
 public:
  void begin();
  void sample(uint32_t now);  // call every loop: 40 ms ADC samples, 1 s windows
  EcReading read(float tempC, bool tempOk);
  bool calActive() const { return calMode_; }
  bool calEnter(CalMessage &m);
  bool calCapture(float tempC, bool tempOk, CalMessage &m);
  bool calSave(CalMessage &m);
  bool calAbort(CalMessage &m);
  void toJson(JsonObject o) const;
  bool stable() const;

 private:
  void save();
  Preferences prefs_;
  CalIdentity id_;
  // DFRobot "kvalue": software correction factors relative to the nominal
  // K = 1 cell (≈ 1.0). They are NOT the cell constant.
  float corrLow_ = 1.0f, corrHigh_ = 1.0f, corrActive_ = 1.0f;
  uint8_t stdMask_ = 0;  // bit0 = 1.413 captured, bit1 = 12.88 captured (last save)
  float buf_[EC_WINDOW];
  uint8_t idx_ = 0;
  uint32_t lastSample_ = 0;
  float mvAvg_ = NAN;
  float hist_[EC_STABLE_WINDOWS];
  uint8_t histN_ = 0, histIdx_ = 0;
  bool calMode_ = false;
  float pendLow_ = NAN, pendHigh_ = NAN;
};

// ------------------------------------------------------------------- DO -----
struct DoReading {
  float mgL;
  SensorStatus status;
  float mv;
};

class DoSensor {
 public:
  void begin();
  DoReading read(float tempC, bool tempOk);  // ~100 ms
  bool calActive() const { return calMode_; }
  bool calEnter(CalMessage &m);
  bool calCapture(float tempC, bool tempOk, CalMessage &m);
  bool calSave(CalMessage &m);
  bool calAbort(CalMessage &m);
  void toJson(JsonObject o) const;

 private:
  void save();
  Preferences prefs_;
  CalIdentity id_;
  sm::DoCal cal_ = {1, sm::DO_DEFAULT_V1_MV, sm::DO_DEFAULT_T1_C, 0, 0};
  float lastMv_ = NAN;
  bool calMode_ = false;
  uint8_t capCount_ = 0;
  float capV_[2], capT_[2];
};
