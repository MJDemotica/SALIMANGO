/*
  S.A.L.I.M.A.N.G.O. — EC calibration + dual DS18B20 temperature compensation
  WiFi edition: live web page + calibration buttons (bench tool only)
  ---------------------------------------------------------------------------
  Target   : ESP32 DevKit 38-pin (ESP32-WROOM-32), Arduino-ESP32 core 2.x / 3.x
  Sensor   : DFRobot Gravity Analog EC Sensor V2, K=1 (DFR0300), 0–3.4 V output
  Temp     : 2x waterproof DS18B20 on one shared 1-Wire bus
  Pins     : EC analog  -> GPIO 35 (ADC1_CH7, input-only)      [WIRING.md §3]
             1-Wire bus -> GPIO 19, external 4.7 kOhm pull-up to 3.3 V
  Libraries: OneWire, DallasTemperature (Library Manager);
             Preferences, WiFi, WebServer, ESPmDNS (built into the ESP32 core)

  WiFi
    Default : joins the home WiFi set in STA_SSID / STA_PASS (2.4 GHz only).
              The Serial Monitor prints the address, e.g. http://192.168.1.37;
              http://salimango.local also works on most laptops.
    Fallback: if the home WiFi cannot be joined within 20 s, the ESP32 starts
              its own hotspot "SALIMANGO-CAL" (password "salimango2026") at
              http://192.168.4.1, so the page is never unreachable.
    Option  : set WIFI_USE_STA to 0 to always use the hotspot.
    EC stays on ADC1 (GPIO 35), which keeps working while WiFi runs.
    The float node keeps WiFi OFF in the deployed design (FIRMWARE.md); this
    sketch is a bench calibration tool only.

  Serial Monitor (still works): 115200 baud, line ending = "Newline".
  Commands: help | enter | cal | exit | abort | show | reset | wifion | wifioff
  The web page offers the same calibration commands (wifion/wifioff are
  Serial-only so the page cannot cut its own connection).
*/

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <stdarg.h>
#include <math.h>

// ============================ WiFi settings ============================
#define WIFI_USE_STA 1                        // 1 = join the home WiFi (default), 0 = own hotspot
const char *STA_SSID = "GlobeAtHome_3BFD9_2.4G"; // <-- type your home WiFi name here (case-sensitive)
const char *STA_PASS = "yarekakaymean@2.4G"; // <-- type your home WiFi password here
const char *MDNS_NAME = "salimango";          // page also reachable at http://salimango.local
const uint32_t STA_TIMEOUT_MS = 20000;        // no connection after 20 s -> fallback hotspot
const char *AP_SSID  = "SALIMANGO-CAL";       // fallback / WIFI_USE_STA 0 hotspot
const char *AP_PASS  = "salimango2026";       // at least 8 characters

// ===================== Pin map (docs/WIRING.md §3) =====================
constexpr uint8_t PIN_EC      = 35;
constexpr uint8_t PIN_ONEWIRE = 19;

// ============ DFR0300 (K=1) constants — DFRobot_EC algorithm ===========
constexpr float RES2        = 820.0f;
constexpr float ECREF       = 200.0f;
constexpr float TEMP_COEF   = 0.0185f;   // per °C, referenced to 25 °C
constexpr float K_MIN       = 0.5f;      // valid K window for a K=1 probe
constexpr float K_MAX       = 1.5f;
constexpr float STD_LOW     = 1.413f;    // mS/cm @ 25 °C
constexpr float STD_HIGH    = 12.88f;    // mS/cm @ 25 °C
constexpr float RAW_MAX     = 20.0f;     // K=1 usable ceiling, mS/cm
constexpr float ADC_CLIP_MV = 3000.0f;   // ESP32 ADC goes non-linear above ~3.1 V

