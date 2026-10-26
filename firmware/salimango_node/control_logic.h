// Pure control logic (no Arduino dependencies; unit-tested on the host).
// All timing uses rollover-safe `now - start` arithmetic on millis().
#pragma once

#include <math.h>
#include <stdint.h>

// --------------------------------------------------------------- selector ---
// INScom LW28-20A M-O-A switch (DOSENSE/selector_test, perfboard J2):
//   M closes GPIO4 to GND, A closes GPIO21 to GND, O leaves both open.
enum class Selector : uint8_t { AUTO, MANUAL, OFF, FAULT };
Selector decodeSelector(bool mPinLow, bool aPinLow);
const char *selectorName(Selector s);

// -------------------------------------------------------------- mode ---------
enum class Mode : uint8_t { AUTO, MANUAL, OFF };
const char *modeName(Mode m);

struct ModeStatus {
  Mode mode;
  bool fromSelector;          // true when the local selector decides the mode
  int32_t manualExpiresInS;   // -1 when not in MANUAL
};

// The local selector is the authority: in M or O it decides the mode. In A
// (or FAULT, which fails safe to the AUTO position) the dashboard may request
// AUTO, MANUAL or OFF. MANUAL reverts to AUTO after manualTimeoutMs without a
// manual command, whichever way it was entered.
class ModeManager {
 public:
  explicit ModeManager(uint32_t manualTimeoutMs) : timeoutMs_(manualTimeoutMs) {}
  ModeStatus resolve(Selector sel, uint32_t now);
  // Remote request; false (with reason) when the selector overrides it.
  bool requestRemote(Mode m, Selector sel, uint32_t now, const char **reason);
  void noteManualActivity(uint32_t now) { manualSince_ = now; }

 private:
  uint32_t timeoutMs_;
  Mode remote_ = Mode::AUTO;           // boot / reset -> AUTO
  bool selectorManualExpired_ = false;
  bool wasManual_ = false;
  uint32_t manualSince_ = 0;
};

// ------------------------------------------------------------- aeration -----
// Low-DO aeration, from DOSENSE/DO_ESP32_DS18B20_WiFi4:
//   ON  when DO < 3.0 mg/L for 3 readings in a row (missing reading counts as low)
//   OFF when DO > 3.0 mg/L for 3 readings in a row
//   stays ON ≥ 60 s and OFF ≥ 10 s; held during DO calibration.
// Boot: aeration starts ON (fail-safe rule, FIRMWARE.md §2).
// Emergency (forces aeration in every mode): a *valid* DO below 3.0 mg/L for 3
// readings; cleared by 3 valid readings above 3.0 mg/L.
struct AerationConfig {
  float onBelowMgL = 3.0f;
  float offAboveMgL = 3.0f;
  uint8_t confirm = 3;
  uint32_t minOnMs = 60000;
  uint32_t minOffMs = 10000;
};

class AerationController {
 public:
  explicit AerationController(const AerationConfig &c = AerationConfig()) : cfg_(c) {}
  void begin(uint32_t now);
  // doMgL: NaN when the reading is invalid. hold: DO probe is being calibrated.
  void update(float doMgL, bool hold, uint32_t now);
  bool autoDemand() const { return on_; }
  bool emergency() const { return emergency_; }

 private:
  AerationConfig cfg_;
  bool on_ = true;
  bool emergency_ = false;
  uint32_t changedAt_ = 0;
  uint8_t lowCount_ = 0, okCount_ = 0, validLow_ = 0, validOk_ = 0;
};

// ---------------------------------------------------------------- bilge -----
// AUTO salinity correction, from salinity_rev.md:
//   ON after salinity < salMin for 3 readings; OFF when salinity ≥ salMin + salHyst
//   (recovered) or after maxRun (timeout); then rest minOff before re-checking;
//   after maxFails timeouts in a row -> LOCKOUT until salinity recovers or the
//   operator clears it.
// Probe-range handling (project decision): a reading above the K = 1 probe's
// range (OVER_RANGE) counts as "not low" and as recovered; a reading below the
// PSS-78 range (UNDER_RANGE, S < 2) counts as low. A missing/invalid reading
// (FAULT) stops the pumps.
enum class SalinityInput : uint8_t { VALID, UNDER_RANGE, OVER_RANGE, FAULT };
enum class BilgeState : uint8_t { IDLE, RUNNING, RESTING, LOCKOUT };
const char *bilgeStateName(BilgeState s);

struct BilgeConfig {
  float salMin = 28.0f;
  float salHyst = 2.0f;
  uint32_t maxRunMs = 15UL * 60000UL;
  uint32_t minOffMs = 30UL * 60000UL;
  uint8_t maxFails = 3;
  uint8_t confirm = 3;
};

enum class BilgeEvent : uint8_t { NONE, STARTED, RECOVERED, TIMED_OUT, LOCKED_OUT, LOCKOUT_RECOVERED, STOPPED_FAULT };

class BilgeController {
 public:
  void configure(const BilgeConfig &c) { cfg_ = c; }
  const BilgeConfig &config() const { return cfg_; }
  // Call once per control cycle. autoMode false: pumps off, a running cycle is
  // abandoned (rest/lockout timers keep running).
  BilgeEvent update(SalinityInput in, float salPpt, bool autoMode, uint32_t now);
  bool clearLockout();
  bool pumpsOn() const { return state_ == BilgeState::RUNNING; }
  BilgeState state() const { return state_; }
  uint8_t failCount() const { return fails_; }

 private:
  bool isLow(SalinityInput in, float s) const;
  bool isRecovered(SalinityInput in, float s) const;
  BilgeConfig cfg_;
  BilgeState state_ = BilgeState::IDLE;
  uint32_t t0_ = 0;
  uint8_t lowCount_ = 0, fails_ = 0;
};

// --------------------------------------------------------------- relays -----
// Relay channels in use (low-level trigger module): K1 air pump 1, K5 air
// pump 2, K3 bilge pump 1, K4 bilge pump 2. K2 has a defective terminal.
enum RelayIndex : uint8_t { RELAY_K1 = 0, RELAY_K5, RELAY_K3, RELAY_K4, RELAY_COUNT };

// Turns desired states into outputs. Off is immediate; within a group only one
// pump may start per stagger interval so motors never start together
// (air pumps 5 s apart, bilge pumps 2 s apart, project decision).
class RelaySequencer {
 public:
  RelaySequencer(uint32_t airStaggerMs, uint32_t bilgeStaggerMs);
  void setDesired(RelayIndex r, bool on) { desired_[r] = on; }
  bool desired(RelayIndex r) const { return desired_[r]; }
  void update(uint32_t now);
  bool output(RelayIndex r) const { return out_[r]; }

 private:
  struct Group {
    RelayIndex first, second;
    uint32_t staggerMs;
    uint32_t lastStart;
    bool started;
  };
  Group groups_[2];
  bool desired_[RELAY_COUNT] = {false, false, false, false};
  bool out_[RELAY_COUNT] = {false, false, false, false};
};
