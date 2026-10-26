### Recommended rule


| Element                | Recommendation                                                                                                                                             | Reason                                                                                                                                          |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| **Turn ON**            | Filtered salinity &lt; `salMin` for 3 consecutive readings                                                                                                 | Ignores single-sample spikes from turbulence or bubbles at the EC probe                                                                         |
| **Turn OFF (normal)**  | Filtered salinity ≥ `salMin + salHyst`                                                                                                                     | The hysteresis band stops the relay from switching rapidly near the threshold                                                                   |
| `salHyst`              | 2 ppt starting value; must be larger than the EC noise you measure during calibration                                                                      | If the sensor noise is ±1.5 ppt, a 1 ppt band will still chatter                                                                                |
| **Turn OFF (timeout)** | Maximum run time reached (`bilgeMaxRunMin`, start at 15 min)                                                                                               | Water that won’t recover must not keep the pump running forever                                                                                 |
| **Rest period**        | Minimum off time before restarting (`bilgeMinOffMin`, start at 30 min)                                                                                     | Protects the pump and relay, and gives the water time to mix before re-checking                                                                 |
| **Lockout**            | After 3 timeouts with no recovery, stop retrying, raise a “salinity not recovering” alarm, and wait for the operator or for salinity to recover on its own | Turns a failed correction into information for the farmer instead of wasted energy                                                              |
| **Inrush**             | Start pump 2 about 2 s after pump 1                                                                                                                        | Avoids both motors drawing startup current from the 30 A PSU at the same moment                                                                 |
| **DO emergency**       | Evaluate the bilge logic independently of the DO branch                                                                                                    | The current flowchart skips the salinity check whenever DO is below 3 ppm. The two pumps use separate relays and there’s enough power for both. |


All of these numbers are engineering starting values, not literature-backed figures. Present them as parameters tuned during bench testing and record the final values in your test log.

### Firmware sketch (non-blocking, `millis()`-based)

cpp

```cpp
// Relay module is low-level trigger: LOW = energized
const uint8_t BILGE1 = 33, BILGE2 = 13;
const uint32_t MIN_MS = 60000UL;

float salMin = 28.0, salHyst = 2.0;          // from devices/<id>/config
uint32_t maxRunMs = 15 * MIN_MS, minOffMs = 30 * MIN_MS;
const uint8_t CONFIRM_N = 3, MAX_FAILS = 3;
const uint32_t STAGGER_MS = 2000;

enum BilgeState { IDLE, RUNNING, RESTING, LOCKOUT };
BilgeState bState = IDLE;
uint32_t bT0 = 0;
uint8_t lowCount = 0, failCount = 0;
bool pump2On = false;

void setBilge(bool on) {
  digitalWrite(BILGE1, on ? LOW : HIGH);
  if (!on) { digitalWrite(BILGE2, HIGH); pump2On = false; }
}

// Call once per sensor cycle with the filtered (median) salinity, AUTO mode only
void bilgeUpdate(float sal, bool sensorFault) {
  uint32_t now = millis();
  if (sensorFault) { setBilge(false); bState = IDLE; lowCount = 0; return; }

  switch (bState) {
    case IDLE:
      lowCount = (sal < salMin) ? lowCount + 1 : 0;
      if (lowCount >= CONFIRM_N) { setBilge(true); bT0 = now; bState = RUNNING; }
      break;

    case RUNNING:
      if (!pump2On && now - bT0 >= STAGGER_MS) { digitalWrite(BILGE2, LOW); pump2On = true; }
      if (sal >= salMin + salHyst) {                 // recovered
        setBilge(false); failCount = 0; lowCount = 0; bT0 = now; bState = RESTING;
      } else if (now - bT0 >= maxRunMs) {            // timed out
        setBilge(false); lowCount = 0; bT0 = now;
        bState = (++failCount >= MAX_FAILS) ? LOCKOUT : RESTING;
        // if LOCKOUT: set alarm flag in next uplink
      }
      break;

    case RESTING:
      if (now - bT0 >= minOffMs) bState = IDLE;
      break;

    case LOCKOUT:
      if (sal >= salMin + salHyst) { failCount = 0; bState = IDLE; }  // or operator ack via downlink
      break;
  }
}
```

The relay pins must be driven HIGH before `pinMode()` at boot, as [WIRING.md](http://WIRING.md) §4 already requires. The rollover-safe `now - bT0` subtraction keeps the timers correct when `millis()` wraps around after about 49 days.

### Add to [APP.md](http://APP.md) §6 (threshold table)


| Parameter        | Default | Allowed range | Unit  |
| ---------------- | ------- | ------------- | ----- |
| `salHyst`        | 2.0     | 1.0–5.0       | ppt   |
| `bilgeMaxRunMin` | 15      | 5–60          | min   |
| `bilgeMinOffMin` | 30      | 10–120        | min   |
| `bilgeMaxFails`  | 3       | 1–10          | count |


### Prerequisite

None of this works until the K = 1 sensor-range question from my last reply is settled. If the DFR0300 saturates below 28 ppt, the reading never reaches `salMin + salHyst`, every run ends by timeout, and the system locks out after three tries. That outcome is safe, but it does nothing useful. Resolve the probe range or the threshold band first.

---

**Validation Check**

- **Boundary Check:** Pass. Based only on the project files ([FLOWCHART.md](http://FLOWCHART.md), [APP.md](http://APP.md) §6, [WIRING.md](http://WIRING.md) §4–5, [Components.md](http://Components.md)) and this conversation.
- **Logic Check:** Pass. Non-blocking `millis()` timing with rollover-safe arithmetic, low-level trigger polarity matching the relay module, hysteresis plus timeout plus lockout so the relay cannot chatter or run without limit, and a staggered start to limit inrush current. The numbers are untuned defaults.
- **Reference Check:** N/A. No citations were generated. The parameter values need justification from your bench tests, not from literature.
- **Integration Check:** Replaces the “Salinity below threshold → energize bilge pump” node in [FLOWCHART.md](http://FLOWCHART.md). The new thresholds go into `devices/<id>/config`, and the lockout flag goes into the uplink frame and the [APP.md](http://APP.md) §7 alert table. Add test cases to [TESTING.md](http://TESTING.md) for recovery, timeout, and lockout.