// ============================ Timing ===================================
constexpr uint32_t EC_SAMPLE_MS = 40;    // one ADC sample every 40 ms
constexpr uint8_t  EC_WINDOW    = 25;    // 25 x 40 ms = 1 s window
constexpr uint8_t  EC_TRIM      = 5;     // drop 5 lowest + 5 highest
constexpr uint32_t T_CONV_MS    = 750;   // DS18B20 12-bit conversion time
constexpr uint8_t  STAB_WINDOWS = 5;     // 5 s history for stability check
constexpr float    STAB_TOL     = 0.01f; // ±1 % spread (min 5 mV)
constexpr float    T_MISMATCH_C = 0.5f;  // TESTING.md SEN-01 tolerance

// ============================ Objects ==================================
OneWire           oneWire(PIN_ONEWIRE);
DallasTemperature ds(&oneWire);
Preferences       prefs;
WebServer         server(80);

// ======================== Temperature state ============================
DeviceAddress tAddr[2];
bool     tPresent[2] = {false, false};
bool     tValid[2]   = {false, false};
float    tC[2]       = {NAN, NAN};
uint32_t tReqAt      = 0;

// =========================== EC state ==================================
float    ecBuf[EC_WINDOW];
uint8_t  ecIdx        = 0;
uint32_t ecLastSample = 0;
float    mvAvg        = NAN;
float    mvHist[STAB_WINDOWS];
uint8_t  histN = 0, histIdx = 0;
bool     windowReady  = false;

// ======================== Calibration state ============================
float kLow = 1.0f, kHigh = 1.0f, kActive = 1.0f;
bool  calMode  = false;
float pendLow  = NAN;
float pendHigh = NAN;

// ===================== Latest snapshot (for web) =======================
struct Snapshot {
  float t1 = NAN, t2 = NAN, tc = NAN, mv = NAN, raw = NAN, ec25 = NAN, k = NAN;
  bool  stable = false;
  char  flags[96] = "";
  bool  ready = false;
} snap;

// ========================= WiFi state ==================================
bool wifiOn      = false;
bool staReported = false;

// ===================== Message log (Serial + web) ======================
constexpr uint8_t LOG_N   = 12;
constexpr size_t  LOG_LEN = 220;
char     logBuf[LOG_N][LOG_LEN];
uint8_t  logHead = 0, logCount = 0;

void say(const char *fmt, ...) {
  char b[LOG_LEN];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  Serial.println(b);
  strncpy(logBuf[logHead], b, LOG_LEN - 1);
  logBuf[logHead][LOG_LEN - 1] = '\0';
  logHead = (logHead + 1) % LOG_N;
  if (logCount < LOG_N) logCount++;
}

// ======================== Serial line buffer ===========================
char    cmdLine[24];
uint8_t cmdLen = 0;

// ============================ Helpers ==================================
float trimmedMean(const float *src, uint8_t n, uint8_t trim) {
  float a[EC_WINDOW];
  memcpy(a, src, n * sizeof(float));
  for (uint8_t i = 1; i < n; i++) {            // insertion sort (n = 25)
    float v = a[i];
    int8_t j = i - 1;
    while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
    a[j + 1] = v;
  }
  float s = 0;
  for (uint8_t i = trim; i < n - trim; i++) s += a[i];
  return s / (n - 2 * trim);
}

float rawEC(float mv) {                        // mS/cm at K = 1, uncompensated
  return 1000.0f * mv / RES2 / ECREF;
}

float ecAt25(float mv, float tempC) {          // calibrated, compensated to 25 °C
  float raw = rawEC(mv);
  float trial = raw * kActive;                 // auto-range with hysteresis
  if (trial > 2.5f)      kActive = kHigh;
  else if (trial < 2.0f) kActive = kLow;
  return raw * kActive / (1.0f + TEMP_COEF * (tempC - 25.0f));
}

// Probe 1 is the probe mounted beside the EC cell; probe 2 is the fallback.
bool compensationTemp(float &t) {
  if (tValid[0]) { t = tC[0]; return true; }
  if (tValid[1]) { t = tC[1]; return true; }
  t = 25.0f;                                   // no compensation; flagged
  return false;
}

