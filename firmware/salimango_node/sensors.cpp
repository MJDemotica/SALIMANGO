#include "sensors.h"

#include <stdarg.h>

const char *sensorStatusName(SensorStatus s) {
  switch (s) {
    case SensorStatus::OK: return "ok";
    case SensorStatus::DEGRADED: return "degraded";
    case SensorStatus::OVER_RANGE: return "over_range";
    case SensorStatus::FAULT: return "fault";
    default: return "not_present";
  }
}

void calSay(CalMessage &m, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(m.text, sizeof(m.text), fmt, ap);
  va_end(ap);
}

static uint32_t newCalId() { return esp_random() | 1u; }

static float round2(float v, float scale) { return isnan(v) ? v : roundf(v * scale) / scale; }

// Restores version/id keys for a stored calibration. Calibrations saved by the
// bench sketches have no version yet: they become version 1 with a fresh id.
static void loadIdentity(Preferences &p, CalIdentity &id) {
  id.calibrated = true;
  id.version = p.getUInt("ver", 0);
  id.calId = p.getUInt("calId", 0);
  if (id.version == 0 || id.calId == 0) {
    id.version = id.version ? id.version : 1;
    id.calId = newCalId();
    p.putUInt("ver", id.version);
    p.putUInt("calId", id.calId);
  }
}

static void bumpIdentity(Preferences &p, CalIdentity &id) {
  id.calibrated = true;
  id.invalidStored = false;
  id.version++;
  id.calId = newCalId();
  p.putUInt("ver", id.version);
  p.putUInt("calId", id.calId);
}

static void identityJson(JsonObject o, const CalIdentity &id) {
  o["state"] = id.calibrated ? "calibrated" : "default";
  o["version"] = id.calibrated ? id.version : 0;
  if (id.calibrated) o["cal_id"] = id.calId;
  if (id.invalidStored) o["stored_invalid"] = true;
}

// ----------------------------------------------------------- temperature ----
void TemperatureBus::begin(uint8_t pin) {
  bus_ = new OneWire(pin);
  ds_ = new DallasTemperature(bus_);
  ds_->begin();
  found_ = ds_->getDeviceCount();
  for (uint8_t i = 0; i < 2; i++) {
    present_[i] = ds_->getAddress(addr_[i], i);
    if (present_[i]) ds_->setResolution(addr_[i], 12);
  }
  Serial.printf("DS18B20 on GPIO %u: %u found\n", pin, found_);
  ds_->setWaitForConversion(false);
  ds_->requestTemperatures();
  requestedAt_ = millis();
}

void TemperatureBus::poll(uint32_t now) {
  if (now - requestedAt_ < 750) return;  // 12-bit conversion still running
  for (uint8_t i = 0; i < 2; i++) {
    if (!present_[i]) {
      valid_[i] = false;
      continue;
    }
    float v = ds_->getTempC(addr_[i]);
    // -127 = disconnected; 85.0 = power-on reset value (never a real pond reading)
    valid_[i] = (v != DEVICE_DISCONNECTED_C) && (v != 85.0f) && (v > -10.0f) && (v < 60.0f);
    t_[i] = valid_[i] ? v : NAN;
  }
  ds_->requestTemperatures();  // async: returns immediately
  requestedAt_ = now;
}

bool TemperatureBus::compensation(float &t) const {
  if (valid_[0]) { t = t_[0]; return true; }
  if (valid_[1]) { t = t_[1]; return true; }
  t = 25.0f;  // no compensation; flagged by the caller
  return false;
}

// ------------------------------------------------------------------- pH -----
void PhSensor::begin() {
  prefs_.begin("ph", false);
  if (prefs_.isKey("ph7V")) {
    float v = prefs_.getFloat("ph7V", sm::PH_DEFAULT_PH7_V);
    float s = prefs_.getFloat("slope", sm::PH_DEFAULT_SLOPE);
    float t = prefs_.getFloat("calT", sm::PH_DEFAULT_CAL_T);
    bool ok = s >= sm::PH_SLOPE_MIN && s <= sm::PH_SLOPE_MAX && v > 1.0f && v < 4.0f && t >= 0 && t <= 50;
    if (ok) {
      ph7V_ = v; slope_ = s; calT_ = t;
      lastPoints_ = prefs_.getUChar("pts", 0);
      loadIdentity(prefs_, id_);
    } else {
      id_.invalidStored = true;  // keep defaults, report it
    }
  }
  Serial.printf("pH calibration: %s  pH7 = %.4f V, slope = %.4f V/pH @ %.2f C\n",
                id_.calibrated ? "stored" : "DEFAULT", ph7V_, slope_, calT_);
}

