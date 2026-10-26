/*
 * DO_ESP32_DS18B20_WiFi.ino
 *
 * DFRobot Gravity Dissolved Oxygen sensor (SEN0237) on ESP32,
 * with one DS18B20 for temperature, serial calibration commands,
 * a Wi-Fi web dashboard, and low-DO relay control (relay IN1).
 *
 * DO conversion (DFRobot method):
 *   DO (mg/L) = V * DO_sat(T) / V_sat(T)
 *     V         = probe output voltage (mV)
 *     DO_sat(T) = saturated DO at temperature T (DFRobot table, 0-40 °C)
 *     V_sat(T)  = probe voltage in saturated water at T, from calibration:
 *       single-point: V_sat = V1 + 35 mV/°C * (T - T1)
 *       two-point   : V_sat = (T - T2) * (V1 - V2) / (T1 - T2) + V2
 *
 * Calibration commands (Serial Monitor OR the buttons on the web page):
 *   enterdo  -> enter calibration mode
 *   caldo    -> capture a calibration point at the current voltage + temperature
 *               (1st caldo = single-point; a 2nd caldo at least 5 °C away = two-point)
 *   exitdo   -> save calibration to flash and leave calibration mode
 *   wifi     -> (serial only) print the Wi-Fi status and the dashboard address
 * Calibration survives power-off (stored with Preferences / NVS).
 *
 * Relay IN1 (low-DO control, e.g. an aerator):
 *   - turns ON when DO < 3.0 mg/L for 3 readings in a row
 *   - turns OFF when DO > 3.0 mg/L for 3 readings in a row
 *     (the 3-reading confirmation and minimum times below stop it chattering)
 *   - stays ON at least 60 s and OFF at least 10 s between switches (protects the motor)
 *   - held as-is during calibration (the probe is out of the pond then)
 *   - a missing/invalid DO reading counts as LOW, so it fails toward aeration
 *   Serial test commands (return to auto after 10 min):
 *     relayon / relayoff -> force the relay,  relayauto -> back to automatic
 *
 * Wi-Fi (set in the "Wi-Fi settings" block below):
 *   The ESP32 joins your existing router; it does NOT make its own hotspot.
 *   Any phone or laptop on the same Wi-Fi can open the dashboard at the address
 *   printed on Serial (type "wifi" to print it again), or http://salimango-do.local
 *   If the router is down or the connection drops, the sensor keeps reading and
 *   logging on Serial, and the ESP32 keeps retrying until it reconnects.
 *   Optional: set USE_STATIC_IP so the address never changes.
 *
 * Temperature code: S.A.L.I.M.A.N.G.O. DS18B20 handling
 *   - non-blocking (asynchronous) conversions
 *   - invalid readings rejected (-127 disconnected, 85.0 power-on value, out of range)
 *   - falls back to 25 °C (flagged [TEMP_FAULT]) if the sensor faults
 *   - caldo is refused while the temperature sensor is faulted
 *
 * Required libraries (Arduino Library Manager):
 *   - OneWire
 *   - DallasTemperature
 * (WiFi, WebServer, ESPmDNS and Preferences are built into the ESP32 core)
 *
 * Serial Monitor: 115200 baud
 */

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <math.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>

// ---------------- Wi-Fi settings ----------------
const char *WIFI_SSID = "wala ba kayong wifi";      // router name (2.4 GHz; the ESP32 can't join 5 GHz)
const char *WIFI_PASS = "Fortuna369@192629";  // router password
const char *MDNS_NAME = "salimango-do";        // also reachable as http://salimango-do.local
constexpr uint32_t WIFI_BOOT_WAIT_MS  = 15000; // how long setup() waits for the first connection
constexpr uint32_t WIFI_RETRY_MS      = 10000; // retry interval while disconnected

// Optional fixed address. Leave false to get one from the router (DHCP).
// If true, pick an unused address in your router's range, e.g. 192.168.1.50
#define USE_STATIC_IP false
IPAddress STATIC_IP(192, 168, 1, 50);
IPAddress GATEWAY  (192, 168, 1, 1);
IPAddress SUBNET   (255, 255, 255, 0);
IPAddress DNS1     (8, 8, 8, 8);

// ---------------- Pins ----------------
#define DO_PIN        34   // ADC1_CH6, input-only. Use ADC1 pins only: ADC2 doesn't work while Wi-Fi is on
                           // (GPIO 35 is left free for the pH sensor)
constexpr uint8_t PIN_ONEWIRE = 19;   // 1-Wire bus, external 4.7 kOhm pull-up to 3.3 V
constexpr uint8_t PIN_RELAY1  = 26;   // relay module IN1

// ---------------- Relay settings ----------------
#define RELAY_ACTIVE_LOW   true     // most blue 5 V relay boards switch ON when IN is LOW
#define DO_RELAY_ON_BELOW  3.0f     // mg/L, relay turns ON below this
#define DO_RELAY_OFF_ABOVE 3.0f     // mg/L, relay turns OFF above this
#define RELAY_CONFIRM      3        // readings in a row needed before switching (~9 s)
#define RELAY_MIN_ON_MS    60000UL  // minimum ON time
#define RELAY_MIN_OFF_MS   10000UL  // minimum OFF time
#define RELAY_MANUAL_MS    600000UL // relayon/relayoff test override lasts 10 min

// ---------------- DO constants ----------------
#define DO_DIVIDER_RATIO   1.0f    // DO board outputs 0-3.0 V, fits the ESP32 ADC directly.
                                   // Change only if you put a divider in front of the pin.
#define DO_SAMPLES         10
#define READ_INTERVAL      3000UL  // ms, normal mode
#define CAL_READ_INTERVAL  1000UL  // ms, calibration mode (faster, to watch the voltage settle)