bool isStable() {
  if (histN < STAB_WINDOWS) return false;
  float mn = mvHist[0], mx = mvHist[0], sum = 0;
  for (uint8_t i = 0; i < STAB_WINDOWS; i++) {
    mn = min(mn, mvHist[i]);
    mx = max(mx, mvHist[i]);
    sum += mvHist[i];
  }
  float mean = sum / STAB_WINDOWS;
  return (mx - mn) <= max(STAB_TOL * mean, 5.0f);
}

void printAddress(const DeviceAddress a) {
  for (uint8_t i = 0; i < 8; i++) {
    if (a[i] < 16) Serial.print('0');
    Serial.print(a[i], HEX);
  }
}

void fmtTemp(char *out, size_t n, uint8_t i) {
  if (tValid[i]) snprintf(out, n, "%.2f", tC[i]);
  else           snprintf(out, n, "FAULT");
}

// ======================== Non-blocking pollers =========================
void pollEC(uint32_t now) {
  if (now - ecLastSample < EC_SAMPLE_MS) return;
  ecLastSample = now;
  ecBuf[ecIdx++] = (float)analogReadMilliVolts(PIN_EC);
  if (ecIdx >= EC_WINDOW) {
    ecIdx = 0;
    mvAvg = trimmedMean(ecBuf, EC_WINDOW, EC_TRIM);
    mvHist[histIdx] = mvAvg;
    histIdx = (histIdx + 1) % STAB_WINDOWS;
    if (histN < STAB_WINDOWS) histN++;
    windowReady = true;
  }
}

void pollTemps(uint32_t now) {
  if (now - tReqAt < T_CONV_MS) return;        // conversion still running
  for (uint8_t i = 0; i < 2; i++) {
    if (!tPresent[i]) { tValid[i] = false; continue; }
    float v = ds.getTempC(tAddr[i]);
    // -127 = disconnected; 85.0 = power-on reset value (never a real pond reading)
    tValid[i] = (v != DEVICE_DISCONNECTED_C) && (v != 85.0f) && (v > -10.0f) && (v < 60.0f);
    tC[i] = v;
  }
  ds.requestTemperatures();                    // async: returns immediately
  tReqAt = now;
}

// =============================== WiFi ==================================
bool     apFallback = false;
uint32_t staStartMs = 0;

void startHotspot() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  say("[WIFI] Hotspot '%s' (password %s) is up. Join it, then open http://%s",
      AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
}

void startWiFi() {
  apFallback = false;
#if WIFI_USE_STA
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_NAME);
  WiFi.begin(STA_SSID, STA_PASS);              // non-blocking; pollWiFi() reports the IP
  staReported = false;
  staStartMs  = millis();
  say("[WIFI] Joining home WiFi '%s'... the address will be printed when connected.", STA_SSID);
#else
  startHotspot();
#endif
  wifiOn = true;
}

void stopWiFi() {
  MDNS.end();
  WiFi.mode(WIFI_OFF);
  wifiOn = false;
  staReported = false;
  say("[WIFI] Off. Web page unavailable; Serial Monitor still works.");
}

void pollWiFi() {
#if WIFI_USE_STA
  if (!wifiOn || apFallback) return;
  bool up = (WiFi.status() == WL_CONNECTED);
  if (up && !staReported) {
    if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
    say("[WIFI] Connected to '%s' (signal %d dBm). Open http://%s or http://%s.local",
        STA_SSID, WiFi.RSSI(), WiFi.localIP().toString().c_str(), MDNS_NAME);
    staReported = true;
  } else if (!up && staReported) {
    say("[WIFI] Home WiFi lost; reconnecting in the background.");
    staReported = false;
    staStartMs  = millis();
  } else if (!up && !staReported && millis() - staStartMs > STA_TIMEOUT_MS) {
    say("[WIFI] Could not join '%s' within %lu s (wrong name/password, out of range, or 5 GHz-only).",
        STA_SSID, (unsigned long)(STA_TIMEOUT_MS / 1000));
    WiFi.disconnect(true);
    apFallback = true;
    startHotspot();
  }
#endif
}

