/*
  S.A.L.I.M.A.N.G.O. float node — ESP32 DevKit 38-pin (ESP32-WROOM-32)

  Sensors : pH (E-201-C + PH-4502C), EC (DFRobot DFR0300, K = 1),
            DO (DFRobot SEN0237-A), DS18B20 temperature
  Actuators: K1 air pump 1, K5 air pump 2, K3 bilge pump 1, K4 bilge pump 2
  Link    : direct Wi-Fi -> Supabase (no LoRa, no house bridge in this phase)
  Pins    : config.h (perfboard_build_guide.pdf)

  Control (all decisions are local; the network is never required):
    Mode   : the M-O-A selector on the box decides in M and O. In A, the
             dashboard may request AUTO / MANUAL / OFF. Boot -> AUTO.
             MANUAL reverts to AUTO after 5 min without a manual command.
    AUTO   : low DO -> both air pumps (K1, then K5 5 s later), off together;
             low salinity -> both bilge pumps (K3, then K4 2 s later), with
             hysteresis, max run, rest and lockout (salinity_rev.md).
    MANUAL : relays as commanded from the dashboard (K1/K5/K3/K4).
    OFF    : all relays off.
    DO emergency (valid DO < 3.0 mg/L, 3 readings): both air pumps forced ON
             in every mode, including OFF.

  Serial (115200, newline): help | status | enterph calph exitph abortph |
          enterec calec exitec abortec | enterdo caldo exitdo abortdo | clearlockout

  Libraries: OneWire, DallasTemperature, ArduinoJson 7 (Library Manager).
  Copy secrets.example.h to secrets.h before building.
*/

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_system.h>

#include "cloud.h"
#include "config.h"
#include "control_logic.h"
#include "sensor_math.h"
#include "sensors.h"

// ---------------------------------------------------------------- objects ---
TemperatureBus temps;
PhSensor phSensor;
EcSensor ecSensor;
DoSensor doSensor;
ModeManager modes(MANUAL_TIMEOUT_MS);
AerationController aeration;
BilgeController bilge;
RelaySequencer relays(AIR_STAGGER_MS, BILGE_STAGGER_MS);
Preferences settingsPrefs;

const uint8_t RELAY_PINS[RELAY_COUNT] = {PIN_RELAY_K1, PIN_RELAY_K5, PIN_RELAY_K3, PIN_RELAY_K4};
const char *const RELAY_KEYS[RELAY_COUNT] = {"k1", "k5", "k3", "k4"};

// ------------------------------------------------------------------ state ---
cloud::Settings settings = {0, DEFAULT_SAL_MIN_PPT, DEFAULT_SAL_MAX_PPT, DEFAULT_SAL_HYST_PPT,
                            DEFAULT_BILGE_MAX_RUN_MIN, DEFAULT_BILGE_MIN_OFF_MIN, DEFAULT_BILGE_MAX_FAILS};

Selector selector = Selector::AUTO, selectorRaw = Selector::AUTO;
uint32_t selectorChangedAt = 0;
ModeStatus modeStatus = {Mode::AUTO, true, -1};
bool manualRelay[RELAY_COUNT] = {false, false, false, false};

PhReading phR = {NAN, SensorStatus::FAULT, NAN, NAN};
EcReading ecR = {NAN, NAN, SensorStatus::FAULT, NAN};
DoReading doR = {NAN, SensorStatus::FAULT, NAN};
float tempC = 25.0f;
bool tempOk = false;

enum class Derived : uint8_t { OK, OVER_RANGE, UNDER_RANGE, UNAVAILABLE };
float salinityPpt = NAN, nh3Fraction = NAN;
Derived salinityStatus = Derived::UNAVAILABLE, nh3Status = Derived::UNAVAILABLE;

uint32_t lastRead = 0, lastControl = 0, seq = 0;
uint32_t calStarted[3] = {0, 0, 0};  // ph, ec, do

struct RecentCommand {
  char id[40];
  bool accepted;
  char message[120];
};
RecentCommand recent[8];
uint8_t recentNext = 0;

const char *derivedName(Derived d) {
  switch (d) {
    case Derived::OK: return "ok";
    case Derived::OVER_RANGE: return "over_range";
    case Derived::UNDER_RANGE: return "under_range";
    default: return "unavailable";
  }
}