#define DEFAULT_CAL1_V     1600.0f // mV, DFRobot default (used until you calibrate)
#define DEFAULT_CAL1_T     25.0f   // °C
#define SAT_TEMP_COEF_MV   35.0f   // mV per °C, single-point temperature correction
#define MIN_TWO_POINT_SPAN 5.0f    // °C, minimum gap between the two calibration points
#define MIN_CAL_MV         100.0f  // reject caldo below this (probe unplugged / not ready)
#define MAX_CAL_MV         3100.0f // reject caldo above this (ADC at its limit)

// Saturated DO in water, 0..40 °C, in µg/L (DFRobot table)
const uint16_t DO_Table[41] = {
  14460, 14220, 13820, 13440, 13090, 12740, 12420, 12110, 11810, 11530,
  11260, 11010, 10770, 10530, 10300, 10080,  9860,  9660,  9460,  9270,
   9080,  8900,  8730,  8570,  8410,  8250,  8110,  7960,  7820,  7690,
   7560,  7430,  7300,  7180,  7070,  6950,  6840,  6730,  6630,  6530,
   6410
};

// ---------------- Temperature constants ----------------
constexpr uint32_t T_CONV_MS = 750;   // DS18B20 12-bit conversion time

// ============================ Objects ==================================
OneWire           oneWire(PIN_ONEWIRE);
DallasTemperature ds(&oneWire);
Preferences       prefs;
WebServer         server(80);

// ======================== Temperature state ============================
DeviceAddress tAddr;
bool     tPresent = false;
bool     tValid   = false;
float    tC       = NAN;
uint32_t tReqAt   = 0;

float temperature = 25.0;   // compensation temperature actually used

// ======================== Latest reading (for the web page) ============
float    lastMV  = NAN;
float    lastDO  = NAN;
bool     lastTOK = false;
uint32_t readSeq = 0;       // increments on every new reading

// ======================== DO calibration state =========================
struct DoCal {
  uint8_t points;   // 1 = single-point, 2 = two-point
  float   v1, t1;   // point 1 (the higher-temperature point in two-point mode)
  float   v2, t2;   // point 2 (the lower-temperature point)
};
DoCal cal;
bool  calIsDefault = true;

bool    calMode  = false;
uint8_t capCount = 0;
float   capV[2], capT[2];

// ======================== Relay state ==================================
enum RelayMode { RELAY_AUTO, RELAY_MANUAL };
bool      relayOn       = false;
RelayMode relayMode     = RELAY_AUTO;
uint32_t  relayChangeAt = 0;   // when the relay last switched
uint32_t  manualAt      = 0;   // when the manual override started
uint8_t   lowCount = 0, okCount = 0;

// ======================== Wi-Fi state ==================================
bool     wifiUp     = false; // true while joined to the router
uint32_t wifiTryAt  = 0;     // last connection attempt (ms)

// ======================== Message output ===============================
// Command messages go to Serial and are also collected for the web page.
String cmdOut;

void say(const char *fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(">>> ");
  Serial.println(buf);
  cmdOut += buf;
  cmdOut += '\n';
}

// ======================== Temperature helpers ==========================

// Uses the DS18B20 beside the DO probe; falls back to 25 °C if it faults.
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

// ============================ DO functions =============================

// Average of DO_SAMPLES readings, returned as the probe's output voltage (mV)
float readDOVoltage() {
  uint32_t sum = 0;
  for (int i = 0; i < DO_SAMPLES; i++) {
    sum += analogReadMilliVolts(DO_PIN);       // eFuse-calibrated millivolts
    delay(10);
  }
  return (sum / (float)DO_SAMPLES) * DO_DIVIDER_RATIO;
}

// Saturated DO (µg/L) at temperature t, linearly interpolated between table rows
float doSaturation(float t) {
  if (t <= 0.0f)  return DO_Table[0];
  if (t >= 40.0f) return DO_Table[40];
  int   i    = (int)t;
  float frac = t - i;
  return DO_Table[i] + (DO_Table[i + 1] - (float)DO_Table[i]) * frac;
}

// Probe voltage (mV) expected in saturated water at temperature t
float saturationVoltage(float t) {
  if (cal.points == 2)
    return (t - cal.t2) * (cal.v1 - cal.v2) / (cal.t1 - cal.t2) + cal.v2;
  return cal.v1 + SAT_TEMP_COEF_MV * (t - cal.t1);
}

// Dissolved oxygen in mg/L
float readDO(float voltage_mv, float t) {
  float vSat = saturationVoltage(t);
  if (vSat <= 0.0f) return NAN;
  return voltage_mv * doSaturation(t) / vSat / 1000.0f;
}

// ========================= Calibration storage =========================

void loadCal() {
  prefs.begin("do_cal", true);                 // read-only
  cal.points = prefs.getUChar("pts", 0);
  cal.v1 = prefs.getFloat("v1", DEFAULT_CAL1_V);
  cal.t1 = prefs.getFloat("t1", DEFAULT_CAL1_T);
  cal.v2 = prefs.getFloat("v2", 0.0f);
  cal.t2 = prefs.getFloat("t2", 0.0f);
  prefs.end();

  calIsDefault = (cal.points == 0);
  if (calIsDefault) {
    cal.points = 1;
    cal.v1 = DEFAULT_CAL1_V;
    cal.t1 = DEFAULT_CAL1_T;
  }
}

void saveCal() {
  prefs.begin("do_cal", false);
  prefs.putUChar("pts", cal.points);
  prefs.putFloat("v1", cal.v1);
  prefs.putFloat("t1", cal.t1);
  prefs.putFloat("v2", cal.v2);
  prefs.putFloat("t2", cal.t2);
  prefs.end();
  calIsDefault = false;
}