// ======================== Calibration commands =========================
void printHelp() {
  say("Commands: help | enter | cal | exit | abort | show | reset | wifion | wifioff");
  say("  enter : start calibration mode");
  say("  cal   : capture K for the buffer the probe is in (1.413 or 12.88 mS/cm)");
  say("  exit  : save captured K values to NVS and leave calibration mode");
  say("  abort : leave calibration mode without saving");
  say("  show  : print stored K values");
  say("  reset : restore K values to 1.000 (uncalibrated)");
  say("  wifion / wifioff : switch WiFi (Serial Monitor only)");
}

void cmdCal() {
  if (!calMode) { say("[CAL] Type 'enter' first."); return; }
  float t;
  if (!compensationTemp(t)) {
    say("[CAL] REJECTED: no valid DS18B20 reading. Fix the 1-Wire bus first.");
    return;
  }
  if (isnan(mvAvg) || !isStable()) {
    say("[CAL] REJECTED: reading not stable yet. Wait for STABLE, then retry.");
    return;
  }
  float raw = rawEC(mvAvg);
  // Temperature-aware buffer recognition: a buffer is accepted when the raw
  // reading is one that a probe with K inside K_MIN..K_MAX could produce at the
  // measured temperature. (DFRobot's fixed 0.9–1.9 / 9–16.8 windows assume
  // ~25 °C and reject valid readings in warm rooms.) The windows cannot overlap.
  float tf      = 1.0f + TEMP_COEF * (t - 25.0f);
  float lowLo   = STD_LOW  * tf / K_MAX, lowHi  = STD_LOW  * tf / K_MIN;
  float highLo  = STD_HIGH * tf / K_MAX, highHi = STD_HIGH * tf / K_MIN;
  float stdVal;
  bool  isLow;
  if (raw >= lowLo && raw <= lowHi)         { stdVal = STD_LOW;  isLow = true;  }
  else if (raw >= highLo && raw <= highHi)  { stdVal = STD_HIGH; isLow = false; }
  else {
    say("[CAL] REJECTED: raw %.3f mS/cm matches neither buffer at %.2f °C "
        "(1.413 window %.2f–%.2f, 12.88 window %.2f–%.2f). "
        "Check solution, probe immersion, wiring.",
        raw, t, lowLo, lowHi, highLo, highHi);
    return;
  }
  float compStd = stdVal * tf;
  float k = RES2 * ECREF * compStd / 1000.0f / mvAvg;
  if (k < K_MIN || k > K_MAX) {
    say("[CAL] REJECTED: K = %.4f outside %.1f–%.1f. Probe fouled, wrong buffer, "
        "or wrong sensor (K=10?).", k, K_MIN, K_MAX);
    return;
  }
  if (isLow) pendLow = k; else pendHigh = k;
  say("[CAL] %s buffer %.3f mS/cm @ %.2f °C -> V = %.1f mV, K = %.4f (pending; type 'exit' to save)",
      isLow ? "LOW" : "HIGH", stdVal, t, mvAvg, k);
}

void cmdExit() {
  if (!calMode) { say("[CAL] Not in calibration mode."); return; }
  if (!isnan(pendLow))  { kLow  = pendLow;  prefs.putFloat("kLow",  kLow);  }
  if (!isnan(pendHigh)) { kHigh = pendHigh; prefs.putFloat("kHigh", kHigh); }
  if (isnan(pendLow) || isnan(pendHigh)) {
    say("[CAL] WARNING: only one point captured. The other K keeps its previous value.");
  }
  kActive = kLow;
  calMode = false;
  pendLow = pendHigh = NAN;
  say("[CAL] Saved to NVS: kLow = %.4f, kHigh = %.4f", kLow, kHigh);
}