void event(const char *severity, const char *kind, const char *fmt, ...) {
  char msg[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  Serial.printf("[event:%s] %s\n", kind, msg);
  cloud::pushEvent(severity, kind, msg);
}

// ------------------------------------------------------------------ relays --
void writeRelayPin(uint8_t pin, bool on) { digitalWrite(pin, on ? LOW : HIGH); }  // low-level trigger

void relaysOffEarly() {
  // Drive HIGH (relay OFF) before switching to output so nothing chatters at boot.
  for (uint8_t p : RELAY_PINS) {
    digitalWrite(p, HIGH);
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);
  }
}

// ---------------------------------------------------------------- settings --
bool settingsValid(const cloud::Settings &s) {
  return s.salMinPpt >= 10 && s.salMinPpt <= 34 && s.salMaxPpt >= 10 && s.salMaxPpt <= 34 &&
         s.salMaxPpt > s.salMinPpt && s.salHystPpt >= 1 && s.salHystPpt <= 5 && s.bilgeMaxRunMin >= 5 &&
         s.bilgeMaxRunMin <= 60 && s.bilgeMinOffMin >= 10 && s.bilgeMinOffMin <= 120 && s.bilgeMaxFails >= 1 &&
         s.bilgeMaxFails <= 10;
}

void applySettings(const cloud::Settings &s) {
  settings = s;
  BilgeConfig c;
  c.salMin = s.salMinPpt;
  c.salHyst = s.salHystPpt;
  c.maxRunMs = s.bilgeMaxRunMin * 60000UL;
  c.minOffMs = s.bilgeMinOffMin * 60000UL;
  c.maxFails = s.bilgeMaxFails;
  bilge.configure(c);
}

void loadSettings() {
  settingsPrefs.begin("settings", false);
  if (settingsPrefs.isKey("ver")) {
    cloud::Settings s;
    s.version = settingsPrefs.getInt("ver", 0);
    s.salMinPpt = settingsPrefs.getFloat("salMin", DEFAULT_SAL_MIN_PPT);
    s.salMaxPpt = settingsPrefs.getFloat("salMax", DEFAULT_SAL_MAX_PPT);
    s.salHystPpt = settingsPrefs.getFloat("salHyst", DEFAULT_SAL_HYST_PPT);
    s.bilgeMaxRunMin = settingsPrefs.getUShort("maxRun", DEFAULT_BILGE_MAX_RUN_MIN);
    s.bilgeMinOffMin = settingsPrefs.getUShort("minOff", DEFAULT_BILGE_MIN_OFF_MIN);
    s.bilgeMaxFails = settingsPrefs.getUChar("maxFails", DEFAULT_BILGE_MAX_FAILS);
    if (settingsValid(s)) settings = s;
  }
  applySettings(settings);
}

void receiveSettings() {
  cloud::Settings s;
  if (!cloud::takeSettings(s) || s.version == settings.version) return;
  if (!settingsValid(s)) {
    event("warning", "settings_rejected", "Settings version %ld rejected: values outside the allowed ranges",
          (long)s.version);
    return;
  }
  applySettings(s);
  settingsPrefs.putInt("ver", s.version);  // kept for offline restarts
  settingsPrefs.putFloat("salMin", s.salMinPpt);
  settingsPrefs.putFloat("salMax", s.salMaxPpt);
  settingsPrefs.putFloat("salHyst", s.salHystPpt);
  settingsPrefs.putUShort("maxRun", s.bilgeMaxRunMin);
  settingsPrefs.putUShort("minOff", s.bilgeMinOffMin);
  settingsPrefs.putUChar("maxFails", s.bilgeMaxFails);
  event("info", "settings_applied",
        "Settings v%ld applied: salMin %.1f, salMax %.1f, salHyst %.1f ppt; bilge max run %u min, rest %u min, %u tries",
        (long)s.version, s.salMinPpt, s.salMaxPpt, s.salHystPpt, s.bilgeMaxRunMin, s.bilgeMinOffMin, s.bilgeMaxFails);
}

// ---------------------------------------------------------------- selector --
void updateSelector(uint32_t now) {
  Selector raw = decodeSelector(digitalRead(PIN_SEL_M) == LOW, digitalRead(PIN_SEL_A) == LOW);
  if (raw != selectorRaw) {
    selectorRaw = raw;
    selectorChangedAt = now;
  } else if (raw != selector && now - selectorChangedAt >= SELECTOR_DEBOUNCE_MS) {
    selector = raw;
    if (raw == Selector::FAULT)
      event("warning", "selector_fault", "Selector reads both M and A closed; treating it as A (AUTO). Check J2 wiring.");
    else
      event("info", "selector", "Selector on the box moved to %s", selectorName(raw));
  }
}