void sayCal() {
  if (cal.points == 2)
    say("DO calibration: two-point  V1=%.0fmV @ %.2f^C, V2=%.0fmV @ %.2f^C",
        cal.v1, cal.t1, cal.v2, cal.t2);
  else
    say("DO calibration: single-point  V1=%.0fmV @ %.2f^C%s",
        cal.v1, cal.t1, calIsDefault ? "  (DFRobot default, not calibrated)" : "");
}

// ============================ Relay ====================================

void relayWrite(bool on) {
  digitalWrite(PIN_RELAY1, (on ^ RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

void setRelay(bool on, const char *why) {
  if (on == relayOn) return;
  relayOn = on;
  relayChangeAt = millis();
  relayWrite(on);
  Serial.printf("RELAY IN1 %s (%s)\n", on ? "ON" : "OFF", why);
}

void setupRelay() {
  pinMode(PIN_RELAY1, OUTPUT);
  relayWrite(false);                           // start OFF
  relayChangeAt = millis();
  Serial.printf("  Relay IN1 on GPIO %u: ON below %.1f mg/L, OFF above %.1f mg/L\n",
                PIN_RELAY1, DO_RELAY_ON_BELOW, DO_RELAY_OFF_ABOVE);
}

// Called once per new DO reading
void updateRelay(float doMgL, uint32_t now) {
  if (relayMode == RELAY_MANUAL) {
    if (now - manualAt < RELAY_MANUAL_MS) return;
    relayMode = RELAY_AUTO;
    Serial.println(F("Relay manual override expired; back to automatic"));
  }
  if (calMode) { lowCount = okCount = 0; return; }   // probe is out of the pond: hold

  bool low = isnan(doMgL) || doMgL < DO_RELAY_ON_BELOW;   // no reading -> treat as low
  bool ok  = !isnan(doMgL) && doMgL > DO_RELAY_OFF_ABOVE;
  lowCount = low ? (lowCount < 255 ? lowCount + 1 : 255) : 0;
  okCount  = ok  ? (okCount  < 255 ? okCount  + 1 : 255) : 0;

  uint32_t held = now - relayChangeAt;
  if (!relayOn && lowCount >= RELAY_CONFIRM && held >= RELAY_MIN_OFF_MS)
    setRelay(true, "DO low");
  else if (relayOn && okCount >= RELAY_CONFIRM && held >= RELAY_MIN_ON_MS)
    setRelay(false, "DO recovered");
}

// ========================= Commands ====================================
// Each returns true on success; messages go through say().

bool cmdEnter() {
  if (calMode) { say("Already in DO calibration mode"); return false; }
  calMode  = true;
  capCount = 0;
  say("Entered DO calibration mode");
  sayCal();
  say("Put the probe in saturated conditions (wet probe held in air, or water aerated "
      "for several minutes). Wait for V to stop drifting, then caldo.");
  say("For two-point, caldo again in a second sample at least 5 ^C warmer/colder. "
      "exitdo saves and exits.");
  return true;
}

bool cmdCal() {
  if (!calMode) { say("Type enterdo first"); return false; }
  if (!tValid)  { say("caldo refused: temperature sensor fault (calibration needs a real temperature)"); return false; }

  float v = readDOVoltage();
  float t = tC;
  if (v < MIN_CAL_MV || v > MAX_CAL_MV) {
    say("caldo refused: V=%.0fmV is out of range (%.0f-%.0fmV). Check probe and wiring",
        v, MIN_CAL_MV, MAX_CAL_MV);
    return false;
  }

  if (capCount == 2) {
    say("Two points already captured; starting over from point 1");
    capCount = 0;
  }
  if (capCount == 1 && fabsf(t - capT[0]) < MIN_TWO_POINT_SPAN) {
    say("Point 2 refused: %.2f^C is within %.0f^C of point 1 (%.2f^C). "
        "Use a warmer/colder sample, or exitdo to save single-point",
        t, MIN_TWO_POINT_SPAN, capT[0]);
    return false;
  }

  capV[capCount] = v;
  capT[capCount] = t;
  capCount++;
  say("Point %u captured: V=%.0fmV @ %.2f^C", capCount, v, t);
  if (capCount == 1) say("exitdo to save single-point, or caldo in a second sample for two-point");
  else               say("Two-point ready. exitdo to save");
  return true;
}

bool cmdExit() {
  if (!calMode) { say("Not in calibration mode (type enterdo)"); return false; }
  calMode = false;

  if (capCount == 0) {
    say("No point captured; calibration unchanged");
  } else if (capCount == 1) {
    cal.points = 1;
    cal.v1 = capV[0]; cal.t1 = capT[0];
    cal.v2 = 0;       cal.t2 = 0;
    saveCal();
    say("Single-point calibration saved");
  } else {
    uint8_t hi = (capT[0] >= capT[1]) ? 0 : 1;  // point 1 = higher temperature
    uint8_t lo = 1 - hi;
    cal.points = 2;
    cal.v1 = capV[hi]; cal.t1 = capT[hi];
    cal.v2 = capV[lo]; cal.t2 = capT[lo];
    saveCal();
    say("Two-point calibration saved");
  }
  sayCal();
  say("Exited DO calibration mode");
  return true;
}

void cmdWifi() {
  if (WiFi.status() == WL_CONNECTED)
    say("Wi-Fi: joined \"%s\", signal %d dBm. Dashboard: http://%s  (or http://%s.local)",
        WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(), MDNS_NAME);
  else
    say("Wi-Fi: not connected to \"%s\"; retrying every %lu s. Check the name, password, "
        "and that the router is 2.4 GHz and in range", WIFI_SSID, (unsigned long)(WIFI_RETRY_MS / 1000));
}

// Runs one command; returns true on success. Output is left in cmdOut.
bool runCommand(const char *cmd) {
  cmdOut = "";
  if      (strcmp(cmd, "enterdo") == 0) return cmdEnter();
  else if (strcmp(cmd, "caldo")   == 0) return cmdCal();
  else if (strcmp(cmd, "exitdo")  == 0) return cmdExit();
  else if (strcmp(cmd, "wifi")    == 0) { cmdWifi(); return true; }
  else if (strcmp(cmd, "relayon") == 0 || strcmp(cmd, "relayoff") == 0) {
    relayMode = RELAY_MANUAL;
    manualAt  = millis();
    relayOn   = !relayOn;                      // force setRelay() to act and log
    setRelay(cmd[6] == 'n', "manual test");
    say("Relay IN1 forced %s for 10 min (relayauto to return to automatic)", relayOn ? "ON" : "OFF");
    return true;
  }
  else if (strcmp(cmd, "relayauto") == 0) {
    relayMode = RELAY_AUTO;
    lowCount = okCount = 0;
    say("Relay IN1 back to automatic");
    return true;
  }
  say("Unknown command \"%s\" (use enterdo, caldo, exitdo, wifi, relayon, relayoff, relayauto)", cmd);
  return false;
}

// Non-blocking line reader; commands are case-insensitive
void pollSerial() {
  static char    buf[24];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (len) { buf[len] = '\0'; runCommand(buf); len = 0; }
    } else if (c != ' ' && len < sizeof(buf) - 1) {
      buf[len++] = tolower(c);
    }
  }
}

// ============================ Web page =================================

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SALIMANGO DO Probe</title>
<style>
:root{
  --bg:#E9EEE8;--ink:#17302A;--muted:#5B6E66;--line:#C6D1CA;--panel:#F6F8F5;
  --pond:#16392F;--pond-ink:#EAF3EE;--pond-muted:#9DBBB0;--water:#5DB3AE;--water-top:#9AD8D2;
  --good:#2E8B57;--watch:#C58B16;--low:#C2453A;--btn:#16392F;--btn-ink:#F2F7F4;
}
@media (prefers-color-scheme:dark){:root{
  --bg:#0F1C18;--ink:#E3ECE7;--muted:#93A69D;--line:#2A3B35;--panel:#14251F;
  --pond:#173F34;--pond-ink:#EAF3EE;--pond-muted:#9DBBB0;--water:#4FA6A1;--water-top:#86CCC6;
  --good:#4DB37C;--watch:#E0A53A;--low:#E06A5E;--btn:#CFE5DA;--btn-ink:#0F1C18;
}}
*{box-sizing:border-box}
[hidden]{display:none!important}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--ink);
  font:16px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;
  font-variant-numeric:tabular-nums}