// fromWeb = true blocks wifion/wifioff so the page cannot cut its own link
void handleCommand(const char *c, bool fromWeb) {
  if      (!strcmp(c, "help"))  printHelp();
  else if (!strcmp(c, "enter")) {
    calMode = true; pendLow = pendHigh = NAN;
    say("[CAL] Calibration mode. Immerse EC probe + probe T1 in a buffer, wait for STABLE, then 'cal'.");
  }
  else if (!strcmp(c, "cal"))   cmdCal();
  else if (!strcmp(c, "exit"))  cmdExit();
  else if (!strcmp(c, "abort")) {
    calMode = false; pendLow = pendHigh = NAN;
    say("[CAL] Aborted. Nothing saved.");
  }
  else if (!strcmp(c, "show"))  say("[CAL] kLow = %.4f, kHigh = %.4f", kLow, kHigh);
  else if (!strcmp(c, "reset")) {
    kLow = kHigh = kActive = 1.0f;
    prefs.putFloat("kLow", kLow); prefs.putFloat("kHigh", kHigh);
    say("[CAL] K values reset to 1.0000 (uncalibrated).");
  }
  else if (!strcmp(c, "wifion") || !strcmp(c, "wifioff")) {
    if (fromWeb)               say("[WIFI] wifion/wifioff are Serial Monitor commands only.");
    else if (!strcmp(c, "wifion")) { if (!wifiOn) startWiFi(); else say("[WIFI] Already on."); }
    else                       { if (wifiOn)  stopWiFi();  else say("[WIFI] Already off."); }
  }
  else say("Unknown command '%s'. Type 'help'.", c);
}

void pollSerial() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (cmdLen) { cmdLine[cmdLen] = '\0'; handleCommand(cmdLine, false); cmdLen = 0; }
    } else if (cmdLen < sizeof(cmdLine) - 1) {
      cmdLine[cmdLen++] = (char)tolower(ch);
    }
  }
}

// ============================== Report =================================
void report() {
  float t;
  bool  tOK = compensationTemp(t);
  float raw = rawEC(mvAvg);
  float ec  = ecAt25(mvAvg, t);
  bool  stable = isStable();
  char  s1[10], s2[10];
  fmtTemp(s1, sizeof(s1), 0);
  fmtTemp(s2, sizeof(s2), 1);

  char flags[96] = "";
  if (!tOK) strlcat(flags, " [TEMP_FAULT]", sizeof(flags));
  if (tValid[0] && tValid[1] && fabsf(tC[0] - tC[1]) > T_MISMATCH_C) strlcat(flags, " [T_MISMATCH]", sizeof(flags));
  if (mvAvg > ADC_CLIP_MV) strlcat(flags, " [ADC_CLIP]", sizeof(flags));
  if (raw > RAW_MAX)       strlcat(flags, " [OVER_RANGE_K1]", sizeof(flags));
  if (calMode) strlcat(flags, stable ? " [CAL][STABLE]" : " [CAL][settling]", sizeof(flags));

  Serial.printf("T1=%s T2=%s Tcomp=%.2f | V=%.1f mV raw=%.3f EC25=%.3f mS/cm K=%.4f%s\n",
                s1, s2, t, mvAvg, raw, ec, kActive, flags);

  snap.t1 = tValid[0] ? tC[0] : NAN;
  snap.t2 = tValid[1] ? tC[1] : NAN;
  snap.tc = tOK ? t : NAN;
  snap.mv = mvAvg; snap.raw = raw; snap.ec25 = ec; snap.k = kActive;
  snap.stable = stable;
  strlcpy(snap.flags, flags, sizeof(snap.flags));
  snap.ready = true;
}

