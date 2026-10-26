/*
  S.A.L.I.M.A.N.G.O. - Step 6 Mode Selector Test (ESP32 #1, float node)
  ----------------------------------------------------------------------
  INScom LW28-20A MOA rotary switch (M - O - A).
  Beep test result:  1-2 closed ONLY in M,  3-4 closed ONLY in A,  all open in O.

  Wiring:
    Switch terminal 2  -> GPIO 4   (+ 10 kOhm pull-up to 3V3)
    Switch terminal 4  -> GPIO 21  (+ 10 kOhm pull-up to 3V3)
    Switch terminals 1 and 3 -> ESP32 GND
    Pull-ups go to 3V3, NOT 5V.

  Decode:
    Position  GPIO4  GPIO21  Mode
    M         LOW    HIGH    MANUAL
    O         HIGH   HIGH    OFF
    A         HIGH   LOW     AUTO
    -         LOW    LOW     FAULT (both contacts closed - should never happen)

  The relays (K1-K4 on GPIO 25/27/33/13) are only held OFF here.
  This sketch does NOT switch any pump.

  Serial Monitor: 115200 baud. Prints the mode whenever it changes,
  and the raw pin levels every 2 s.
*/

// ---------- Relays: hold OFF (active-LOW) ----------
const uint8_t RELAY_PINS[] = {25, 27, 33, 13};

// ---------- Selector ----------
const uint8_t PIN_SEL_M = 4;    // terminal 2 (closed only in M)
const uint8_t PIN_SEL_A = 21;   // terminal 4 (closed only in A)
const unsigned long DEBOUNCE_MS = 50;
const unsigned long REPORT_MS   = 2000;

enum Mode { MODE_OFF, MODE_MANUAL, MODE_AUTO, MODE_FAULT };

Mode currentMode = MODE_OFF;
Mode lastRaw     = MODE_OFF;
unsigned long lastChange = 0;
unsigned long lastReport = 0;

const char* modeName(Mode m) {
  switch (m) {
    case MODE_MANUAL: return "MANUAL (M)";
    case MODE_AUTO:   return "AUTO (A)";
    case MODE_OFF:    return "OFF (O)";
    default:          return "FAULT (both LOW)";
  }
}

Mode readRaw() {
  bool m = (digitalRead(PIN_SEL_M) == LOW);
  bool a = (digitalRead(PIN_SEL_A) == LOW);
  if (m && !a)  return MODE_MANUAL;
  if (a && !m)  return MODE_AUTO;
  if (!m && !a) return MODE_OFF;
  return MODE_FAULT;
}

// Non-blocking debounce: accept a new mode only after it stays
// the same for DEBOUNCE_MS.
void updateSelector() {
  Mode raw = readRaw();
  unsigned long now = millis();
  if (raw != lastRaw) {
    lastRaw = raw;
    lastChange = now;
  } else if (raw != currentMode && now - lastChange >= DEBOUNCE_MS) {
    currentMode = raw;
    Serial.printf(">> Mode changed: %s\n", modeName(currentMode));
  }
}

void setup() {
  for (uint8_t p : RELAY_PINS) {          // keep all pumps OFF
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);
  }

  pinMode(PIN_SEL_M, INPUT);              // external 10k pull-up
  pinMode(PIN_SEL_A, INPUT);              // external 10k pull-up

  Serial.begin(115200);
  Serial.println();
  Serial.println(F("=== Mode Selector Test (GPIO 4 = M, GPIO 21 = A) ==="));

  currentMode = readRaw();
  lastRaw = currentMode;
  lastChange = millis();
  Serial.printf("Start mode: %s\n", modeName(currentMode));
}

void loop() {
  updateSelector();

  unsigned long now = millis();
  if (now - lastReport >= REPORT_MS) {
    lastReport = now;
    Serial.printf("GPIO4=%s  GPIO21=%s  ->  %s\n",
                  digitalRead(PIN_SEL_M) ? "HIGH" : "LOW",
                  digitalRead(PIN_SEL_A) ? "HIGH" : "LOW",
                  modeName(currentMode));
  }
}