// ------------------------------------------------------------- calibration --
int sensorIndex(const char *s) {
  if (!strcmp(s, "ph")) return 0;
  if (!strcmp(s, "ec")) return 1;
  if (!strcmp(s, "do")) return 2;
  return -1;
}

bool anyCalActive() { return phSensor.calActive() || ecSensor.calActive() || doSensor.calActive(); }

bool runCalibration(int sensor, const char *action, CalMessage &m) {
  bool wasActive = sensor == 0 ? phSensor.calActive() : sensor == 1 ? ecSensor.calActive() : doSensor.calActive();
  bool ok;
  if (!strcmp(action, "enter")) {
    ok = sensor == 0 ? phSensor.calEnter(m) : sensor == 1 ? ecSensor.calEnter(m) : doSensor.calEnter(m);
    if (ok) calStarted[sensor] = millis();
  } else if (!strcmp(action, "capture")) {
    ok = sensor == 0 ? phSensor.calCapture(tempC, tempOk, m)
         : sensor == 1 ? ecSensor.calCapture(tempC, tempOk, m)
                       : doSensor.calCapture(tempC, tempOk, m);
  } else if (!strcmp(action, "save")) {
    ok = sensor == 0 ? phSensor.calSave(m) : sensor == 1 ? ecSensor.calSave(m) : doSensor.calSave(m);
    if (wasActive) event(ok ? "info" : "warning", "calibration", "%s", m.text);
  } else if (!strcmp(action, "abort")) {
    ok = sensor == 0 ? phSensor.calAbort(m) : sensor == 1 ? ecSensor.calAbort(m) : doSensor.calAbort(m);
  } else {
    calSay(m, "Unknown calibration action");
    ok = false;
  }
  Serial.printf(">>> %s\n", m.text);
  return ok;
}

void checkCalTimeouts(uint32_t now) {
  const char *names[3] = {"pH", "EC", "DO"};
  for (int i = 0; i < 3; i++) {
    bool active = i == 0 ? phSensor.calActive() : i == 1 ? ecSensor.calActive() : doSensor.calActive();
    if (active && now - calStarted[i] >= CAL_SESSION_TIMEOUT_MS) {
      CalMessage m;
      runCalibration(i, "abort", m);
      event("warning", "calibration", "%s calibration cancelled after 30 min without being saved", names[i]);
    }
  }
}

// --------------------------------------------------------------------- mode --
// Applies a newly resolved mode. Entering MANUAL is bumpless: it starts from
// the outputs that are on right now.
void setModeStatus(const ModeStatus &ns) {
  Mode before = modeStatus.mode;
  modeStatus = ns;
  if (ns.mode == before) return;
  if (ns.mode == Mode::MANUAL)
    for (uint8_t i = 0; i < RELAY_COUNT; i++) manualRelay[i] = relays.output((RelayIndex)i);
  if (before == Mode::MANUAL && ns.mode == Mode::AUTO)
    event("info", "mode", "MANUAL ended (5 min without a command, or the selector moved); now AUTO");
}

// ---------------------------------------------------------------- commands --
void ack(const char *id, bool accepted, const char *message) {
  RecentCommand &r = recent[recentNext];
  recentNext = (recentNext + 1) % 8;
  strlcpy(r.id, id, sizeof(r.id));
  r.accepted = accepted;
  strlcpy(r.message, message, sizeof(r.message));
  cloud::pushAck(id, accepted, message);
  Serial.printf("[command %.8s] %s: %s\n", id, accepted ? "accepted" : "rejected", message);
}