// ============================ Web server ===============================
const char PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SALIMANGO EC Calibration</title>
<style>
:root{--bg:#f4f6f5;--card:#fff;--ink:#1b2420;--mute:#5d6b65;--line:#d8dfdb;--acc:#0f6b57;--warn:#b25b00;--bad:#b3261e;--ok:#1e7d3a}
@media (prefers-color-scheme:dark){:root{--bg:#121715;--card:#1b2220;--ink:#e6ece9;--mute:#9aa8a1;--line:#2c3632;--acc:#4fbfa0;--warn:#f0a24a;--bad:#ff7a70;--ok:#5fd17d}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 system-ui,sans-serif}
main{max-width:640px;margin:0 auto;padding:16px}
h1{font-size:18px;margin:0 0 2px}.sub{color:var(--mute);font-size:13px;margin-bottom:14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin-bottom:12px}
.big{font-size:42px;font-weight:700;font-variant-numeric:tabular-nums}.unit{font-size:16px;color:var(--mute);font-weight:400}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
.lab{color:var(--mute);font-size:12px}.val{font-size:18px;font-variant-numeric:tabular-nums}
.badge{display:inline-block;padding:3px 8px;border-radius:999px;font-size:12px;font-weight:600;border:1px solid var(--line)}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}
.btns{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
button{font:inherit;padding:11px 6px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--ink);cursor:pointer}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}button.dang{color:var(--bad)}
pre{white-space:pre-wrap;font:12px/1.45 ui-monospace,monospace;margin:0;max-height:260px;overflow:auto}
#conn{float:right}
</style></head><body><main>
<h1>EC Calibration <span id="conn" class="badge">…</span></h1>
<div class="sub">S.A.L.I.M.A.N.G.O. bench tool · DFR0300 K=1 · 2× DS18B20</div>

<div class="card">
  <div class="lab">EC at 25 °C</div>
  <div class="big"><span id="ec">–</span> <span class="unit">mS/cm</span></div>
  <div style="margin-top:6px"><span id="mode" class="badge">–</span> <span id="flags" class="warn"></span></div>
</div>

<div class="card grid">
  <div><div class="lab">T1 (°C)</div><div class="val" id="t1">–</div></div>
  <div><div class="lab">T2 (°C)</div><div class="val" id="t2">–</div></div>
  <div><div class="lab">Tcomp (°C)</div><div class="val" id="tc">–</div></div>
  <div><div class="lab">Voltage (mV)</div><div class="val" id="mv">–</div></div>
  <div><div class="lab">Raw EC (mS/cm)</div><div class="val" id="raw">–</div></div>
  <div><div class="lab">K in use</div><div class="val" id="k">–</div></div>
  <div><div class="lab">Saved kLow</div><div class="val" id="kl">–</div></div>
  <div><div class="lab">Saved kHigh</div><div class="val" id="kh">–</div></div>
  <div><div class="lab">Pending (L / H)</div><div class="val" id="pend">–</div></div>
</div>

<div class="card btns">
  <button class="pri" onclick="cmd('enter')">Enter cal</button>
  <button class="pri" onclick="cmd('cal')">Capture</button>
  <button class="pri" onclick="cmd('exit')">Save &amp; exit</button>
  <button onclick="cmd('abort')">Abort</button>
  <button onclick="cmd('show')">Show K</button>
  <button class="dang" onclick="if(confirm('Reset both K values to 1.0000?'))cmd('reset')">Reset K</button>
</div>

<div class="card"><div class="lab" style="margin-bottom:6px">Messages</div><pre id="log"></pre></div>
</main>
<script>
const $=id=>document.getElementById(id);
const f=(v,d)=>v===null||v===undefined?'FAULT':Number(v).toFixed(d);
function render(j){
  $('conn').textContent='live';$('conn').className='badge ok';
  if(!j.ready){return}
  $('ec').textContent=f(j.ec25,3);
  $('t1').textContent=f(j.t1,2);$('t2').textContent=f(j.t2,2);$('tc').textContent=f(j.tc,2);
  $('mv').textContent=f(j.mv,1);$('raw').textContent=f(j.raw,3);$('k').textContent=f(j.k,4);
  $('kl').textContent=f(j.kLow,4);$('kh').textContent=f(j.kHigh,4);
  $('pend').textContent=(j.pendLow===null?'–':j.pendLow.toFixed(4))+' / '+(j.pendHigh===null?'–':j.pendHigh.toFixed(4));
  const m=$('mode');
  if(j.cal){m.textContent=j.stable?'CAL · STABLE':'CAL · settling';m.className='badge '+(j.stable?'ok':'warn')}
  else{m.textContent='Normal (not calibrating)';m.className='badge'}
  $('flags').textContent=j.flags.replace('[CAL][STABLE]','').replace('[CAL][settling]','');
  $('log').textContent=j.log.join('\n');
}
function poll(){fetch('/data').then(r=>r.json()).then(render)
  .catch(()=>{$('conn').textContent='offline';$('conn').className='badge bad'})}
function cmd(c){fetch('/cmd?c='+c).then(r=>r.json()).then(render).catch(()=>{})}
poll();setInterval(poll,1000);
</script></body></html>)rawliteral";