PhReading PhSensor::read(float t, bool tOk) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < PH_SAMPLES; i++) {
    sum += analogReadMilliVolts(PIN_PH);  // eFuse-calibrated millivolts
    delay(10);
  }
  float pinMv = sum / (float)PH_SAMPLES;
  float v = pinMv / 1000.0f * PH_DIVIDER_RATIO;
  lastV_ = v;
  hist_[histIdx_] = v;
  histIdx_ = (histIdx_ + 1) % PH_STABLE_COUNT;
  if (histN_ < PH_STABLE_COUNT) histN_++;

  PhReading r = {NAN, SensorStatus::FAULT, pinMv, v};
  if (pinMv < PH_PIN_MIN_MV) return r;  // board unplugged: the divider pulls the pin to GND
  if (pinMv > PH_PIN_MAX_MV) {
    r.status = SensorStatus::OVER_RANGE;
    return r;
  }
  float ph = sm::phFromVoltage(v, t, ph7V_, slope_, calT_);
  if (!(ph >= 0.0f && ph <= 14.0f)) return r;
  r.ph = ph;
  r.status = tOk ? SensorStatus::OK : SensorStatus::DEGRADED;
  return r;
}

bool PhSensor::stable() const {
  if (histN_ < PH_STABLE_COUNT) return false;
  float mn = hist_[0], mx = hist_[0];
  for (uint8_t i = 1; i < PH_STABLE_COUNT; i++) { mn = min(mn, hist_[i]); mx = max(mx, hist_[i]); }
  return (mx - mn) <= PH_STABLE_TOL_V;
}

bool PhSensor::calEnter(CalMessage &m) {
  calMode_ = true;
  for (auto &p : pts_) p.captured = false;
  calSay(m, "pH calibration started. Put the probe and temperature sensor in a buffer "
            "(4.01, 6.86 or 9.18), wait until the reading is stable, then capture.");
  return true;
}

bool PhSensor::calCapture(float t, bool tOk, CalMessage &m) {
  if (!calMode_) { calSay(m, "Start pH calibration first."); return false; }
  if (!tOk) { calSay(m, "Rejected: no valid temperature reading. Check the DS18B20."); return false; }
  if (!stable()) { calSay(m, "Rejected: reading not stable yet. Wait, then capture again."); return false; }
  float est = sm::phFromVoltage(lastV_, t, ph7V_, slope_, calT_);
  int b = sm::phRecognizeBuffer(est);
  if (b < 0) {
    calSay(m, "Rejected: reading (pH %.2f, %.3f V) doesn't match 4.01, 6.86 or 9.18. "
              "Check the buffer, or set the board's offset pot so pH 7 reads about 2.50 V.", est, lastV_);
    return false;
  }
  pts_[b] = {true, lastV_, sm::phBufferAt(b, t), t};
  int n = pts_[0].captured + pts_[1].captured + pts_[2].captured;
  calSay(m, "Captured the %.2f buffer (= pH %.2f at %.2f C), %.3f V. %d buffer(s) so far. "
            "Rinse and capture the next buffer, or save.", sm::PH_BUFFER_NOMINAL[b], pts_[b].bufferPh, t, lastV_, n);
  return true;
}

bool PhSensor::calSave(CalMessage &m) {
  if (!calMode_) { calSay(m, "pH calibration is not active."); return false; }
  calMode_ = false;
  sm::PhFit f = sm::phFit(pts_, ph7V_, slope_, calT_);
  if (f.points == 0) { calSay(m, "No buffer captured; pH calibration unchanged."); return true; }
  if (!f.ok) {
    calSay(m, "pH calibration FAILED: slope %.4f V/pH is outside %.2f-%.2f. Check the buffers, rinse "
              "between them, or the probe may be worn. Nothing saved.", f.slope, sm::PH_SLOPE_MIN, sm::PH_SLOPE_MAX);
    return false;
  }
  ph7V_ = f.ph7V; slope_ = f.slope; calT_ = f.calT; lastPoints_ = f.points;
  save();
  calSay(m, "pH calibration saved (%d point%s): pH7 = %.4f V, slope = %.4f V/pH @ %.2f C.",
         f.points, f.points > 1 ? "s" : "", ph7V_, slope_, calT_);
  return true;
}