void handleCommand(const cloud::Command &c, uint32_t now) {
  for (const RecentCommand &r : recent) {  // re-delivery after a lost response: re-ack only
    if (r.id[0] && !strcmp(r.id, c.id)) {
      cloud::pushAck(r.id, r.accepted, r.message);
      return;
    }
  }
  if (c.expiresInS == 0) { ack(c.id, false, "Expired before it reached the device"); return; }

  char msg[120];
  switch (c.kind) {
    case cloud::CommandKind::SET_MODE: {
      if (c.mode == Mode::OFF && !c.confirm) { ack(c.id, false, "OFF must be confirmed"); return; }
      const char *why = "";
      if (!modes.requestRemote(c.mode, selector, now, &why)) { ack(c.id, false, why); return; }
      setModeStatus(modes.resolve(selector, now));
      snprintf(msg, sizeof(msg), "Mode set to %s%s", modeName(c.mode),
               c.mode == Mode::OFF && aeration.emergency() ? " (air pumps stay on: DO emergency)" : "");
      ack(c.id, true, msg);
      event("info", "mode", "Dashboard set the mode to %s", modeName(c.mode));
      return;
    }
    case cloud::CommandKind::SET_RELAYS: {
      if (modeStatus.mode != Mode::MANUAL) {
        snprintf(msg, sizeof(msg), "Pumps can only be switched in MANUAL (current mode: %s)", modeName(modeStatus.mode));
        ack(c.id, false, msg);
        return;
      }
      if (aeration.emergency() && (c.relay[RELAY_K1] == 0 || c.relay[RELAY_K5] == 0)) {
        ack(c.id, false, "DO emergency: the air pumps cannot be switched off until DO recovers");
        return;
      }
      for (uint8_t i = 0; i < RELAY_COUNT; i++)
        if (c.relay[i] >= 0) manualRelay[i] = c.relay[i] == 1;
      modes.noteManualActivity(now);
      ack(c.id, true, "Pump states applied");
      return;
    }
    case cloud::CommandKind::CALIBRATION: {
      int s = sensorIndex(c.sensor);
      if (s < 0) { ack(c.id, false, "Unknown sensor"); return; }
      CalMessage m;
      bool ok = runCalibration(s, c.action, m);
      ack(c.id, ok, m.text);
      return;
    }
    case cloud::CommandKind::CLEAR_BILGE_LOCKOUT:
      if (bilge.clearLockout()) {
        ack(c.id, true, "Bilge lockout cleared; salinity correction will retry when needed");
        event("info", "bilge", "Operator cleared the bilge lockout");
      } else {
        ack(c.id, false, "The bilge pumps are not locked out");
      }
      return;
  }
}

// ------------------------------------------------------------------- serial --
void printStatus() {
  Serial.printf("Mode %s (%s), selector %s | T=%s%.2f C | pH %s %.2f | EC %s %.3f mS/cm (EC25 %.3f) | "
                "S %s %.2f ppt | DO %s %.2f mg/L | NH3 %s %.4f\n",
                modeName(modeStatus.mode), modeStatus.fromSelector ? "selector" : "dashboard", selectorName(selector),
                tempOk ? "" : "FAULT/", tempC, sensorStatusName(phR.status), phR.ph, sensorStatusName(ecR.status),
                ecR.ecMsCm, ecR.ec25MsCm, derivedName(salinityStatus), salinityPpt, sensorStatusName(doR.status),
                doR.mgL, derivedName(nh3Status), nh3Fraction);
  Serial.printf("Relays K1 %d K5 %d K3 %d K4 %d | DO emergency %d | bilge %s (fails %u) | settings v%ld\n",
                relays.output(RELAY_K1), relays.output(RELAY_K5), relays.output(RELAY_K3), relays.output(RELAY_K4),
                aeration.emergency(), bilgeStateName(bilge.state()), bilge.failCount(), (long)settings.version);
  cloud::LinkStatus l = cloud::status();
  Serial.printf("Wi-Fi %s (%ld dBm) | Supabase %s %s\n", l.wifi ? "up" : "down", (long)l.rssi,
                l.backendOk ? "ok" : "not reached", l.lastError);
}

void handleSerial(const char *cmd) {
  static const struct { const char *name; int sensor; const char *action; } CAL[] = {
      {"enterph", 0, "enter"}, {"calph", 0, "capture"}, {"exitph", 0, "save"}, {"abortph", 0, "abort"},
      {"enterec", 1, "enter"}, {"calec", 1, "capture"}, {"exitec", 1, "save"}, {"abortec", 1, "abort"},
      {"enterdo", 2, "enter"}, {"caldo", 2, "capture"}, {"exitdo", 2, "save"}, {"abortdo", 2, "abort"},
  };
  for (const auto &c : CAL) {
    if (!strcmp(cmd, c.name)) {
      CalMessage m;
      runCalibration(c.sensor, c.action, m);
      return;
    }
  }
  if (!strcmp(cmd, "status")) printStatus();
  else if (!strcmp(cmd, "clearlockout")) Serial.println(bilge.clearLockout() ? "Bilge lockout cleared" : "No lockout");
  else
    Serial.println("Commands: status | enterph calph exitph abortph | enterec calec exitec abortec | "
                   "enterdo caldo exitdo abortdo | clearlockout");
}