.wrap{max-width:960px;margin:0 auto;padding:16px 16px 40px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}
header h1{font-size:17px;font-weight:650;margin:0;letter-spacing:.01em}
header p{margin:0;color:var(--muted);font-size:13px}
.conn{display:flex;align-items:center;gap:8px;font-size:13px;color:var(--muted);white-space:nowrap}
.dot{width:9px;height:9px;border-radius:50%;background:var(--muted)}
.conn.live .dot{background:var(--good)}
.conn.lost .dot{background:var(--low)}
.banner{display:none;border-left:4px solid var(--low);background:var(--panel);
  padding:10px 14px;border-radius:6px;margin-bottom:14px;font-size:14px}
.banner.show{display:block}
.grid{display:grid;gap:14px}
@media (min-width:820px){.grid{grid-template-columns:1.15fr 1fr;align-items:start}}

/* hero */
.hero{background:var(--pond);color:var(--pond-ink);border-radius:18px;padding:22px 22px 20px;
  display:grid;grid-template-columns:1fr 64px;gap:18px;min-height:300px}
.hero .lab{color:var(--pond-muted);font-size:14px;margin:0 0 4px}
.reading{font-size:clamp(68px,20vw,104px);font-weight:700;line-height:.95;letter-spacing:-.03em;margin:0}
.unit{font-size:20px;font-weight:500;color:var(--pond-muted);margin-left:4px;letter-spacing:0}
.state{display:inline-flex;align-items:center;gap:8px;margin-top:16px;font-size:15px;font-weight:600}
.state i{width:12px;height:12px;border-radius:3px;background:var(--pond-muted)}
.state.good i{background:var(--good)}.state.watch i{background:var(--watch)}.state.low i{background:var(--low)}
.hint{color:var(--pond-muted);font-size:14px;margin:6px 0 0;max-width:34ch}
.calflag{display:none;margin-top:14px;font-size:13px;color:var(--pond-ink);
  border:1px solid var(--pond-muted);border-radius:999px;padding:3px 10px;width:max-content}
.calflag.show{display:block}
.sub{display:flex;gap:28px;margin-top:22px;flex-wrap:wrap}
.sub div{min-width:110px}
.sub b{display:block;font-size:26px;font-weight:650;letter-spacing:-.01em}
.sub span{color:var(--pond-muted);font-size:13px}
.sub b.fault{color:var(--low);font-size:18px;padding-top:6px}

/* gauge: a water column */
.tube{position:relative;border:2px solid var(--pond-muted);border-radius:30px;overflow:hidden;
  background:rgba(255,255,255,.04)}
.fill{position:absolute;left:0;right:0;bottom:0;height:0;background:var(--water);
  border-top:3px solid var(--water-top);transition:height .8s ease}
.tick{position:absolute;left:0;right:0;border-top:1px dashed var(--pond-ink);opacity:.55}
.tick span{position:absolute;right:4px;top:-17px;font-size:11px;color:var(--pond-ink)}
@media (prefers-reduced-motion:reduce){.fill{transition:none}}

/* panels */
.panel{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:18px}
.panel h2{font-size:16px;font-weight:650;margin:0 0 4px}
.panel .desc{color:var(--muted);font-size:14px;margin:0 0 14px}
canvas{width:100%;height:150px;display:block}
.trendfoot{display:flex;justify-content:space-between;color:var(--muted);font-size:12px;margin-top:6px}