bool PhSensor::calAbort(CalMessage &m) {
  bool was = calMode_;
  calMode_ = false;
  calSay(m, was ? "pH calibration cancelled; nothing saved." : "pH calibration was not active.");
  return was;
}

void PhSensor::save() {
  prefs_.putFloat("ph7V", ph7V_);
  prefs_.putFloat("slope", slope_);
  prefs_.putFloat("calT", calT_);
  prefs_.putUChar("pts", lastPoints_);
  bumpIdentity(prefs_, id_);
}

void PhSensor::toJson(JsonObject o) const {
  identityJson(o, id_);
  JsonObject p = o["params"].to<JsonObject>();
  p["ph7_v"] = round2(ph7V_, 10000);
  p["slope_v_per_ph"] = round2(slope_, 10000);
  p["cal_temp_c"] = round2(calT_, 100);
  if (lastPoints_) p["points"] = lastPoints_;
  p["divider_ratio"] = round2(PH_DIVIDER_RATIO, 10000);
  if (!calMode_) { o["session"] = nullptr; return; }
  JsonObject s = o["session"].to<JsonObject>();
  s["stable"] = stable();
  s["module_v"] = round2(lastV_, 1000);
  JsonArray a = s["points"].to<JsonArray>();
  for (int i = 0; i < sm::PH_BUFFER_COUNT; i++) {
    if (!pts_[i].captured) continue;
    JsonObject q = a.add<JsonObject>();
    q["buffer"] = sm::PH_BUFFER_NOMINAL[i];
    q["buffer_ph"] = round2(pts_[i].bufferPh, 100);
    q["module_v"] = round2(pts_[i].moduleV, 1000);
    q["temp_c"] = round2(pts_[i].tempC, 100);
  }
}

// ------------------------------------------------------------------- EC -----
void EcSensor::begin() {
  pinMode(PIN_EC, INPUT);
  prefs_.begin("ec_cal", false);  // same namespace as the bench calibration sketches
  if (prefs_.isKey("kLow") || prefs_.isKey("kHigh")) {
    float lo = prefs_.getFloat("kLow", 1.0f), hi = prefs_.getFloat("kHigh", 1.0f);
    bool ok = lo >= sm::EC_CORR_MIN && lo <= sm::EC_CORR_MAX && hi >= sm::EC_CORR_MIN && hi <= sm::EC_CORR_MAX;
    if (!ok) {
      id_.invalidStored = true;
    } else if (!(lo == 1.0f && hi == 1.0f)) {  // 1.0 / 1.0 is the "reset" (uncalibrated) state
      corrLow_ = lo; corrHigh_ = hi;
      stdMask_ = prefs_.getUChar("std", 0);
      loadIdentity(prefs_, id_);
    }
  }
  corrActive_ = corrLow_;
  Serial.printf("EC calibration (K = %.1f): %s  corrLow = %.4f, corrHigh = %.4f\n", sm::EC_CELL_CONSTANT_K,
                id_.calibrated ? "stored" : "DEFAULT", corrLow_, corrHigh_);
}

static float trimmedMean(const float *src, uint8_t n, uint8_t trim) {
  float a[EC_WINDOW];
  memcpy(a, src, n * sizeof(float));
  for (uint8_t i = 1; i < n; i++) {  // insertion sort (n = 25)
    float v = a[i];
    int8_t j = i - 1;
    while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
    a[j + 1] = v;
  }
  float s = 0;
  for (uint8_t i = trim; i < n - trim; i++) s += a[i];
  return s / (n - 2 * trim);
}

void EcSensor::sample(uint32_t now) {
  if (now - lastSample_ < EC_SAMPLE_MS) return;
  lastSample_ = now;
  buf_[idx_++] = (float)analogReadMilliVolts(PIN_EC);
  if (idx_ >= EC_WINDOW) {
    idx_ = 0;
    mvAvg_ = trimmedMean(buf_, EC_WINDOW, EC_TRIM);
    hist_[histIdx_] = mvAvg_;
    histIdx_ = (histIdx_ + 1) % EC_STABLE_WINDOWS;
    if (histN_ < EC_STABLE_WINDOWS) histN_++;
  }
}

