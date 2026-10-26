#include "control_logic.h"

// --------------------------------------------------------------- selector ---
Selector decodeSelector(bool m, bool a) {
  if (m && !a) return Selector::MANUAL;
  if (a && !m) return Selector::AUTO;
  if (!m && !a) return Selector::OFF;
  return Selector::FAULT;  // both contacts closed - should never happen
}

const char *selectorName(Selector s) {
  switch (s) {
    case Selector::AUTO: return "AUTO";
    case Selector::MANUAL: return "MANUAL";
    case Selector::OFF: return "OFF";
    default: return "FAULT";
  }
}

const char *modeName(Mode m) {
  switch (m) {
    case Mode::AUTO: return "AUTO";
    case Mode::MANUAL: return "MANUAL";
    default: return "OFF";
  }
}

// -------------------------------------------------------------- mode ---------
ModeStatus ModeManager::resolve(Selector sel, uint32_t now) {
  if (sel != Selector::MANUAL) selectorManualExpired_ = false;

  ModeStatus st;
  if (sel == Selector::OFF) {
    st = {Mode::OFF, true, -1};
  } else if (sel == Selector::MANUAL) {
    st = {selectorManualExpired_ ? Mode::AUTO : Mode::MANUAL, true, -1};
  } else {  // AUTO position, or FAULT treated as AUTO
    st = {remote_, remote_ == Mode::AUTO, -1};
  }

  if (st.mode == Mode::MANUAL) {
    if (!wasManual_) manualSince_ = now;
    uint32_t elapsed = now - manualSince_;
    if (elapsed >= timeoutMs_) {
      if (sel == Selector::MANUAL) selectorManualExpired_ = true;
      else remote_ = Mode::AUTO;
      st = {Mode::AUTO, true, -1};
    } else {
      st.manualExpiresInS = (int32_t)((timeoutMs_ - elapsed + 999) / 1000);
    }
  }
  wasManual_ = st.mode == Mode::MANUAL;
  return st;
}

bool ModeManager::requestRemote(Mode m, Selector sel, uint32_t now, const char **reason) {
  if (sel == Selector::OFF) {
    *reason = "The selector on the box is at OFF; it overrides the dashboard";
    return false;
  }
  if (sel == Selector::MANUAL) {
    if (m == Mode::MANUAL) {            // re-enter MANUAL after the 5-min revert
      selectorManualExpired_ = false;
      manualSince_ = now;
      wasManual_ = true;
      return true;
    }
    *reason = "The selector on the box is at MANUAL; it overrides the dashboard";
    return false;
  }
  remote_ = m;
  if (m == Mode::MANUAL) {
    manualSince_ = now;
    wasManual_ = true;
  }
  return true;
}

// ------------------------------------------------------------- aeration -----
void AerationController::begin(uint32_t now) {
  on_ = true;  // fail-safe: aeration ON at boot
  changedAt_ = now;
  emergency_ = false;
  lowCount_ = okCount_ = validLow_ = validOk_ = 0;
}

static uint8_t bump(uint8_t c) { return c < 255 ? c + 1 : 255; }

void AerationController::update(float doMgL, bool hold, uint32_t now) {
  if (hold) {  // probe is out of the pond while calibrating: hold everything
    lowCount_ = okCount_ = validLow_ = validOk_ = 0;
    return;
  }
  bool valid = !isnan(doMgL);
  bool low = !valid || doMgL < cfg_.onBelowMgL;  // no reading -> treat as low
  bool ok = valid && doMgL > cfg_.offAboveMgL;
  lowCount_ = low ? bump(lowCount_) : 0;
  okCount_ = ok ? bump(okCount_) : 0;
  validLow_ = (valid && doMgL < cfg_.onBelowMgL) ? bump(validLow_) : 0;
  validOk_ = ok ? bump(validOk_) : 0;

  uint32_t held = now - changedAt_;
  if (!on_ && lowCount_ >= cfg_.confirm && held >= cfg_.minOffMs) {
    on_ = true;
    changedAt_ = now;
  } else if (on_ && okCount_ >= cfg_.confirm && held >= cfg_.minOnMs) {
    on_ = false;
    changedAt_ = now;
  }

  if (!emergency_ && validLow_ >= cfg_.confirm) emergency_ = true;
  else if (emergency_ && validOk_ >= cfg_.confirm) emergency_ = false;
}