/* calibration */
.current{font-size:14px;padding:10px 12px;border-radius:8px;background:var(--bg);margin-bottom:16px}
.current b{font-weight:600}
ol.steps{list-style:none;margin:0;padding:0;counter-reset:s}
ol.steps>li{counter-increment:s;display:grid;grid-template-columns:28px 1fr;gap:10px;
  padding:12px 0;border-top:1px solid var(--line)}
ol.steps>li::before{content:counter(s);width:26px;height:26px;border-radius:50%;
  border:1.5px solid var(--line);display:grid;place-items:center;font-size:13px;font-weight:650;color:var(--muted)}
ol.steps>li.active::before{border-color:var(--ink);color:var(--ink)}
ol.steps>li.done::before{background:var(--good);border-color:var(--good);color:#fff;content:"✓"}
ol.steps p{margin:2px 0 8px;color:var(--muted);font-size:14px}
ol.steps strong{font-size:15px}
button{font:inherit;font-size:15px;font-weight:600;border:0;border-radius:9px;padding:10px 16px;
  background:var(--btn);color:var(--btn-ink);cursor:pointer;min-height:44px}
button.ghost{background:transparent;color:var(--ink);border:1.5px solid var(--line)}
button:disabled{opacity:.38;cursor:not-allowed}
button:focus-visible{outline:3px solid var(--water);outline-offset:2px}
.cmd{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:12px;color:var(--muted);margin-left:8px}
.steady{font-size:14px;margin:0 0 10px;display:flex;gap:8px;align-items:center}
.steady i{width:10px;height:10px;border-radius:50%;background:var(--watch)}
.steady.ok i{background:var(--good)}
.points{margin:0 0 10px;padding:0;list-style:none;font-size:14px}
.points li{padding:4px 0}
.log{margin:0;padding:0;list-style:none;font-size:13px;max-height:220px;overflow:auto}
.log li{padding:7px 0;border-top:1px solid var(--line);white-space:pre-wrap}
.log li:first-child{border-top:0}
.log time{color:var(--muted);margin-right:8px}
.log li.err{color:var(--low)}
.empty{color:var(--muted);font-size:14px}
footer{margin-top:18px;color:var(--muted);font-size:13px;display:flex;flex-wrap:wrap;gap:6px 20px}
.stack{display:grid;gap:14px}
</style>
</head>
<body>
<div class="wrap">
  <header>
    <div>
      <h1>S.A.L.I.M.A.N.G.O. dissolved oxygen</h1>
      <p>Probe on this ESP32, read over Wi‑Fi</p>
    </div>
    <div class="conn" id="conn"><span class="dot"></span><span id="connTxt">Connecting</span></div>
  </header>

  <div class="banner" id="lost">Can't reach the probe. Check that this device is on the same Wi‑Fi as the probe and that the ESP32 is powered, then wait a few seconds.</div>
  <div class="banner" id="tfault">Temperature sensor fault. DO is being worked out at 25 °C, so treat it as approximate. Calibration is paused until the DS18B20 reads again; check its wiring and the 4.7 kΩ pull-up.</div>

  <div class="grid">
    <div class="stack">
      <section class="hero" aria-live="polite">
        <div>
          <p class="lab">Dissolved oxygen</p>
          <p class="reading"><span id="do">--</span><span class="unit">mg/L</span></p>
          <div class="state" id="state"><i></i><span id="stateTxt">Waiting for a reading</span></div>
          <p class="hint" id="stateHint"></p>
          <div class="calflag" id="calflag">Calibration in progress</div>
          <div class="sub">
            <div><b id="temp">--</b><span>Water temperature, °C</span></div>
            <div><b id="mv">--</b><span>Probe voltage, mV</span></div>
            <div><b id="relay">--</b><span id="relayTxt">Relay IN1</span></div>
          </div>
        </div>
        <div class="tube" aria-hidden="true">
          <div class="fill" id="fill"></div>
          <div class="tick" id="tickWatch"><span>5</span></div>
          <div class="tick" id="tickLow"><span>3</span></div>
        </div>
      </section>

      <section class="panel">
        <h2>Recent readings</h2>
        <p class="desc">Dissolved oxygen since this page was opened. Dashed lines mark 3 and 5 mg/L.</p>
        <canvas id="trend" aria-label="Dissolved oxygen trend"></canvas>
        <div class="trendfoot"><span id="trendSpan">No readings yet</span><span id="trendRange"></span></div>
      </section>
    </div>

    <div class="stack">
      <section class="panel">
        <h2>Calibrate the probe</h2>
        <p class="desc">Same steps as the serial commands. Use saturated conditions: the wet probe held in air, or water aerated for several minutes.</p>
        <div class="current" id="current">Loading calibration…</div>

        <ol class="steps">
          <li id="s1">
            <div>
              <strong>Start calibration</strong>
              <p>Readings speed up to once a second so you can watch the voltage settle.</p>
              <button id="bEnter" disabled>Start calibration</button><span class="cmd">enterdo</span>
            </div>
          </li>
          <li id="s2">
            <div>
              <strong>Capture a point</strong>
              <p>Capture once for single-point. For two-point, capture again in a second sample at least 5 °C warmer or colder.</p>
              <p class="steady" id="steady" hidden><i></i><span id="steadyTxt"></span></p>
              <ul class="points" id="points"></ul>
              <button id="bCal" disabled>Capture point</button><span class="cmd">caldo</span>
            </div>
          </li>
          <li id="s3">
            <div>
              <strong>Save and finish</strong>
              <p>Saves to the ESP32's flash, so it is kept after power-off.</p>
              <button id="bExit" class="ghost" disabled>Save and finish</button><span class="cmd">exitdo</span>
            </div>
          </li>
        </ol>
      </section>

      <section class="panel">
        <h2>Activity</h2>
        <ul class="log" id="log"><li class="empty">Calibration messages will show here.</li></ul>
      </section>
    </div>
  </div>

  <footer id="net"></footer>
</div>

<script>
// Thresholds for the status line and gauge marks (mg/L). Set these to your pond's targets.
const DO_LOW = 3.0, DO_WATCH = 5.0, GAUGE_MAX = 15.0;
const HISTORY = 300;           // readings kept for the trend chart
const $ = id => document.getElementById(id);
let st = null, hist = [], mvHist = [], busy = false, lastUp = -1, failCount = 0;

function fmt(v, d){ return (v === null || v === undefined || isNaN(v)) ? '--' : Number(v).toFixed(d); }

function placeTicks(){
  $('tickLow').style.bottom   = (DO_LOW   / GAUGE_MAX * 100) + '%';
  $('tickWatch').style.bottom = (DO_WATCH / GAUGE_MAX * 100) + '%';
}

function statusFor(v){
  if (v === null || isNaN(v)) return ['', 'No reading', 'Check that the DO probe is plugged into the signal board.'];
  if (v < DO_LOW)   return ['low',   'Low oxygen',      'Below ' + DO_LOW + ' mg/L. Check aeration and water exchange.'];
  if (v < DO_WATCH) return ['watch', 'Watch closely',   'Between ' + DO_LOW + ' and ' + DO_WATCH + ' mg/L.'];
  return ['good', 'Good for the pond', DO_WATCH + ' mg/L or more.'];
}

function calText(c){
  if (!c) return '';
  if (c.pts === 2) return '<b>Two-point</b>: ' + fmt(c.v1,0) + ' mV at ' + fmt(c.t1,2) + ' °C and ' + fmt(c.v2,0) + ' mV at ' + fmt(c.t2,2) + ' °C';
  return '<b>Single-point</b>: ' + fmt(c.v1,0) + ' mV at ' + fmt(c.t1,2) + ' °C' + (c.def ? ' (factory default, not calibrated yet)' : '');
}

function log(msg, err){
  const ul = $('log');
  const e = ul.querySelector('.empty'); if (e) e.remove();
  const li = document.createElement('li');
  if (err) li.className = 'err';
  const t = document.createElement('time');
  t.textContent = new Date().toLocaleTimeString([], {hour:'2-digit', minute:'2-digit', second:'2-digit'});
  li.append(t, document.createTextNode(msg.trim()));
  ul.prepend(li);
  while (ul.children.length > 40) ul.lastChild.remove();
}

function setConn(ok){
  const c = $('conn');
  c.classList.toggle('live', ok); c.classList.toggle('lost', !ok);
  $('connTxt').textContent = ok ? 'Live' : 'Offline';
  $('lost').classList.toggle('show', !ok);
}

function render(d){
  st = d;
  $('do').textContent = fmt(d.do, 2);
  $('mv').textContent = fmt(d.mv, 0);
  if (d.relay){
    $('relay').textContent = d.relay.on ? 'On' : 'Off';
    $('relayTxt').textContent = 'Relay IN1, ' + (d.relay.mode === 'manual' ? 'manual test'
      : d.relay.mode === 'hold' ? 'held while calibrating'
      : 'on below ' + fmt(d.relay.onBelow,1) + ' mg/L');
  }
  const tb = $('temp');
  if (d.tOk){ tb.textContent = fmt(d.t, 2); tb.classList.remove('fault'); }
  else { tb.textContent = 'Sensor fault'; tb.classList.add('fault'); }
  $('tfault').classList.toggle('show', !d.tOk);

  const [cls, txt, hint] = statusFor(d.do);
  $('state').className = 'state ' + cls; $('stateTxt').textContent = txt; $('stateHint').textContent = hint;
  const pct = (d.do === null || isNaN(d.do)) ? 0 : Math.max(0, Math.min(1, d.do / GAUGE_MAX)) * 100;
  $('fill').style.height = pct + '%';

  // trend history: one entry per new reading on the ESP32
  if (d.seq !== lastUp){
    lastUp = d.seq;
    if (d.do !== null && !isNaN(d.do)) { hist.push({t: Date.now(), v: d.do}); if (hist.length > HISTORY) hist.shift(); }
    if (d.mv !== null && !isNaN(d.mv)) { mvHist.push(d.mv); if (mvHist.length > 10) mvHist.shift(); }
    drawTrend();
  }

  // calibration panel
  const c = d.cal;
  $('current').innerHTML = 'Current calibration: ' + calText(c);
  $('calflag').classList.toggle('show', c.mode);
  $('s1').className = c.mode ? 'done' : 'active';
  $('s2').className = !c.mode ? '' : (c.cap > 0 ? 'done' : 'active');
  $('s3').className = c.mode && c.cap > 0 ? 'active' : '';
  $('bEnter').disabled = busy || c.mode;
  $('bCal').disabled   = busy || !c.mode || !d.tOk;
  $('bExit').disabled  = busy || !c.mode;
  $('bExit').textContent = !c.mode ? 'Save and finish' : (c.cap === 0 ? 'Leave without changes' : (c.cap === 1 ? 'Save single-point' : 'Save two-point'));
  $('bExit').className = c.mode && c.cap > 0 ? '' : 'ghost';
  $('bCal').textContent = c.cap === 1 ? 'Capture second point' : (c.cap === 2 ? 'Capture again (starts over)' : 'Capture point');

  const pts = $('points'); pts.innerHTML = '';
  (c.capPts || []).forEach((p, i) => {
    const li = document.createElement('li');
    li.textContent = 'Point ' + (i + 1) + ': ' + fmt(p.v,0) + ' mV at ' + fmt(p.t,2) + ' °C';
    pts.append(li);
  });

  // voltage steadiness, only useful while calibrating
  const sd = $('steady');
  sd.hidden = !c.mode;
  if (c.mode){
    if (mvHist.length < 5){ sd.className = 'steady'; $('steadyTxt').textContent = 'Collecting readings…'; }
    else {
      const span = Math.max(...mvHist) - Math.min(...mvHist);
      const ok = span <= 10;
      sd.className = 'steady' + (ok ? ' ok' : '');
      $('steadyTxt').textContent = ok
        ? 'Voltage steady (varies by ' + span.toFixed(0) + ' mV). Ready to capture.'
        : 'Still settling (varies by ' + span.toFixed(0) + ' mV over the last ' + mvHist.length + ' readings).';
    }
  }

  const n = d.net || {};
  const up = d.up || 0, h = Math.floor(up / 3600), m = Math.floor(up % 3600 / 60);
  $('net').innerHTML = '';
  [ 'Wi‑Fi ' + (n.ssid || ''),
    'Address ' + (n.ip || ''),
    'Signal ' + n.rssi + ' dBm',
    'Running for ' + (h ? h + ' h ' : '') + m + ' min' ].filter(Boolean)
    .forEach(s => { const sp = document.createElement('span'); sp.textContent = s; $('net').append(sp); });
}

function drawTrend(){
  const cv = $('trend'), dpr = window.devicePixelRatio || 1;
  const w = cv.clientWidth, h = cv.clientHeight;
  cv.width = w * dpr; cv.height = h * dpr;
  const g = cv.getContext('2d'); g.scale(dpr, dpr); g.clearRect(0, 0, w, h);
  const css = getComputedStyle(document.documentElement);
  const line = css.getPropertyValue('--line').trim(), muted = css.getPropertyValue('--muted').trim();
  const ink = css.getPropertyValue('--water').trim();
  const top = Math.max(GAUGE_MAX * 0.6, ...hist.map(p => p.v)) * 1.05;
  const y = v => h - 4 - (v / top) * (h - 8);
  g.setLineDash([4, 4]); g.strokeStyle = muted; g.lineWidth = 1;
  [DO_LOW, DO_WATCH].forEach(v => { g.beginPath(); g.moveTo(0, y(v)); g.lineTo(w, y(v)); g.stroke(); });
  g.setLineDash([]); g.strokeStyle = line; g.beginPath(); g.moveTo(0, h - 0.5); g.lineTo(w, h - 0.5); g.stroke();
  if (hist.length < 2){ $('trendSpan').textContent = hist.length ? 'Waiting for more readings' : 'No readings yet'; $('trendRange').textContent = ''; return; }
  const n = hist.length, x = i => 4 + (i / (n - 1)) * (w - 8);
  g.strokeStyle = ink; g.lineWidth = 2.25; g.lineJoin = 'round'; g.beginPath();
  hist.forEach((p, i) => i ? g.lineTo(x(i), y(p.v)) : g.moveTo(x(i), y(p.v))); g.stroke();
  const lp = hist[n - 1]; g.fillStyle = ink; g.beginPath(); g.arc(x(n - 1), y(lp.v), 3.5, 0, 7); g.fill();
  const mins = Math.max(1, Math.round((lp.t - hist[0].t) / 60000));
  const vs = hist.map(p => p.v);
  $('trendSpan').textContent = 'Last ' + mins + ' min';
  $('trendRange').textContent = 'Low ' + Math.min(...vs).toFixed(2) + ', high ' + Math.max(...vs).toFixed(2) + ' mg/L';
}

async function poll(){
  try {
    const ctl = new AbortController(); const to = setTimeout(() => ctl.abort(), 4000);
    const r = await fetch('/api/data', {cache: 'no-store', signal: ctl.signal});
    clearTimeout(to);
    if (!r.ok) throw new Error(r.status);
    render(await r.json()); failCount = 0; setConn(true);
  } catch (e) { if (++failCount >= 2) setConn(false); }
  setTimeout(poll, st && st.cal && st.cal.mode ? 1000 : 2000);
}

async function send(cmd){
  busy = true; if (st) render(st);
  try {
    const r = await fetch('/api/cmd', {method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: 'c=' + cmd});
    const j = await r.json();
    log(j.msg || cmd, !j.ok);
    if (cmd === 'enterdo') mvHist = [];
  } catch (e) { log('Could not send ' + cmd + '. The probe did not answer; try again.', true); }
  busy = false;
  try { const r = await fetch('/api/data', {cache: 'no-store'}); render(await r.json()); } catch (e) {}
}

$('bEnter').onclick = () => send('enterdo');
$('bCal').onclick   = () => send('caldo');
$('bExit').onclick  = () => send('exitdo');
window.addEventListener('resize', drawTrend);
placeTicks(); poll();
</script>
</body>
</html>
)rawliteral";

