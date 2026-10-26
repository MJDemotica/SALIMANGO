// Wi-Fi + Supabase link. Runs in its own FreeRTOS task (core 0) so network
// delays never block sensing or control (Arduino loop, core 1).
//
// One HTTPS call per report cycle: POST /rest/v1/rpc/device_report with the
// latest state, queued command acknowledgements and events. The response
// carries waiting commands and the current settings. Authorised by the
// per-device token; only the public anon key is used (never the service key).
#pragma once

#include <Arduino.h>

#include "control_logic.h"

namespace cloud {

enum class CommandKind : uint8_t { SET_MODE, SET_RELAYS, CALIBRATION, CLEAR_BILGE_LOCKOUT };

struct Command {
  char id[40];
  CommandKind kind;
  uint16_t expiresInS;
  // set_mode
  Mode mode;
  bool confirm;
  // set_relays: -1 = not in the request, 0 = off, 1 = on (index = RelayIndex)
  int8_t relay[RELAY_COUNT];
  // calibration
  char sensor[4];   // "ph" | "ec" | "do"
  char action[8];   // "enter" | "capture" | "save" | "abort"
};

struct Settings {
  int32_t version;  // 0 = built-in defaults (never received from the server)
  float salMinPpt, salMaxPpt, salHystPpt;
  uint16_t bilgeMaxRunMin, bilgeMinOffMin;
  uint8_t bilgeMaxFails;
};

struct LinkStatus {
  bool wifi;
  bool backendOk;      // last report accepted
  int32_t rssi;
  uint32_t lastOkMs;   // millis() of the last accepted report (0 = never)
  char lastError[80];
};

void begin();
// Latest state JSON object (without acks/events). fast = report every 2 s.
void publishState(const char *json, size_t len, bool fast);
bool popCommand(Command &out);
void pushAck(const char *id, bool accepted, const char *message);
void pushEvent(const char *severity, const char *kind, const char *message);
bool takeSettings(Settings &out);
LinkStatus status();

}  // namespace cloud