// ---------------------------------------------------------------- bilge -----
const char *bilgeStateName(BilgeState s) {
  switch (s) {
    case BilgeState::IDLE: return "IDLE";
    case BilgeState::RUNNING: return "RUNNING";
    case BilgeState::RESTING: return "RESTING";
    default: return "LOCKOUT";
  }
}

bool BilgeController::isLow(SalinityInput in, float s) const {
  return in == SalinityInput::UNDER_RANGE || (in == SalinityInput::VALID && s < cfg_.salMin);
}

bool BilgeController::isRecovered(SalinityInput in, float s) const {
  return in == SalinityInput::OVER_RANGE || (in == SalinityInput::VALID && s >= cfg_.salMin + cfg_.salHyst);
}

BilgeEvent BilgeController::update(SalinityInput in, float s, bool autoMode, uint32_t now) {
  if (in == SalinityInput::FAULT || !autoMode) {
    lowCount_ = 0;
    if (state_ == BilgeState::RUNNING) {
      state_ = BilgeState::IDLE;
      return in == SalinityInput::FAULT ? BilgeEvent::STOPPED_FAULT : BilgeEvent::NONE;
    }
    if (state_ == BilgeState::RESTING && now - t0_ >= cfg_.minOffMs) state_ = BilgeState::IDLE;
    return BilgeEvent::NONE;
  }

  switch (state_) {
    case BilgeState::IDLE:
      lowCount_ = isLow(in, s) ? (uint8_t)(lowCount_ + (lowCount_ < 255)) : 0;
      if (lowCount_ >= cfg_.confirm) {
        state_ = BilgeState::RUNNING;
        t0_ = now;
        return BilgeEvent::STARTED;
      }
      break;

    case BilgeState::RUNNING:
      if (isRecovered(in, s)) {
        state_ = BilgeState::RESTING;
        fails_ = 0;
        lowCount_ = 0;
        t0_ = now;
        return BilgeEvent::RECOVERED;
      }
      if (now - t0_ >= cfg_.maxRunMs) {
        lowCount_ = 0;
        t0_ = now;
        fails_++;
        if (fails_ >= cfg_.maxFails) {
          state_ = BilgeState::LOCKOUT;
          return BilgeEvent::LOCKED_OUT;
        }
        state_ = BilgeState::RESTING;
        return BilgeEvent::TIMED_OUT;
      }
      break;

    case BilgeState::RESTING:
      if (now - t0_ >= cfg_.minOffMs) state_ = BilgeState::IDLE;
      break;

    case BilgeState::LOCKOUT:
      if (isRecovered(in, s)) {
        fails_ = 0;
        lowCount_ = 0;
        state_ = BilgeState::IDLE;
        return BilgeEvent::LOCKOUT_RECOVERED;
      }
      break;
  }
  return BilgeEvent::NONE;
}

bool BilgeController::clearLockout() {
  if (state_ != BilgeState::LOCKOUT) return false;
  state_ = BilgeState::IDLE;
  fails_ = 0;
  lowCount_ = 0;
  return true;
}

// --------------------------------------------------------------- relays -----
RelaySequencer::RelaySequencer(uint32_t airStaggerMs, uint32_t bilgeStaggerMs)
    : groups_{{RELAY_K1, RELAY_K5, airStaggerMs, 0, false}, {RELAY_K3, RELAY_K4, bilgeStaggerMs, 0, false}} {}

void RelaySequencer::update(uint32_t now) {
  for (Group &g : groups_) {
    RelayIndex order[2] = {g.first, g.second};
    for (RelayIndex r : order) {
      if (!desired_[r]) {
        out_[r] = false;
      } else if (!out_[r] && (!g.started || now - g.lastStart >= g.staggerMs)) {
        out_[r] = true;
        g.lastStart = now;
        g.started = true;
      }
    }
  }
}