// ============================ Web server ===============================

// Appends a float as JSON (null for NaN)
void jsonNum(String &s, const char *key, float v, uint8_t dec, bool comma = true) {
  s += '"'; s += key; s += "\":";
  if (isnan(v)) s += "null"; else s += String(v, (unsigned int)dec);
  if (comma) s += ',';
}

String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if      (c == '"')  out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if ((uint8_t)c < 0x20) out += ' ';
    else out += c;
  }
  return out;
}

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleData() {
  String s;
  s.reserve(512);
  s += "{\"seq\":"; s += readSeq; s += ',';
  jsonNum(s, "t", tValid ? tC : NAN, 2);
  s += "\"tOk\":"; s += lastTOK ? "true" : "false"; s += ',';
  jsonNum(s, "tComp", temperature, 2);
  jsonNum(s, "mv", lastMV, 0);
  jsonNum(s, "do", lastDO, 2);
  s += "\"up\":"; s += (uint32_t)(millis() / 1000); s += ',';

  s += "\"cal\":{\"mode\":"; s += calMode ? "true" : "false";
  s += ",\"cap\":"; s += capCount;
  s += ",\"capPts\":[";
  for (uint8_t i = 0; i < capCount; i++) {
    if (i) s += ',';
    s += "{\"v\":"; s += String(capV[i], 0u); s += ",\"t\":"; s += String(capT[i], 2u); s += '}';
  }
  s += "],\"pts\":"; s += cal.points; s += ',';
  jsonNum(s, "v1", cal.v1, 0);
  jsonNum(s, "t1", cal.t1, 2);
  jsonNum(s, "v2", cal.v2, 0);
  jsonNum(s, "t2", cal.t2, 2);
  s += "\"def\":"; s += calIsDefault ? "true" : "false"; s += "},";

  s += "\"relay\":{\"on\":"; s += relayOn ? "true" : "false";
  s += ",\"mode\":\""; s += relayMode == RELAY_MANUAL ? "manual" : (calMode ? "hold" : "auto");
  s += "\","; jsonNum(s, "onBelow", DO_RELAY_ON_BELOW, 1); jsonNum(s, "offAt", DO_RELAY_OFF_ABOVE, 1, false);
  s += "},";

  s += "\"net\":{\"ssid\":\""; s += jsonEscape(WiFi.SSID());
  s += "\",\"ip\":\""; s += WiFi.localIP().toString();
  s += "\",\"rssi\":"; s += WiFi.RSSI();
  s += "}}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void handleCmd() {
  String c = server.arg("c");
  c.trim();
  c.toLowerCase();
  if (c != "enterdo" && c != "caldo" && c != "exitdo") {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Unknown command\"}");
    return;
  }
  bool ok = runCommand(c.c_str());
  String s = "{\"ok\":";
  s += ok ? "true" : "false";
  s += ",\"msg\":\""; s += jsonEscape(cmdOut); s += "\"}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void startMDNS() {
  MDNS.end();
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
}