void pollSerial() {
  static char buf[24];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (len) {
        buf[len] = 0;
        handleSerial(buf);
        len = 0;
      }
    } else if (c != ' ' && len < sizeof(buf) - 1) {
      buf[len++] = tolower(c);
    }
  }
}

// ------------------------------------------------------------- measurement --
void computeDerived() {
  // Salinity: PSS-78 from conductivity at the measured temperature. Needs a
  // valid temperature; never computed from the 25 °C fallback.
  salinityPpt = NAN;
  if (ecR.status == SensorStatus::OVER_RANGE) {
    salinityStatus = Derived::OVER_RANGE;
  } else if (ecR.status != SensorStatus::OK || !tempOk || tempC < -2 || tempC > 35) {
    salinityStatus = Derived::UNAVAILABLE;
  } else {
    float s = sm::pss78Salinity(ecR.ecMsCm, tempC);
    if (s < 2) salinityStatus = Derived::UNDER_RANGE;
    else if (s > 42) salinityStatus = Derived::OVER_RANGE;
    else { salinityPpt = s; salinityStatus = Derived::OK; }
  }

  // Unionized ammonia fraction (Emerson et al. 1975): needs valid pH and temperature.
  nh3Fraction = NAN;
  if (phR.status != SensorStatus::OK || !tempOk) {
    nh3Status = Derived::UNAVAILABLE;
  } else if (tempC < NH3_MIN_T_C || tempC > NH3_MAX_T_C || phR.ph < NH3_MIN_PH || phR.ph > NH3_MAX_PH) {
    nh3Status = (phR.ph < NH3_MIN_PH || tempC < NH3_MIN_T_C) ? Derived::UNDER_RANGE : Derived::OVER_RANGE;
  } else {
    nh3Fraction = sm::emersonNh3Fraction(phR.ph, tempC);
    nh3Status = Derived::OK;
  }
}

void readSensors() {
  tempOk = temps.compensation(tempC);
  SensorStatus prev[3] = {phR.status, ecR.status, doR.status};
  phR = phSensor.read(tempC, tempOk);
  ecR = ecSensor.read(tempC, tempOk);
  doR = doSensor.read(tempC, tempOk);
  computeDerived();
  const char *names[3] = {"pH", "EC", "DO"};
  SensorStatus now[3] = {phR.status, ecR.status, doR.status};
  for (int i = 0; i < 3; i++) {
    bool wasBad = prev[i] == SensorStatus::FAULT || prev[i] == SensorStatus::OVER_RANGE;
    bool isBad = now[i] == SensorStatus::FAULT || now[i] == SensorStatus::OVER_RANGE;
    if (seq > 0 && isBad != wasBad)
      event(isBad ? "warning" : "info", "sensor", "%s sensor %s", names[i],
            isBad ? (now[i] == SensorStatus::FAULT ? "fault" : "out of range") : "reading again");
  }
}

void runControl(uint32_t now) {
  bool wasEmergency = aeration.emergency();
  aeration.update(doR.status == SensorStatus::OK || doR.status == SensorStatus::DEGRADED ? doR.mgL : NAN,
                  doSensor.calActive(), now);
  if (aeration.emergency() != wasEmergency) {
    if (aeration.emergency())
      event("critical", "do_emergency", "DO %.2f mg/L is below 3.0: both air pumps forced ON in every mode", doR.mgL);
    else
      event("info", "do_emergency", "DO recovered to %.2f mg/L; emergency cleared", doR.mgL);
  }

  SalinityInput in = salinityStatus == Derived::OK            ? SalinityInput::VALID
                     : salinityStatus == Derived::UNDER_RANGE ? SalinityInput::UNDER_RANGE
                     : salinityStatus == Derived::OVER_RANGE  ? SalinityInput::OVER_RANGE
                                                              : SalinityInput::FAULT;
  switch (bilge.update(in, salinityPpt, modeStatus.mode == Mode::AUTO, now)) {
    case BilgeEvent::STARTED:
      event("warning", "bilge", "Salinity below salMin %.1f ppt: bilge pumps ON", settings.salMinPpt);
      break;
    case BilgeEvent::RECOVERED: event("info", "bilge", "Salinity recovered: bilge pumps OFF, resting"); break;
    case BilgeEvent::TIMED_OUT:
      event("warning", "bilge", "Bilge run reached %u min without recovery (try %u of %u)", settings.bilgeMaxRunMin,
            bilge.failCount(), settings.bilgeMaxFails);
      break;
    case BilgeEvent::LOCKED_OUT:
      event("critical", "bilge_lockout", "Salinity not recovering after %u tries: bilge locked out until salinity "
            "recovers or the operator clears it", bilge.failCount());
      break;
    case BilgeEvent::LOCKOUT_RECOVERED: event("info", "bilge", "Salinity recovered; bilge lockout released"); break;
    case BilgeEvent::STOPPED_FAULT: event("warning", "bilge", "Salinity reading lost: bilge pumps stopped"); break;
    default: break;
  }
}

