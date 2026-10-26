// Host unit tests for the firmware's pure math and control logic.
// Build and run from this folder (any C++17 compiler), as one command:
//   g++ -std=c++17 -I../../salimango_node test_firmware_logic.cpp
//       ../../salimango_node/sensor_math.cpp ../../salimango_node/control_logic.cpp -o tests
// then run ./tests (exit code 0 = all checks passed).
#include <math.h>
#include <stdio.h>

#include "control_logic.h"
#include "sensor_math.h"

static int failures = 0, checks = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    checks++;                                                          \
    if (!(cond)) {                                                     \
      failures++;                                                      \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
    }                                                                  \
  } while (0)
#define CHECK_NEAR(a, b, tol)                                                         \
  do {                                                                                \
    checks++;                                                                         \
    double _a = (a), _b = (b);                                                        \
    if (!(fabs(_a - _b) <= (tol))) {                                                  \
      failures++;                                                                     \
      printf("FAIL %s:%d  %s = %.6f, expected %.6f ± %g\n", __FILE__, __LINE__, #a, _a, _b, (double)(tol)); \
    }                                                                                 \
  } while (0)

static void testPh() {
  using namespace sm;
  // Uncalibrated defaults: 2.50 V = pH 7, 0.18 V per pH at 25 °C
  CHECK_NEAR(phFromVoltage(2.50f, 25, PH_DEFAULT_PH7_V, PH_DEFAULT_SLOPE, 25), 7.0, 1e-5);
  CHECK_NEAR(phFromVoltage(2.32f, 25, 2.5f, 0.18f, 25), 8.0, 1e-4);
  // Nernst: the slope grows with absolute temperature
  CHECK(phFromVoltage(2.32f, 35, 2.5f, 0.18f, 25) < 8.0f);

  CHECK_NEAR(phBufferAt(2, 25), 9.18, 1e-6);
  CHECK_NEAR(phBufferAt(2, 27.5f), 9.16, 1e-5);
  CHECK_NEAR(phBufferAt(1, 10), 6.90, 1e-6);  // clamps below the table

  CHECK(phRecognizeBuffer(6.5f) == 1);
  CHECK(phRecognizeBuffer(9.9f) == 2);
  CHECK(phRecognizeBuffer(5.5f) == -1);  // 1.49 from 4.01 and 1.36 from 6.86

  // Two-point fit recovers a known line (slope 0.17 V/pH, pH 7 at 2.45 V)
  PhCalPoint pts[3] = {};
  pts[1] = {true, 2.45f + 0.17f * (7.0f - 6.86f), 6.86f, 25};
  pts[2] = {true, 2.45f + 0.17f * (7.0f - 9.18f), 9.18f, 25};
  PhFit f = phFit(pts, 2.5f, 0.18f, 25);
  CHECK(f.ok && f.points == 2);
  CHECK_NEAR(f.slope, 0.17, 1e-4);
  CHECK_NEAR(f.ph7V, 2.45, 1e-4);

  // One point: offset only, slope kept
  PhCalPoint one[3] = {};
  one[1] = {true, 2.40f, 6.86f, 25};
  PhFit g = phFit(one, 2.5f, 0.18f, 25);
  CHECK(g.ok && g.points == 1);
  CHECK_NEAR(g.slope, 0.18, 1e-6);
  CHECK_NEAR(phFromVoltage(2.40f, 25, g.ph7V, g.slope, g.calT), 6.86, 1e-4);

  // A worn probe (slope 0.09 V/pH) is rejected
  PhCalPoint worn[3] = {};
  worn[0] = {true, 2.5f + 0.09f * (7 - 4.01f), 4.01f, 25};
  worn[1] = {true, 2.5f + 0.09f * (7 - 6.86f), 6.86f, 25};
  CHECK(!phFit(worn, 2.5f, 0.18f, 25).ok);
}

static void testEc() {
  using namespace sm;
  CHECK(EC_CELL_CONSTANT_K == 1.0f);
  // DFR0300 transfer function at K = 1: 1000 * V / 820 / 200
  CHECK_NEAR(ecRawMsCm(1640.0f), 10.0, 1e-4);
  CHECK_NEAR(ecTempFactor(30), 1.0925, 1e-6);

  // Recognition of the two standards at 25 °C and in a warm room
  CHECK(ecRecognizeStandard(1.41f, 25) == 0);
  CHECK(ecRecognizeStandard(12.9f, 25) == 1);
  CHECK(ecRecognizeStandard(1.413f * ecTempFactor(32), 32) == 0);
  CHECK(ecRecognizeStandard(5.0f, 25) == -1);

  // Correction factor: an ideal K = 1 cell reads the standard exactly -> 1.0
  float mvIdeal = 12.88f * 820.0f * 200.0f / 1000.0f;
  CHECK_NEAR(ecCorrectionFactor(mvIdeal, 12.88f, 25), 1.0, 1e-5);
  // A cell reading 5 % low needs a factor of 1/0.95; applying it once restores
  // the standard (the cell constant is not applied a second time).
  float k = ecCorrectionFactor(mvIdeal * 0.95f, 12.88f, 25);
  CHECK_NEAR(k, 1.0 / 0.95, 1e-4);
  CHECK_NEAR(ecRawMsCm(mvIdeal * 0.95f) * k, 12.88, 1e-3);
}

static void testDo() {
  using namespace sm;
  CHECK_NEAR(doSaturationMgL(25), 8.25, 1e-6);
  CHECK_NEAR(doSaturationMgL(25.5f), 8.18, 1e-4);
  DoCal def = {1, DO_DEFAULT_V1_MV, DO_DEFAULT_T1_C, 0, 0};
  // At the calibration voltage and temperature the probe reads saturation
  CHECK_NEAR(doMgL(1600, 25, def), 8.25, 1e-4);
  CHECK_NEAR(doMgL(800, 25, def), 4.125, 1e-4);
  // Single-point temperature correction: +35 mV/°C
  CHECK_NEAR(doSaturationVoltage(def, 30), 1775, 1e-3);
  DoCal two = {2, 1700, 30, 1500, 20};
  CHECK_NEAR(doSaturationVoltage(two, 25), 1600, 1e-3);
  DoCal broken = {1, 10, 25, 0, 0};
  CHECK(isnan(doMgL(500, 0, broken)));  // non-positive saturation voltage
}

static void testSalinity() {
  using namespace sm;
  // Definition point of PSS-78: C(35, 15 °C) = 42.914 mS/cm -> S = 35
  CHECK_NEAR(pss78Salinity(42.914f, 15), 35.0, 1e-3);
  // S = 35 at 25 °C: C = 42.914 * rt(25) = 53.065 mS/cm
  CHECK_NEAR(pss78Salinity(53.065f, 25), 35.0, 5e-3);
  // Monotonic in conductivity; the K = 1 ceiling (~18 mS/cm at 25 °C) is ~10.6 ppt
  float sCeil = pss78Salinity(18.0f, 25);
  CHECK(sCeil > 10.0f && sCeil < 11.5f);
  CHECK(pss78Salinity(10.0f, 25) < sCeil);
}

static void testEmerson() {
  using namespace sm;
  CHECK_NEAR(emersonPKa(25), 9.2448, 1e-3);
  // Emerson et al. (1975) Table 2: 25 °C, pH 8.0 -> 5.38 % NH3
  CHECK_NEAR(emersonNh3Fraction(8.0f, 25) * 100, 5.38, 0.01);
  // 20 °C, pH 7.0 -> 0.40 %
  CHECK_NEAR(emersonNh3Fraction(7.0f, 20) * 100, 0.40, 0.01);
  // more toxic when warmer and more alkaline
  CHECK(emersonNh3Fraction(8.5f, 30) > emersonNh3Fraction(8.0f, 30));
  CHECK(emersonNh3Fraction(8.0f, 30) > emersonNh3Fraction(8.0f, 25));
}

static void testSelectorAndMode() {
  CHECK(decodeSelector(true, false) == Selector::MANUAL);
  CHECK(decodeSelector(false, true) == Selector::AUTO);
  CHECK(decodeSelector(false, false) == Selector::OFF);
  CHECK(decodeSelector(true, true) == Selector::FAULT);

  ModeManager mm(300000);
  const char *why = nullptr;
  ModeStatus st = mm.resolve(Selector::AUTO, 0);
  CHECK(st.mode == Mode::AUTO);
  // Selector at OFF overrides the dashboard
  CHECK(!mm.requestRemote(Mode::MANUAL, Selector::OFF, 0, &why));
  CHECK(mm.resolve(Selector::OFF, 0).mode == Mode::OFF);
  // Remote MANUAL with the selector at AUTO, then the 5-minute revert
  CHECK(mm.requestRemote(Mode::MANUAL, Selector::AUTO, 1000, &why));
  st = mm.resolve(Selector::AUTO, 1000);
  CHECK(st.mode == Mode::MANUAL && !st.fromSelector && st.manualExpiresInS == 300);
  mm.noteManualActivity(200000);
  CHECK(mm.resolve(Selector::AUTO, 400000).mode == Mode::MANUAL);
  CHECK(mm.resolve(Selector::AUTO, 500000).mode == Mode::AUTO);
  CHECK(mm.resolve(Selector::AUTO, 500100).mode == Mode::AUTO);  // stays reverted
  // Selector at M also reverts, and can be re-entered from the dashboard
  CHECK(mm.resolve(Selector::MANUAL, 600000).mode == Mode::MANUAL);
  CHECK(mm.resolve(Selector::MANUAL, 900000).mode == Mode::AUTO);
  CHECK(!mm.requestRemote(Mode::OFF, Selector::MANUAL, 900000, &why));
  CHECK(mm.requestRemote(Mode::MANUAL, Selector::MANUAL, 900000, &why));
  CHECK(mm.resolve(Selector::MANUAL, 900001).mode == Mode::MANUAL);
}

static void testAeration() {
  AerationController a;
  a.begin(0);
  CHECK(a.autoDemand());  // ON at boot
  // Good DO: turns off only after 3 readings AND 60 s minimum on time
  a.update(6, false, 3000); a.update(6, false, 6000); a.update(6, false, 9000);
  CHECK(a.autoDemand());
  a.update(6, false, 60000);
  CHECK(!a.autoDemand());
  // Missing readings count as low for AUTO, but are not an emergency
  a.update(NAN, false, 63000); a.update(NAN, false, 66000); a.update(NAN, false, 72000);
  CHECK(a.autoDemand());
  CHECK(!a.emergency());
  // Valid low readings raise the emergency
  a.update(2.5f, false, 75000); a.update(2.5f, false, 78000);
  CHECK(!a.emergency());
  a.update(2.5f, false, 81000);
  CHECK(a.emergency());
  // Held during DO calibration
  a.update(8, true, 84000); a.update(8, true, 87000); a.update(8, true, 90000);
  CHECK(a.emergency() && a.autoDemand());
  a.update(8, false, 93000); a.update(8, false, 96000); a.update(8, false, 99000);
  CHECK(!a.emergency());
}

static void testBilge() {
  BilgeController b;
  BilgeConfig c;  // salMin 28, hyst 2, 15 min run, 30 min off, 3 fails
  b.configure(c);
  const uint32_t MIN = 60000;
  // Needs 3 consecutive low readings
  CHECK(b.update(SalinityInput::VALID, 27, true, 0) == BilgeEvent::NONE);
  CHECK(b.update(SalinityInput::VALID, 29, true, 3000) == BilgeEvent::NONE);
  b.update(SalinityInput::VALID, 27, true, 6000);
  b.update(SalinityInput::VALID, 27, true, 9000);
  CHECK(b.update(SalinityInput::VALID, 27, true, 12000) == BilgeEvent::STARTED);
  CHECK(b.pumpsOn());
  // 29 is not yet recovered (needs ≥ 30)
  CHECK(b.update(SalinityInput::VALID, 29, true, 15000) == BilgeEvent::NONE);
  CHECK(b.update(SalinityInput::VALID, 30, true, 18000) == BilgeEvent::RECOVERED);
  CHECK(b.state() == BilgeState::RESTING);
  // Rest period
  b.update(SalinityInput::VALID, 20, true, 18000 + 29 * MIN);
  CHECK(b.state() == BilgeState::RESTING);
  b.update(SalinityInput::VALID, 20, true, 18000 + 30 * MIN);
  CHECK(b.state() == BilgeState::IDLE);

  // Three timeouts in a row -> lockout
  uint32_t t = 100 * MIN;
  for (int run = 0; run < 3; run++) {
    for (int i = 0; i < 3; i++) b.update(SalinityInput::VALID, 20, true, t += 3000);
    CHECK(b.pumpsOn());
    BilgeEvent e = b.update(SalinityInput::VALID, 20, true, t += 15 * MIN);
    CHECK(e == (run < 2 ? BilgeEvent::TIMED_OUT : BilgeEvent::LOCKED_OUT));
    if (run < 2) b.update(SalinityInput::VALID, 20, true, t += 30 * MIN);
  }
  CHECK(b.state() == BilgeState::LOCKOUT && b.failCount() == 3);
  CHECK(b.clearLockout());
  CHECK(b.state() == BilgeState::IDLE);

  // Over the probe's range: not low, never starts
  BilgeController o;
  o.configure(c);
  for (int i = 0; i < 5; i++) o.update(SalinityInput::OVER_RANGE, NAN, true, i * 3000);
  CHECK(o.state() == BilgeState::IDLE);
  // Below the PSS-78 range: low, starts
  for (int i = 0; i < 3; i++) o.update(SalinityInput::UNDER_RANGE, NAN, true, 20000 + i * 3000);
  CHECK(o.pumpsOn());
  // Over range while running counts as recovered
  CHECK(o.update(SalinityInput::OVER_RANGE, NAN, true, 40000) == BilgeEvent::RECOVERED);
  // Sensor fault stops a running cycle
  BilgeController f;
  f.configure(c);
  for (int i = 0; i < 3; i++) f.update(SalinityInput::VALID, 10, true, i * 3000);
  CHECK(f.pumpsOn());
  CHECK(f.update(SalinityInput::FAULT, NAN, true, 10000) == BilgeEvent::STOPPED_FAULT);
  CHECK(!f.pumpsOn());
  // Leaving AUTO stops the pumps
  for (int i = 0; i < 3; i++) f.update(SalinityInput::VALID, 10, true, 20000 + i * 3000);
  f.update(SalinityInput::VALID, 10, false, 30000);
  CHECK(!f.pumpsOn());
}

static void testRelaySequencer() {
  RelaySequencer r(5000, 2000);
  r.setDesired(RELAY_K1, true);
  r.setDesired(RELAY_K5, true);
  r.setDesired(RELAY_K3, true);
  r.setDesired(RELAY_K4, true);
  r.update(100);
  CHECK(r.output(RELAY_K1) && !r.output(RELAY_K5));
  CHECK(r.output(RELAY_K3) && !r.output(RELAY_K4));
  r.update(2100);
  CHECK(r.output(RELAY_K4) && !r.output(RELAY_K5));  // bilge stagger 2 s
  r.update(5099);
  CHECK(!r.output(RELAY_K5));
  r.update(5100);
  CHECK(r.output(RELAY_K5));  // air stagger 5 s
  // Off together, immediately
  r.setDesired(RELAY_K1, false);
  r.setDesired(RELAY_K5, false);
  r.update(5200);
  CHECK(!r.output(RELAY_K1) && !r.output(RELAY_K5));
  // Restarting waits for the stagger from the previous start
  r.setDesired(RELAY_K1, true);
  r.setDesired(RELAY_K5, true);
  r.update(5300);
  CHECK(!r.output(RELAY_K1));
  r.update(10100);
  CHECK(r.output(RELAY_K1) && !r.output(RELAY_K5));
}

int main() {
  testPh();
  testEc();
  testDo();
  testSalinity();
  testEmerson();
  testSelectorAndMode();
  testAeration();
  testBilge();
  testRelaySequencer();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
