#include "cloud.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#include "config.h"
#include "secrets.h"

namespace cloud {

namespace {

constexpr size_t STATE_MAX = 4096;
constexpr size_t BODY_MAX = 7168;
constexpr uint8_t MAX_ACKS = 8;
constexpr uint8_t MAX_EVENTS = 16;

struct Ack {
  char id[40];
  bool accepted;
  char message[120];
};
struct Event {
  char severity[10];
  char kind[24];
  char message[160];
};

SemaphoreHandle_t lock;
QueueHandle_t commandQueue;

// Shared with the main loop (guarded by `lock`)
char stateJson[STATE_MAX];
size_t stateLen = 0;
bool stateFast = false;
Ack acks[MAX_ACKS];
uint8_t ackCount = 0;
Event events[MAX_EVENTS];
uint8_t eventCount = 0;
Settings pendingSettings;
bool settingsReady = false;
LinkStatus link = {false, false, 0, 0, ""};

// Task-local
char body[BODY_MAX];
uint32_t lastCommandAt = 0;

struct Guard {
  Guard() { xSemaphoreTake(lock, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(lock); }
};

void setError(const char *fmt, ...) {
  Guard g;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(link.lastError, sizeof(link.lastError), fmt, ap);
  va_end(ap);
}

bool parseMode(const char *s, Mode &m) {
  if (!s) return false;
  if (!strcmp(s, "AUTO")) { m = Mode::AUTO; return true; }
  if (!strcmp(s, "MANUAL")) { m = Mode::MANUAL; return true; }
  if (!strcmp(s, "OFF")) { m = Mode::OFF; return true; }
  return false;
}

// Builds {"p_token":..., "p_report": {<state>, "acks": [...], "events": [...]}}.
// Returns the number of acks/events included so they are dropped only after success.
size_t buildBody(uint8_t &nAcks, uint8_t &nEvents) {
  Guard g;
  nAcks = ackCount;
  nEvents = eventCount;
  if (stateLen < 2) return 0;

  JsonDocument extra;
  JsonArray a = extra["acks"].to<JsonArray>();
  for (uint8_t i = 0; i < nAcks; i++) {
    JsonObject o = a.add<JsonObject>();
    o["id"] = acks[i].id;
    o["status"] = acks[i].accepted ? "accepted" : "rejected";
    o["message"] = acks[i].message;
  }
  JsonArray e = extra["events"].to<JsonArray>();
  for (uint8_t i = 0; i < nEvents; i++) {
    JsonObject o = e.add<JsonObject>();
    o["severity"] = events[i].severity;
    o["kind"] = events[i].kind;
    o["message"] = events[i].message;
  }
  char extraJson[2560];
  size_t extraLen = serializeJson(extra, extraJson, sizeof(extraJson));
  if (extraLen < 2 || extraLen >= sizeof(extraJson) - 1) return 0;

  // state "{...}" + extra "{...}" -> "{... , ...}"
  int n = snprintf(body, sizeof(body), "{\"p_token\":\"%s\",\"p_report\":%.*s,%s}", DEVICE_TOKEN,
                   (int)(stateLen - 1), stateJson, extraJson + 1);
  if (n <= 0 || (size_t)n >= sizeof(body)) return 0;
  return (size_t)n;
}

void dropSent(uint8_t nAcks, uint8_t nEvents) {
  Guard g;
  memmove(acks, acks + nAcks, (ackCount - nAcks) * sizeof(Ack));
  ackCount -= nAcks;
  memmove(events, events + nEvents, (eventCount - nEvents) * sizeof(Event));
  eventCount -= nEvents;
}

void handleResponse(const String &payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    setError("Unreadable response from Supabase");
    return;
  }
  for (JsonObject c : doc["commands"].as<JsonArray>()) {
    Command cmd = {};
    strlcpy(cmd.id, c["id"] | "", sizeof(cmd.id));
    cmd.expiresInS = c["expires_in_s"] | 0;
    for (int8_t &r : cmd.relay) r = -1;
    const char *kind = c["kind"] | "";
    JsonObject p = c["payload"];
    bool ok = true;
    if (!strcmp(kind, "set_mode")) {
      cmd.kind = CommandKind::SET_MODE;
      ok = parseMode(p["mode"] | "", cmd.mode);
      cmd.confirm = p["confirm"] | false;
    } else if (!strcmp(kind, "set_relays")) {
      cmd.kind = CommandKind::SET_RELAYS;
      const char *keys[RELAY_COUNT] = {"k1", "k5", "k3", "k4"};  // RelayIndex order
      for (uint8_t i = 0; i < RELAY_COUNT; i++)
        if (p[keys[i]].is<bool>()) cmd.relay[i] = p[keys[i]].as<bool>() ? 1 : 0;
    } else if (!strcmp(kind, "calibration")) {
      cmd.kind = CommandKind::CALIBRATION;
      strlcpy(cmd.sensor, p["sensor"] | "", sizeof(cmd.sensor));
      strlcpy(cmd.action, p["action"] | "", sizeof(cmd.action));
    } else if (!strcmp(kind, "clear_bilge_lockout")) {
      cmd.kind = CommandKind::CLEAR_BILGE_LOCKOUT;
    } else {
      ok = false;
    }
    if (!ok || !cmd.id[0]) {
      if (cmd.id[0]) pushAck(cmd.id, false, "Command not understood by this firmware version");
      continue;
    }
    lastCommandAt = millis();
    if (xQueueSend(commandQueue, &cmd, 0) != pdTRUE) pushAck(cmd.id, false, "Device busy; try again");
  }