void updateOutputs() {
  bool air = false, pumpsBilge = false;
  bool desired[RELAY_COUNT];
  switch (modeStatus.mode) {
    case Mode::AUTO:
      air = aeration.autoDemand() || aeration.emergency();
      pumpsBilge = bilge.pumpsOn();
      desired[RELAY_K1] = desired[RELAY_K5] = air;
      desired[RELAY_K3] = desired[RELAY_K4] = pumpsBilge;
      break;
    case Mode::MANUAL:
      for (uint8_t i = 0; i < RELAY_COUNT; i++) desired[i] = manualRelay[i];
      break;
    case Mode::OFF:
      for (bool &d : desired) d = false;
      break;
  }
  if (aeration.emergency()) desired[RELAY_K1] = desired[RELAY_K5] = true;  // every mode
  for (uint8_t i = 0; i < RELAY_COUNT; i++) relays.setDesired((RelayIndex)i, desired[i]);
}

// ------------------------------------------------------------------ report --
static float r(float v, float scale) { return isnan(v) ? v : roundf(v * scale) / scale; }

void publish() {
  JsonDocument d;
  cloud::LinkStatus link = cloud::status();
  d["seq"] = ++seq;
  d["uptime_s"] = millis() / 1000;
  d["fw"] = FIRMWARE_VERSION;
  d["simulated"] = false;
  if (link.wifi) d["rssi"] = link.rssi; else d["rssi"] = nullptr;
  d["settings_version"] = settings.version;
  d["mode"] = modeName(modeStatus.mode);
  d["mode_source"] = modeStatus.fromSelector ? "selector" : "remote";
  d["selector"] = selectorName(selector);
  if (modeStatus.manualExpiresInS >= 0) d["manual_expires_in_s"] = modeStatus.manualExpiresInS;
  else d["manual_expires_in_s"] = nullptr;
  d["do_emergency"] = aeration.emergency();
  JsonObject rel = d["relays"].to<JsonObject>();
  for (uint8_t i = 0; i < RELAY_COUNT; i++) rel[RELAY_KEYS[i]] = relays.output((RelayIndex)i);
  JsonObject b = d["bilge"].to<JsonObject>();
  b["state"] = bilgeStateName(bilge.state());
  b["fail_count"] = bilge.failCount();

  JsonObject ph = d["ph"].to<JsonObject>();
  ph["value"] = r(phR.ph, 100);
  ph["status"] = sensorStatusName(phR.status);
  ph["mv"] = r(phR.pinMv, 1);
  JsonObject ec = d["ec"].to<JsonObject>();
  ec["ec_ms_cm"] = r(ecR.ecMsCm, 1000);
  ec["ec25_ms_cm"] = r(ecR.ec25MsCm, 1000);
  ec["status"] = sensorStatusName(ecR.status);
  ec["mv"] = r(ecR.mv, 10);
  JsonObject sal = d["salinity"].to<JsonObject>();
  sal["value"] = r(salinityPpt, 100);
  sal["status"] = derivedName(salinityStatus);
  JsonObject t = d["temp"].to<JsonObject>();
  t["value"] = tempOk ? r(tempC, 100) : NAN;
  t["status"] = tempOk ? "ok" : (temps.found() ? "fault" : "not_present");
  t["t2"] = temps.valid(1) ? r(temps.value(1), 100) : NAN;
  JsonObject dO = d["do"].to<JsonObject>();
  dO["value"] = r(doR.mgL, 100);
  dO["status"] = sensorStatusName(doR.status);
  dO["mv"] = r(doR.mv, 1);
  JsonObject nh3 = d["nh3"].to<JsonObject>();
  nh3["fraction"] = r(nh3Fraction, 100000);
  nh3["status"] = derivedName(nh3Status);

  JsonObject cal = d["cal"].to<JsonObject>();
  phSensor.toJson(cal["ph"].to<JsonObject>());
  ecSensor.toJson(cal["ec"].to<JsonObject>());
  doSensor.toJson(cal["do"].to<JsonObject>());

  JsonArray f = d["faults"].to<JsonArray>();
  if (selector == Selector::FAULT) f.add("selector_fault");
  if (!tempOk) f.add("temperature_fault");
  if (!link.backendOk) f.add("cloud_unreachable_last_cycle");

  static char buf[4096];
  size_t n = serializeJson(d, buf, sizeof(buf));
  if (n > 0 && n < sizeof(buf) - 1) cloud::publishState(buf, n, anyCalActive());
}