bool EcSensor::stable() const {
  if (histN_ < EC_STABLE_WINDOWS) return false;
  float mn = hist_[0], mx = hist_[0], sum = 0;
  for (uint8_t i = 0; i < EC_STABLE_WINDOWS; i++) {
    mn = min(mn, hist_[i]); mx = max(mx, hist_[i]); sum += hist_[i];
  }
  return (mx - mn) <= max(EC_STABLE_TOL * sum / EC_STABLE_WINDOWS, 5.0f);
}

EcReading EcSensor::read(float t, bool tOk) {
  EcReading r = {NAN, NAN, SensorStatus::FAULT, mvAvg_};
  if (isnan(mvAvg_)) return r;  // first 1 s window not complete yet
  float raw = sm::ecRawMsCm(mvAvg_);
  if (mvAvg_ > sm::EC_ADC_CLIP_MV || raw > sm::EC_RAW_MAX) {
    r.status = SensorStatus::OVER_RANGE;  // beyond the K = 1 probe / ESP32 ADC range
    return r;
  }
  float trial = raw * corrActive_;  // auto-range between the two correction factors, with hysteresis
  if (trial > 2.5f) corrActive_ = corrHigh_;
  else if (trial < 2.0f) corrActive_ = corrLow_;
  r.ecMsCm = raw * corrActive_;
  r.ec25MsCm = r.ecMsCm / sm::ecTempFactor(t);
  r.status = tOk ? SensorStatus::OK : SensorStatus::DEGRADED;
  return r;
}

bool EcSensor::calEnter(CalMessage &m) {
  calMode_ = true;
  pendLow_ = pendHigh_ = NAN;
  calSay(m, "EC calibration started (K = 1 probe). Put the EC probe and temperature sensor in the "
            "1.413 or 12.88 mS/cm standard, wait until stable, then capture.");
  return true;
}

bool EcSensor::calCapture(float t, bool tOk, CalMessage &m) {
  if (!calMode_) { calSay(m, "Start EC calibration first."); return false; }
  if (!tOk) { calSay(m, "Rejected: no valid DS18B20 reading. Fix the 1-Wire bus first."); return false; }
  if (isnan(mvAvg_) || !stable()) { calSay(m, "Rejected: reading not stable yet. Wait, then capture again."); return false; }
  float raw = sm::ecRawMsCm(mvAvg_);
  int which = sm::ecRecognizeStandard(raw, t);
  if (which < 0) {
    calSay(m, "Rejected: raw %.3f mS/cm at %.2f C matches neither 1.413 nor 12.88 mS/cm. "
              "Check the solution, probe immersion and wiring.", raw, t);
    return false;
  }
  float std = which == 0 ? sm::EC_STD_LOW : sm::EC_STD_HIGH;
  float k = sm::ecCorrectionFactor(mvAvg_, std, t);
  if (k < sm::EC_CORR_MIN || k > sm::EC_CORR_MAX) {
    calSay(m, "Rejected: correction %.4f outside %.1f-%.1f. Probe fouled, wrong standard, or not a K = 1 probe.",
           k, sm::EC_CORR_MIN, sm::EC_CORR_MAX);
    return false;
  }
  if (which == 0) pendLow_ = k; else pendHigh_ = k;
  calSay(m, "Captured %.3f mS/cm standard at %.2f C: %.1f mV, correction %.4f (pending; save to keep).",
         std, t, mvAvg_, k);
  return true;
}

bool EcSensor::calSave(CalMessage &m) {
  if (!calMode_) { calSay(m, "EC calibration is not active."); return false; }
  calMode_ = false;
  if (isnan(pendLow_) && isnan(pendHigh_)) { calSay(m, "No standard captured; EC calibration unchanged."); return true; }
  bool one = isnan(pendLow_) || isnan(pendHigh_);
  if (!isnan(pendLow_)) corrLow_ = pendLow_;
  if (!isnan(pendHigh_)) corrHigh_ = pendHigh_;
  stdMask_ = (isnan(pendLow_) ? 0 : 1) | (isnan(pendHigh_) ? 0 : 2);
  corrActive_ = corrLow_;
  pendLow_ = pendHigh_ = NAN;
  save();
  calSay(m, "EC calibration saved: corrLow = %.4f, corrHigh = %.4f (K = 1).%s", corrLow_, corrHigh_,
         one ? " Only one standard captured; the other correction keeps its previous value." : "");
  return true;
}