  JsonObject s = doc["settings"];
  if (!s.isNull()) {
    Settings st;
    st.version = s["version"] | 0;
    st.salMinPpt = s["sal_min_ppt"] | DEFAULT_SAL_MIN_PPT;
    st.salMaxPpt = s["sal_max_ppt"] | DEFAULT_SAL_MAX_PPT;
    st.salHystPpt = s["sal_hyst_ppt"] | DEFAULT_SAL_HYST_PPT;
    st.bilgeMaxRunMin = s["bilge_max_run_min"] | DEFAULT_BILGE_MAX_RUN_MIN;
    st.bilgeMinOffMin = s["bilge_min_off_min"] | DEFAULT_BILGE_MIN_OFF_MIN;
    st.bilgeMaxFails = s["bilge_max_fails"] | DEFAULT_BILGE_MAX_FAILS;
    Guard g;
    pendingSettings = st;
    settingsReady = true;
  }
}

void maintainWifi(uint32_t now) {
  static uint32_t lastTry = 0;
  static bool started = false;
  if (!started) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("salimango-node");
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    lastTry = now;
    started = true;
  }
  bool up = WiFi.status() == WL_CONNECTED;
  if (!up && now - lastTry >= WIFI_RETRY_MS) {
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    lastTry = now;
  }
  Guard g;
  link.wifi = up;
  link.rssi = up ? WiFi.RSSI() : 0;
  if (!up) link.backendOk = false;
}

void task(void *) {
  NetworkClientSecure tls;
  tls.useBuiltinCACertBundle();  // verify Supabase's certificate (no setInsecure)
  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  const String url = String(SUPABASE_URL) + "/rest/v1/rpc/device_report";

  uint32_t lastReport = 0, backoff = 0, outageStart = 0;
  bool hadOutage = false;
  for (;;) {
    uint32_t now = millis();
    maintainWifi(now);

    bool fast;
    uint8_t pendingAcks;
    {
      Guard g;
      fast = stateFast || ackCount > 0;
      pendingAcks = ackCount;
    }
    fast = fast || (lastCommandAt && now - lastCommandAt < 30000);
    uint32_t interval = backoff ? backoff : (fast ? REPORT_FAST_INTERVAL_MS : REPORT_INTERVAL_MS);
    if (pendingAcks && !backoff) interval = min(interval, (uint32_t)1000);

    if (WiFi.status() == WL_CONNECTED && (lastReport == 0 || now - lastReport >= interval)) {
      lastReport = now;
      uint8_t nAcks = 0, nEvents = 0;
      size_t len = buildBody(nAcks, nEvents);
      if (len) {
        http.begin(tls, url);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Accept", "application/json");
        http.addHeader("apikey", SUPABASE_ANON_KEY);
        http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
        int code = http.POST((uint8_t *)body, len);
        String resp = code > 0 ? http.getString() : String();
        http.end();
        if (code == 200) {
          dropSent(nAcks, nEvents);
          handleResponse(resp);
          backoff = 0;
          {
            Guard g;
            link.backendOk = true;
            link.lastOkMs = millis();
            link.lastError[0] = 0;
          }
          if (hadOutage) {
            char msg[100];
            snprintf(msg, sizeof(msg), "Cloud link restored after %lu s without contact",
                     (unsigned long)((millis() - outageStart) / 1000));
            pushEvent("info", "cloud_restored", msg);
            hadOutage = false;
          }
        } else {
          if (code > 0) setError("HTTP %d: %.60s", code, resp.c_str());
          else setError("Connection failed (%s)", http.errorToString(code).c_str());
          Serial.printf("[cloud] report failed: %s\n", status().lastError);
          backoff = backoff ? min(backoff * 2, REPORT_MAX_BACKOFF_MS) : 5000;
          {
            Guard g;
            link.backendOk = false;
          }
          if (!hadOutage) {
            hadOutage = true;
            outageStart = millis();
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

}  // namespace

void begin() {
  lock = xSemaphoreCreateMutex();
  commandQueue = xQueueCreate(8, sizeof(Command));
  xTaskCreatePinnedToCore(task, "cloud", 16384, nullptr, 1, nullptr, 0);
}

void publishState(const char *json, size_t len, bool fast) {
  Guard g;
  if (len >= STATE_MAX) return;
  memcpy(stateJson, json, len);
  stateJson[len] = 0;
  stateLen = len;
  stateFast = fast;
}

bool popCommand(Command &out) { return xQueueReceive(commandQueue, &out, 0) == pdTRUE; }

void pushAck(const char *id, bool accepted, const char *message) {
  Guard g;
  if (ackCount == MAX_ACKS) {  // keep the newest
    memmove(acks, acks + 1, (MAX_ACKS - 1) * sizeof(Ack));
    ackCount--;
  }
  Ack &a = acks[ackCount++];
  strlcpy(a.id, id, sizeof(a.id));
  a.accepted = accepted;
  strlcpy(a.message, message, sizeof(a.message));
}

void pushEvent(const char *severity, const char *kind, const char *message) {
  Guard g;
  if (eventCount == MAX_EVENTS) {  // keep the newest
    memmove(events, events + 1, (MAX_EVENTS - 1) * sizeof(Event));
    eventCount--;
  }
  Event &e = events[eventCount++];
  strlcpy(e.severity, severity, sizeof(e.severity));
  strlcpy(e.kind, kind, sizeof(e.kind));
  strlcpy(e.message, message, sizeof(e.message));
}

bool takeSettings(Settings &out) {
  Guard g;
  if (!settingsReady) return false;
  out = pendingSettings;
  settingsReady = false;
  return true;
}

LinkStatus status() {
  Guard g;
  return link;
}

}  // namespace cloud