// ---------------------------------------------------------------------- LED --
void updateLed(uint32_t now) {
  if (PIN_STATUS_LED == 0xFF) return;
  cloud::LinkStatus l = cloud::status();
  bool on;
  if (l.backendOk) on = true;                     // steady: reporting to Supabase
  else if (l.wifi) on = (now / 1000) % 2;         // slow blink: Wi-Fi up, Supabase not reached
  else on = (now / 150) % 2;                      // fast blink: no Wi-Fi
  digitalWrite(PIN_STATUS_LED, on);
}

// ------------------------------------------------------------ setup / loop --
void setup() {
  relaysOffEarly();
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nS.A.L.I.M.A.N.G.O. float node v%s\n", FIRMWARE_VERSION);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_PH, ADC_11db);  // ~0.15-3.1 V usable
  analogSetPinAttenuation(PIN_EC, ADC_11db);
  analogSetPinAttenuation(PIN_DO, ADC_11db);
  pinMode(PIN_SEL_M, INPUT);  // external 10k pull-ups (R5, R6)
  pinMode(PIN_SEL_A, INPUT);
  if (PIN_STATUS_LED != 0xFF) pinMode(PIN_STATUS_LED, OUTPUT);

  uint32_t now = millis();
  selectorRaw = selector = decodeSelector(digitalRead(PIN_SEL_M) == LOW, digitalRead(PIN_SEL_A) == LOW);
  selectorChangedAt = now;

  loadSettings();
  temps.begin(PIN_ONEWIRE);
  phSensor.begin();
  ecSensor.begin();
  doSensor.begin();
  aeration.begin(now);  // fail-safe: aeration ON at boot
  modeStatus = modes.resolve(selector, now);
  if (modeStatus.mode == Mode::MANUAL)  // selector already at M: start with aeration on
    manualRelay[RELAY_K1] = manualRelay[RELAY_K5] = true;

  cloud::begin();
  event("info", "boot", "Firmware %s started (reset reason %d); mode %s, selector %s, aeration ON until DO is confirmed",
        FIRMWARE_VERSION, (int)esp_reset_reason(), modeName(modeStatus.mode), selectorName(selector));
}

void loop() {
  uint32_t now = millis();
  temps.poll(now);
  ecSensor.sample(now);
  updateSelector(now);
  pollSerial();
  receiveSettings();

  setModeStatus(modes.resolve(selector, now));
  cloud::Command c;
  while (cloud::popCommand(c)) handleCommand(c, now);

  uint32_t readInterval = anyCalActive() ? CAL_READ_INTERVAL_MS : CONTROL_INTERVAL_MS;
  if (now - lastRead >= readInterval || lastRead == 0) {
    lastRead = now;
    readSensors();
    if (now - lastControl >= CONTROL_INTERVAL_MS || lastControl == 0) {
      lastControl = now;
      runControl(now);
    }
    checkCalTimeouts(now);
    updateOutputs();
    relays.update(now);
    publish();
  }

  updateOutputs();
  relays.update(now);
  for (uint8_t i = 0; i < RELAY_COUNT; i++) writeRelayPin(RELAY_PINS[i], relays.output((RelayIndex)i));
  updateLed(now);
}