void announceWiFi() {
  Serial.printf("  Wi-Fi joined \"%s\" (signal %d dBm)\n", WiFi.SSID().c_str(), WiFi.RSSI());
  Serial.printf("  Dashboard: http://%s\n", WiFi.localIP().toString().c_str());
  Serial.printf("  Also at:   http://%s.local\n", MDNS_NAME);
}

void setupWiFi() {
  WiFi.persistent(false);
  WiFi.setHostname(MDNS_NAME);                 // name shown in the router's device list
  WiFi.mode(WIFI_STA);                         // join the router only, no hotspot
  WiFi.setSleep(false);                        // keeps the web page responsive
  WiFi.setAutoReconnect(true);
#if USE_STATIC_IP
  if (!WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS1))
    Serial.println(F("  Static IP setup failed; using the router's address instead"));
#endif

  Serial.printf("\nJoining Wi-Fi \"%s\"", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiTryAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiTryAt < WIFI_BOOT_WAIT_MS) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  wifiUp = (WiFi.status() == WL_CONNECTED);
  if (wifiUp) { startMDNS(); announceWiFi(); }
  else Serial.println(F("  Not connected yet. Sensor keeps running; retrying in the background "
                        "(type wifi to check)"));

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/data", HTTP_GET, handleData);
  server.on("/api/cmd", HTTP_POST, handleCmd);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