void appendNum(String &s, const char *key, float v, uint8_t dec) {
  s += '"'; s += key; s += "\":";
  if (isnan(v)) s += "null";
  else          s += String(v, (unsigned int)dec);
  s += ',';
}

void appendEscaped(String &s, const char *in) {
  s += '"';
  for (const char *p = in; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') { s += '\\'; s += c; }
    else if ((uint8_t)c < 0x20) s += ' ';
    else s += c;
  }
  s += '"';
}

void sendData() {
  String s;
  s.reserve(3200);
  s += '{';
  s += "\"ready\":"; s += snap.ready ? "true," : "false,";
  appendNum(s, "t1", snap.t1, 2);
  appendNum(s, "t2", snap.t2, 2);
  appendNum(s, "tc", snap.tc, 2);
  appendNum(s, "mv", snap.mv, 1);
  appendNum(s, "raw", snap.raw, 3);
  appendNum(s, "ec25", snap.ec25, 3);
  appendNum(s, "k", snap.k, 4);
  appendNum(s, "kLow", kLow, 4);
  appendNum(s, "kHigh", kHigh, 4);
  appendNum(s, "pendLow", pendLow, 4);
  appendNum(s, "pendHigh", pendHigh, 4);
  s += "\"cal\":";    s += calMode ? "true," : "false,";
  s += "\"stable\":"; s += snap.stable ? "true," : "false,";
  s += "\"flags\":";  appendEscaped(s, snap.flags); s += ',';
  s += "\"log\":[";
  uint8_t start = (logHead + LOG_N - logCount) % LOG_N;
  for (uint8_t i = 0; i < logCount; i++) {
    if (i) s += ',';
    appendEscaped(s, logBuf[(start + i) % LOG_N]);
  }
  s += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleCmdHttp() {
  String c = server.arg("c");
  c.toLowerCase();
  if (c.length() == 0 || c.length() > 16) { server.send(400, "text/plain", "bad command"); return; }
  handleCommand(c.c_str(), true);
  sendData();
}

// =========================== setup / loop ==============================
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  // Arduino-ESP32 defaults to 11 dB attenuation (~0–3.1 V usable), required
  // for the 0–3.4 V DFR0300 output. Do not lower the attenuation.
  pinMode(PIN_EC, INPUT);

  prefs.begin("ec_cal", false);                // same NVS namespace as the USB-only sketch
  kLow    = prefs.getFloat("kLow",  1.0f);
  kHigh   = prefs.getFloat("kHigh", 1.0f);
  kActive = kLow;

  ds.begin();
  uint8_t n = ds.getDeviceCount();
  Serial.printf("\nDS18B20 devices found on GPIO %u: %u\n", PIN_ONEWIRE, n);
  for (uint8_t i = 0; i < 2; i++) {
    tPresent[i] = ds.getAddress(tAddr[i], i);
    Serial.printf("  T%u: ", i + 1);
    if (tPresent[i]) { printAddress(tAddr[i]); ds.setResolution(tAddr[i], 12); Serial.println(); }
    else Serial.println("NOT FOUND - check wiring and 4.7 kOhm pull-up");
  }
  ds.setWaitForConversion(false);
  ds.requestTemperatures();
  tReqAt = millis();

  say("Stored K: kLow = %.4f, kHigh = %.4f", kLow, kHigh);

  server.on("/", handleRoot);
  server.on("/data", sendData);
  server.on("/cmd", handleCmdHttp);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  startWiFi();
  server.begin();

  printHelp();
}

void loop() {
  uint32_t now = millis();
  pollEC(now);
  pollTemps(now);
  pollSerial();
  pollWiFi();
  server.handleClient();                       // non-blocking
  if (windowReady) { windowReady = false; report(); }
}