bool EcSensor::calAbort(CalMessage &m) {
  bool was = calMode_;
  calMode_ = false;
  pendLow_ = pendHigh_ = NAN;
  calSay(m, was ? "EC calibration cancelled; nothing saved." : "EC calibration was not active.");
  return was;
}

void EcSensor::save() {
  prefs_.putFloat("kLow", corrLow_);
  prefs_.putFloat("kHigh", corrHigh_);
  prefs_.putUChar("std", stdMask_);
  bumpIdentity(prefs_, id_);
}

void EcSensor::toJson(JsonObject o) const {
  identityJson(o, id_);
  JsonObject p = o["params"].to<JsonObject>();
  p["cell_constant_k"] = sm::EC_CELL_CONSTANT_K;
  p["corr_low"] = round2(corrLow_, 10000);
  p["corr_high"] = round2(corrHigh_, 10000);
  p["temp_coef_per_c"] = sm::EC_TEMP_COEF;
  if (stdMask_) {
    JsonArray s = p["standards_ms_cm"].to<JsonArray>();
    if (stdMask_ & 1) s.add(sm::EC_STD_LOW);
    if (stdMask_ & 2) s.add(sm::EC_STD_HIGH);
  }
  if (!calMode_) { o["session"] = nullptr; return; }
  JsonObject s = o["session"].to<JsonObject>();
  s["stable"] = stable();
  s["mv"] = round2(mvAvg_, 10);
  s["raw_ms_cm"] = isnan(mvAvg_) ? NAN : round2(sm::ecRawMsCm(mvAvg_), 1000);
  if (isnan(pendLow_)) s["pending_low"] = nullptr; else s["pending_low"] = round2(pendLow_, 10000);
  if (isnan(pendHigh_)) s["pending_high"] = nullptr; else s["pending_high"] = round2(pendHigh_, 10000);
}

// ------------------------------------------------------------------- DO -----
static float readDoMv() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < DO_SAMPLES; i++) {
    sum += analogReadMilliVolts(PIN_DO);
    delay(10);
  }
  return sum / (float)DO_SAMPLES;  // DO board outputs 0-3.0 V: no divider
}

void DoSensor::begin() {
  prefs_.begin("do_cal", false);
  uint8_t pts = prefs_.getUChar("pts", 0);
  if (pts == 1 || pts == 2) {
    sm::DoCal c = {pts, prefs_.getFloat("v1", 0), prefs_.getFloat("t1", 0), prefs_.getFloat("v2", 0),
                   prefs_.getFloat("t2", 0)};
    bool ok = c.v1 >= sm::DO_MIN_CAL_MV && c.v1 <= sm::DO_MAX_CAL_MV && c.t1 >= 0 && c.t1 <= 40 &&
              (pts == 1 || (c.v2 >= sm::DO_MIN_CAL_MV && c.v2 <= sm::DO_MAX_CAL_MV &&
                            fabsf(c.t1 - c.t2) >= sm::DO_MIN_TWO_POINT_C));
    if (ok) {
      cal_ = c;
      loadIdentity(prefs_, id_);
    } else {
      id_.invalidStored = true;
    }
  }
  Serial.printf("DO calibration: %s  %u-point V1 = %.0f mV @ %.2f C\n", id_.calibrated ? "stored" : "DEFAULT",
                cal_.points, cal_.v1, cal_.t1);
}

DoReading DoSensor::read(float t, bool tOk) {
  float mv = readDoMv();
  lastMv_ = mv;
  DoReading r = {NAN, SensorStatus::FAULT, mv};
  if (mv > DO_PIN_MAX_MV) {
    r.status = SensorStatus::OVER_RANGE;
    return r;
  }
  float d = sm::doMgL(mv, t, cal_);
  if (isnan(d) || d < 0 || d >= 50) return r;
  r.mgL = d;
  r.status = tOk ? SensorStatus::OK : SensorStatus::DEGRADED;
  return r;
}

