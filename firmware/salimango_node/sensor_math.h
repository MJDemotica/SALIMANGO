// Pure sensor and water-chemistry math (no Arduino dependencies, so it is
// unit-tested on the host: firmware/tests/host).
//
// Every formula here is the one already used by the project's bench sketches,
// moved into one place:
//   pH  : ph sense/ph_cali_no_ten_k2           (Nernst-scaled slope, 3 buffers)
//   EC  : salinity sense/salimango_ec_calibration_wifi (DFRobot DFR0300, K = 1)
//   DO  : DOSENSE/DO_ESP32_DS18B20_WiFi4        (DFRobot SEN0237-A)
// plus the two derived values the dashboard shows:
//   salinity : UNESCO PSS-78 (practical salinity from conductivity + temperature)
//   NH3      : Emerson et al. (1975) unionized-ammonia fraction from pH + temperature
#pragma once

#include <stdint.h>

namespace sm {

// ---------------------------------------------------------------- pH --------
constexpr float PH_DEFAULT_PH7_V  = 2.50f;   // module output (V) at pH 7, uncalibrated
constexpr float PH_DEFAULT_SLOPE  = 0.18f;   // V per pH unit, uncalibrated
constexpr float PH_DEFAULT_CAL_T  = 25.0f;   // °C
constexpr float PH_SLOPE_MIN      = 0.12f;   // calibrations outside this are rejected
constexpr float PH_SLOPE_MAX      = 0.24f;

// Buffers: 4.01 phthalate, 6.86 phosphate, 9.18 borax (values at 25 °C).
constexpr int PH_BUFFER_COUNT = 3;
extern const float PH_BUFFER_NOMINAL[PH_BUFFER_COUNT];

// pH from module voltage; slope is scaled with absolute temperature (Nernst)
// relative to the calibration temperature.
float phFromVoltage(float moduleV, float tempC, float ph7V, float slope, float calT);

// Buffer pH at temperature (linear interpolation of the 15–40 °C table).
float phBufferAt(int buffer, float tempC);

// Nearest buffer within ±1.0 pH of the estimate, or -1.
int phRecognizeBuffer(float estimatedPh);

struct PhCalPoint {
  bool  captured;
  float moduleV;   // V
  float bufferPh;  // buffer pH at the capture temperature
  float tempC;
};

struct PhFit {
  bool  ok;
  int   points;
  float ph7V, slope, calT;
};

// 1 point: offset only (slope kept). 2–3 points: least-squares line.
PhFit phFit(const PhCalPoint pts[PH_BUFFER_COUNT], float ph7V, float slope, float calT);

// ---------------------------------------------------------------- EC --------
// DFRobot Gravity Analog EC (DFR0300). The project's probe has cell constant
// K = 1 (team-confirmed). The DFR0300 transfer function below is the one for a
// K = 1 cell; K appears in exactly one place (ecRawMsCm) and is never applied
// a second time. The calibration result is a separate software correction
// factor (DFRobot's "kvalue"), nominally 1.0.
constexpr float EC_CELL_CONSTANT_K = 1.0f;
static_assert(EC_CELL_CONSTANT_K == 1.0f,
              "The DFR0300 transfer function is defined for a K = 1 cell; a different "
              "probe needs DFRobot's matching board and conversion, not just a new constant.");
constexpr float EC_RES2        = 820.0f;
constexpr float EC_ECREF       = 200.0f;
constexpr float EC_TEMP_COEF   = 0.0185f;  // per °C, linear compensation to 25 °C
constexpr float EC_STD_LOW     = 1.413f;   // mS/cm at 25 °C
constexpr float EC_STD_HIGH    = 12.88f;   // mS/cm at 25 °C
constexpr float EC_CORR_MIN    = 0.5f;     // acceptable correction factor window
constexpr float EC_CORR_MAX    = 1.5f;
constexpr float EC_RAW_MAX     = 20.0f;    // K = 1 usable ceiling, mS/cm
constexpr float EC_ADC_CLIP_MV = 3000.0f;  // ESP32 ADC is non-linear above ~3.1 V

// Conductivity (mS/cm) at the measurement temperature, before correction.
float ecRawMsCm(float mv);
// 1 + 0.0185 (T − 25)
float ecTempFactor(float tempC);

// Which standard the probe is in, judged from the raw reading at the measured
// temperature (windows allow correction factors EC_CORR_MIN..EC_CORR_MAX).
// Returns 0 = 1.413 mS/cm, 1 = 12.88 mS/cm, -1 = neither.
int ecRecognizeStandard(float rawMsCm, float tempC);
// Correction factor that makes this voltage read the standard at tempC.
float ecCorrectionFactor(float mv, float standardMsCm25, float tempC);

// ---------------------------------------------------------------- DO --------
constexpr float DO_DEFAULT_V1_MV     = 1600.0f;  // DFRobot default (uncalibrated)
constexpr float DO_DEFAULT_T1_C      = 25.0f;
constexpr float DO_SAT_TEMP_COEF_MV  = 35.0f;    // mV/°C, single-point correction
constexpr float DO_MIN_TWO_POINT_C   = 5.0f;
constexpr float DO_MIN_CAL_MV        = 100.0f;
constexpr float DO_MAX_CAL_MV        = 3100.0f;

struct DoCal {
  uint8_t points;   // 1 or 2
  float v1, t1;     // point 1 (higher temperature in two-point mode)
  float v2, t2;
};

// Saturated DO (mg/L) at temperature, DFRobot 0–40 °C table.
float doSaturationMgL(float tempC);
// Probe voltage expected in saturated water at tempC.
float doSaturationVoltage(const DoCal &cal, float tempC);
// DO (mg/L); NaN if the calibration gives a non-positive saturation voltage.
float doMgL(float mv, float tempC, const DoCal &cal);

// ---------------------------------------------------------- salinity --------
// UNESCO (1981) Practical Salinity Scale 1978, surface pressure.
// condMsCm: conductivity at tempC (NOT compensated to 25 °C).
// Formula domain: 2 ≤ S ≤ 42, −2 ≤ T ≤ 35 °C.
constexpr float PSS78_C35_15_MS_CM = 42.914f;   // C(35, 15 °C, 0)
float pss78Salinity(float condMsCm, float tempC);

// ---------------------------------------------------------- ammonia ---------
// Emerson, Russo, Lund & Thurston (1975), J. Fish. Res. Board Can. 32:2379–2383:
//   pKa = 0.09018 + 2729.92 / T(K),  T(K) = °C + 273.2
//   fraction NH3 = 1 / (10^(pKa − pH) + 1)
float emersonPKa(float tempC);
float emersonNh3Fraction(float ph, float tempC);

}  // namespace sm
