/*
  S.A.L.I.M.A.N.G.O. - Relay Test K1-K4 over 2.4 GHz Wi-Fi (ESP32 #1, float node)
  File: relay_wifi_test_k1k4.ino
  -------------------------------------------------------------------------
  Controls K1-K4 from a web page on the same Wi-Fi network.
  Serial commands still work (115200 baud).

  5 V 8-channel relay module (SRD-05VDC), ACTIVE-LOW (LOW = ON).
    K1  IN1  GPIO 25  Air pump 1
    K2  IN2  GPIO 27  Air pump 2
    K3  IN3  GPIO 33  Bilge pump 1
    K4  IN4  GPIO 13  Bilge pump 2

  Notes:
    - ESP32 supports 2.4 GHz Wi-Fi only (not 5 GHz).
    - GPIO 25/27/13 are ADC2 pins. Wi-Fi only blocks ADC2 *analog reads*;
      digital output to the relays is not affected. The analog sensors
      (pH 34, EC 35, DO 36) are on ADC1 and also work with Wi-Fi on.
    - The page has NO password. Use it only on your own private network
      for bench testing.

  How to use:
    1. Put your Wi-Fi name and password below.
    2. Upload with the laptop USB (buck USB unplugged).
    3. Open Serial Monitor (115200). Note the IP address printed.
    4. On a phone/laptop on the SAME Wi-Fi, open  http://<IP>/
       or try  http://salimango.local/

  Serial commands: 1-4 toggle | 0 all OFF | a all ON | p status | i IP
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ---------- Wi-Fi credentials (edit these) ----------
const char* WIFI_SSID = "wala ba kayong wifi";
const char* WIFI_PASS = "Fortuna369@192629";
const char* MDNS_NAME = "salimango";            // -> http://salimango.local/

// ---------- Relays ----------
const uint8_t NUM_RELAYS = 4;
const uint8_t RELAY_PINS[NUM_RELAYS] = {25, 27, 33, 13};
const char* RELAY_NAMES[NUM_RELAYS] = {
  "K1 Air pump 1", "K2 Air pump 2", "K3 Bilge 1", "K4 Bilge 2"
};
bool relayOn[NUM_RELAYS] = {false, false, false, false};

WebServer server(80);

// ---------- Wi-Fi reconnect (non-blocking) ----------
const unsigned long WIFI_RETRY_MS = 10000;
unsigned long lastWifiAttempt = 0;
bool wasConnected = false;
bool mdnsStarted = false;

// ---------------------------------------------------------------
void setRelay(uint8_t i, bool on) {
  if (i >= NUM_RELAYS) return;
  relayOn[i] = on;
  digitalWrite(RELAY_PINS[i], on ? LOW : HIGH);   // active-LOW
}

void setAll(bool on) {
  for (uint8_t i = 0; i < NUM_RELAYS; i++) setRelay(i, on);
}

String statusJson() {
  String s = "{\"relays\":[";
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    if (i) s += ",";
    s += "{\"ch\":" + String(i + 1) +
         ",\"name\":\"" + RELAY_NAMES[i] + "\"" +
         ",\"gpio\":" + String(RELAY_PINS[i]) +
         ",\"on\":" + (relayOn[i] ? "true" : "false") + "}";
  }
  s += "],\"rssi\":" + String(WiFi.RSSI()) + "}";
  return s;
}

void printStatus() {
  Serial.println(F("--- Relay status ---"));
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    Serial.printf("%s (GPIO %u): %s\n", RELAY_NAMES[i], RELAY_PINS[i],
                  relayOn[i] ? "ON" : "OFF");
  }
}

void printIP() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Open: http://"));
    Serial.print(WiFi.localIP());
    Serial.printf("/   or   http://%s.local/\n", MDNS_NAME);
  } else {
    Serial.println(F("Wi-Fi not connected."));
  }
}

// ---------- Web page ----------
const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Relay Test</title>
<style>
 body{font-family:system-ui,sans-serif;max-width:420px;margin:24px auto;padding:0 16px;background:#f4f6f8;color:#1b1f24}
 h1{font-size:20px;margin:0 0 4px}
 .sub{color:#5b6570;font-size:13px;margin-bottom:16px}
 .row{display:flex;align-items:center;justify-content:space-between;background:#fff;border-radius:10px;padding:12px 14px;margin:8px 0;box-shadow:0 1px 2px rgba(0,0,0,.08)}
 .name{font-weight:600}.gpio{font-size:12px;color:#5b6570}
 button{border:0;border-radius:8px;padding:10px 16px;font-size:15px;font-weight:600;cursor:pointer;min-width:84px}
 .on{background:#1f9d55;color:#fff}.off{background:#d5dbe1;color:#1b1f24}
 .all{display:flex;gap:8px;margin-top:14px}.all button{flex:1}
 .alloff{background:#c0392b;color:#fff}.allon{background:#1f6fb2;color:#fff}
 #msg{font-size:12px;color:#5b6570;margin-top:12px}
</style></head><body>
<h1>Relay Test (K1–K4)</h1>
<div class="sub">S.A.L.I.M.A.N.G.O. float node · active-LOW</div>
<div id="list"></div>
<div class="all">
 <button class="allon" onclick="allSet(1)">All ON</button>
 <button class="alloff" onclick="allSet(0)">All OFF</button>
</div>
<div id="msg">Loading…</div>
<script>
async function load(){
 try{
  const r=await fetch('/api/status');const d=await r.json();
  document.getElementById('list').innerHTML=d.relays.map(x=>
   `<div class="row"><div><div class="name">${x.name}</div><div class="gpio">IN${x.ch} · GPIO ${x.gpio}</div></div>
    <button class="${x.on?'on':'off'}" onclick="tog(${x.ch})">${x.on?'ON':'OFF'}</button></div>`).join('');
  document.getElementById('msg').textContent='Connected · Wi-Fi signal '+d.rssi+' dBm';
 }catch(e){document.getElementById('msg').textContent='Not reachable. Check Wi-Fi.';}
}
async function tog(ch){await fetch('/api/toggle?ch='+ch);load();}
async function allSet(v){await fetch('/api/all?on='+v);load();}
load();setInterval(load,2000);
</script></body></html>
)HTML";

// ---------- HTTP handlers ----------
void handleRoot()   { server.send_P(200, "text/html", PAGE); }
void handleStatus() { server.send(200, "application/json", statusJson()); }

void handleToggle() {
  int ch = server.arg("ch").toInt();             // 1..4
  if (ch < 1 || ch > NUM_RELAYS) {
    server.send(400, "application/json", "{\"error\":\"ch must be 1-4\"}");
    return;
  }
  uint8_t i = ch - 1;
  setRelay(i, !relayOn[i]);
  Serial.printf("[WEB] %s -> %s\n", RELAY_NAMES[i], relayOn[i] ? "ON" : "OFF");
  server.send(200, "application/json", statusJson());
}

void handleAll() {
  bool on = server.arg("on") == "1";
  setAll(on);
  Serial.printf("[WEB] All relays %s\n", on ? "ON" : "OFF");
  server.send(200, "application/json", statusJson());
}

// ---------- Wi-Fi ----------
void startWifi() {
  WiFi.mode(WIFI_STA);
  // Lower Wi-Fi TX power to reduce current spikes (helps prevent brownout
  // resets when powered from USB). Enough range for indoor bench testing.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastWifiAttempt = millis();
  Serial.printf("Connecting to Wi-Fi \"%s\"...\n", WIFI_SSID);
}

void serviceWifi() {
  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected && !wasConnected) {              // just connected
    Serial.println(F("Wi-Fi connected."));
    if (!mdnsStarted && MDNS.begin(MDNS_NAME)) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
    }
    printIP();
  }
  if (!connected && wasConnected) {
    Serial.println(F("Wi-Fi lost. Retrying..."));
  }
  if (!connected && millis() - lastWifiAttempt >= WIFI_RETRY_MS) {
    lastWifiAttempt = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  wasConnected = connected;
}

// ---------- Serial ----------
void serviceSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == '\n' || c == '\r' || c == ' ') return;

  if (c >= '1' && c <= '4') {
    uint8_t i = c - '1';
    setRelay(i, !relayOn[i]);
    Serial.printf("%s -> %s\n", RELAY_NAMES[i], relayOn[i] ? "ON" : "OFF");
  } else if (c == '0') { setAll(false); Serial.println(F("All relays OFF")); }
  else if (c == 'a' || c == 'A') { setAll(true); Serial.println(F("All relays ON")); }
  else if (c == 'p' || c == 'P') { printStatus(); }
  else if (c == 'i' || c == 'I') { printIP(); }
  else { Serial.printf("Unknown command: '%c'\n", c); }
}

// ---------------------------------------------------------------
void setup() {
  // Relays OFF first (pinMode, then HIGH) - works on core 2.x and 3.x.
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], HIGH);
    relayOn[i] = false;
  }

  Serial.begin(115200);
  Serial.println();
  Serial.println(F("=== Relay Test over Wi-Fi (K1-K4, active-LOW) ==="));

  startWifi();

  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/toggle", handleToggle);
  server.on("/api/all", handleAll);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

void loop() {
  serviceWifi();          // non-blocking reconnect
  server.handleClient();  // web requests
  serviceSerial();        // serial commands
}