bool DoSensor::calEnter(CalMessage &m) {
  calMode_ = true;
  capCount_ = 0;
  calSay(m, "DO calibration started (SEN0237-A). Put the probe in saturated conditions (wet probe held in "
            "air, or water aerated for several minutes). Wait for the voltage to stop drifting, then capture. "
            "For two-point, capture again in a sample at least 5 C warmer or colder.");
  return true;
}

bool DoSensor::calCapture(float t, bool tOk, CalMessage &m) {
  if (!calMode_) { calSay(m, "Start DO calibration first."); return false; }
  if (!tOk) { calSay(m, "Rejected: temperature sensor fault (calibration needs a real temperature)."); return false; }
  float v = readDoMv();
  if (v < sm::DO_MIN_CAL_MV || v > sm::DO_MAX_CAL_MV) {
    calSay(m, "Rejected: %.0f mV is out of range (%.0f-%.0f mV). Check the probe and wiring.", v,
           sm::DO_MIN_CAL_MV, sm::DO_MAX_CAL_MV);
    return false;
  }
  if (capCount_ == 2) capCount_ = 0;  // third capture starts over
  if (capCount_ == 1 && fabsf(t - capT_[0]) < sm::DO_MIN_TWO_POINT_C) {
    calSay(m, "Point 2 rejected: %.2f C is within %.0f C of point 1 (%.2f C). Use a warmer/colder sample, "
              "or save single-point.", t, sm::DO_MIN_TWO_POINT_C, capT_[0]);
    return false;
  }
  capV_[capCount_] = v;
  capT_[capCount_] = t;
  capCount_++;
  calSay(m, "Point %u captured: %.0f mV at %.2f C. %s", capCount_, v, t,
         capCount_ == 1 ? "Save for single-point, or capture a second sample for two-point." : "Two-point ready; save.");
  return true;
}

bool DoSensor::calSave(CalMessage &m) {
  if (!calMode_) { calSay(m, "DO calibration is not active."); return false; }
  calMode_ = false;
  if (capCount_ == 0) { calSay(m, "No point captured; DO calibration unchanged."); return true; }
  if (capCount_ == 1) {
    cal_ = {1, capV_[0], capT_[0], 0, 0};
  } else {
    uint8_t hi = (capT_[0] >= capT_[1]) ? 0 : 1;  // point 1 = higher temperature
    uint8_t lo = 1 - hi;
    cal_ = {2, capV_[hi], capT_[hi], capV_[lo], capT_[lo]};
  }
  save();
  if (cal_.points == 2)
    calSay(m, "DO two-point calibration saved: %.0f mV @ %.2f C, %.0f mV @ %.2f C.", cal_.v1, cal_.t1, cal_.v2, cal_.t2);
  else
    calSay(m, "DO single-point calibration saved: %.0f mV @ %.2f C.", cal_.v1, cal_.t1);
  return true;
}

bool DoSensor::calAbort(CalMessage &m) {
  bool was = calMode_;
  calMode_ = false;
  capCount_ = 0;
  calSay(m, was ? "DO calibration cancelled; nothing saved." : "DO calibration was not active.");
  return was;
}

void DoSensor::save() {
  prefs_.putUChar("pts", cal_.points);
  prefs_.putFloat("v1", cal_.v1);
  prefs_.putFloat("t1", cal_.t1);
  prefs_.putFloat("v2", cal_.v2);
  prefs_.putFloat("t2", cal_.t2);
  bumpIdentity(prefs_, id_);
}

void DoSensor::toJson(JsonObject o) const {
  identityJson(o, id_);
  JsonObject p = o["params"].to<JsonObject>();
  p["points"] = cal_.points;
  p["v1_mv"] = round2(cal_.v1, 10);
  p["t1_c"] = round2(cal_.t1, 100);
  if (cal_.points == 2) {
    p["v2_mv"] = round2(cal_.v2, 10);
    p["t2_c"] = round2(cal_.t2, 100);
  }
  if (!calMode_) { o["session"] = nullptr; return; }
  JsonObject s = o["session"].to<JsonObject>();
  s["mv"] = round2(lastMv_, 10);
  JsonArray a = s["points"].to<JsonArray>();
  for (uint8_t i = 0; i < capCount_; i++) {
    JsonObject q = a.add<JsonObject>();
    q["mv"] = round2(capV_[i], 10);
    q["temp_c"] = round2(capT_[i], 100);
  }
}