// Watches the connection: reports drops/reconnects and retries while down
void maintainWiFi(uint32_t now) {
  bool up = (WiFi.status() == WL_CONNECTED);
  if (up && !wifiUp) {                         // just (re)connected
    Serial.println(F("\nWi-Fi reconnected"));
    startMDNS();
    announceWiFi();
  } else if (!up && wifiUp) {                  // just dropped
    Serial.println(F("\nWi-Fi connection lost; retrying"));
    wifiTryAt = now;
  }
  wifiUp = up;

  if (!up && now - wifiTryAt >= WIFI_RETRY_MS) {
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    wifiTryAt = now;
  }
}

// =========================== setup / loop ==============================

void setup() {
  pinMode(PIN_RELAY1, OUTPUT);                 // drive the relay OFF as early as possible
  relayWrite(false);
  Serial.begin(115200);
  delay(500);

  // ADC: 12-bit, 11 dB attenuation (~0.15 to ~3.1 V usable range)
  analogReadResolution(12);
  analogSetPinAttenuation(DO_PIN, ADC_11db);

  setupRelay();
  setupTemps();
  loadCal();
  sayCal();
  setupWiFi();
  Serial.println(F("  Commands: enterdo, caldo, exitdo, wifi, relayon, relayoff, relayauto\n"));
}

void loop() {
  static unsigned long timepoint = millis();
  uint32_t now = millis();

  pollTemps(now);
  pollSerial();
  maintainWiFi(now);
  server.handleClient();

  unsigned long interval = calMode ? CAL_READ_INTERVAL : READ_INTERVAL;
  if (now - timepoint > interval) {
    timepoint = now;

    bool  tOK     = compensationTemp(temperature);
    float voltage = readDOVoltage();
    float doMgL   = readDO(voltage, temperature);

    lastMV  = voltage;
    lastDO  = doMgL;
    lastTOK = tOK;
    readSeq++;
    updateRelay(doMgL, now);

    char s1[10];
    fmtTemp(s1, sizeof(s1));

    if (calMode) Serial.print("[CAL] ");
    Serial.printf("T=%s Tcomp=%.2f^C  V:%.0fmV  DO=%.2f mg/L", s1, temperature, voltage, doMgL);
    Serial.printf("  Relay=%s", relayOn ? "ON" : "OFF");
    if (relayMode == RELAY_MANUAL) Serial.print("(manual)");
    if (!tOK) Serial.print(" [TEMP_FAULT]");
    Serial.println();
  }
}
