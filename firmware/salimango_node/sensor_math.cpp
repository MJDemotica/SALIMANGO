#include "sensor_math.h"

#include <math.h>

namespace sm {

// ---------------------------------------------------------------- pH --------
const float PH_BUFFER_NOMINAL[PH_BUFFER_COUNT] = {4.01f, 6.86f, 9.18f};

static const float BUF_T[6] = {15, 20, 25, 30, 35, 40};
static const float BUF_TABLE[PH_BUFFER_COUNT][6] = {
    {4.00f, 4.00f, 4.01f, 4.01f, 4.02f, 4.03f},  // 4.01 phthalate
    {6.90f, 6.88f, 6.86f, 6.85f, 6.84f, 6.84f},  // 6.86 phosphate
    {9.28f, 9.23f, 9.18f, 9.14f, 9.10f, 9.07f},  // 9.18 borax
};

float phFromVoltage(float moduleV, float tempC, float ph7V, float slope, float calT) {
  float s = slope * (tempC + 273.15f) / (calT + 273.15f);
  return 7.0f + (ph7V - moduleV) / s;
}

float phBufferAt(int b, float t) {
  if (t <= BUF_T[0]) return BUF_TABLE[b][0];
  if (t >= BUF_T[5]) return BUF_TABLE[b][5];
  for (int i = 0; i < 5; i++) {
    if (t <= BUF_T[i + 1]) {
      float f = (t - BUF_T[i]) / (BUF_T[i + 1] - BUF_T[i]);
      return BUF_TABLE[b][i] + f * (BUF_TABLE[b][i + 1] - BUF_TABLE[b][i]);
    }
  }
  return PH_BUFFER_NOMINAL[b];
}

int phRecognizeBuffer(float est) {
  int b = -1;
  float best = 1.0f;
  for (int i = 0; i < PH_BUFFER_COUNT; i++) {
    float d = fabsf(est - PH_BUFFER_NOMINAL[i]);
    if (d < best) { best = d; b = i; }
  }
  return b;
}

PhFit phFit(const PhCalPoint pts[PH_BUFFER_COUNT], float ph7V, float slope, float calT) {
  PhFit r = {false, 0, ph7V, slope, calT};
  for (int i = 0; i < PH_BUFFER_COUNT; i++) r.points += pts[i].captured ? 1 : 0;
  if (r.points == 0) return r;

  if (r.points == 1) {
    // Offset only: keep the existing slope
    int b = pts[0].captured ? 0 : (pts[1].captured ? 1 : 2);
    float s = slope * (pts[b].tempC + 273.15f) / (calT + 273.15f);
    r.ph7V = pts[b].moduleV + s * (pts[b].bufferPh - 7.0f);
  } else {
    // Least-squares line V = a + k*pH through the captured points
    float sx = 0, sy = 0, sxx = 0, sxy = 0, st = 0;
    for (int i = 0; i < PH_BUFFER_COUNT; i++) {
      if (!pts[i].captured) continue;
      sx += pts[i].bufferPh;
      sy += pts[i].moduleV;
      sxx += pts[i].bufferPh * pts[i].bufferPh;
      sxy += pts[i].bufferPh * pts[i].moduleV;
      st += pts[i].tempC;
    }
    int n = r.points;
    float k = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    float a = (sy - k * sx) / n;
    r.slope = -k;
    r.ph7V = a + k * 7.0f;
    r.calT = st / n;
  }
  r.ok = r.slope >= PH_SLOPE_MIN && r.slope <= PH_SLOPE_MAX;
  return r;
}

// ---------------------------------------------------------------- EC --------
float ecRawMsCm(float mv) { return EC_CELL_CONSTANT_K * 1000.0f * mv / (EC_RES2 * EC_ECREF); }

float ecTempFactor(float tempC) { return 1.0f + EC_TEMP_COEF * (tempC - 25.0f); }

int ecRecognizeStandard(float raw, float tempC) {
  // Temperature-aware windows: a standard is accepted when the raw reading is one
  // a probe with a correction factor inside EC_CORR_MIN..EC_CORR_MAX could
  // produce at the measured temperature. The two windows cannot overlap.
  float tf = ecTempFactor(tempC);
  if (raw >= EC_STD_LOW * tf / EC_CORR_MAX && raw <= EC_STD_LOW * tf / EC_CORR_MIN) return 0;
  if (raw >= EC_STD_HIGH * tf / EC_CORR_MAX && raw <= EC_STD_HIGH * tf / EC_CORR_MIN) return 1;
  return -1;
}

float ecCorrectionFactor(float mv, float standardMsCm25, float tempC) {
  return standardMsCm25 * ecTempFactor(tempC) / ecRawMsCm(mv);
}

// ---------------------------------------------------------------- DO --------
// Saturated DO in water, 0..40 °C, in µg/L (DFRobot table)
static const uint16_t DO_TABLE[41] = {
    14460, 14220, 13820, 13440, 13090, 12740, 12420, 12110, 11810, 11530,
    11260, 11010, 10770, 10530, 10300, 10080, 9860,  9660,  9460,  9270,
    9080,  8900,  8730,  8570,  8410,  8250,  8110,  7960,  7820,  7690,
    7560,  7430,  7300,  7180,  7070,  6950,  6840,  6730,  6630,  6530,
    6410};

float doSaturationMgL(float t) {
  float ugL;
  if (t <= 0.0f) {
    ugL = DO_TABLE[0];
  } else if (t >= 40.0f) {
    ugL = DO_TABLE[40];
  } else {
    int i = (int)t;
    float frac = t - i;
    ugL = DO_TABLE[i] + (DO_TABLE[i + 1] - (float)DO_TABLE[i]) * frac;
  }
  return ugL / 1000.0f;
}

float doSaturationVoltage(const DoCal &cal, float t) {
  if (cal.points == 2) return (t - cal.t2) * (cal.v1 - cal.v2) / (cal.t1 - cal.t2) + cal.v2;
  return cal.v1 + DO_SAT_TEMP_COEF_MV * (t - cal.t1);
}

float doMgL(float mv, float t, const DoCal &cal) {
  float vSat = doSaturationVoltage(cal, t);
  if (vSat <= 0.0f) return NAN;
  return mv * doSaturationMgL(t) / vSat;
}

// ---------------------------------------------------------- salinity --------
float pss78Salinity(float condMsCm, float t) {
  const double R = condMsCm / PSS78_C35_15_MS_CM;
  const double rt = 0.6766097 + t * (2.00564e-2 + t * (1.104259e-4 + t * (-6.9698e-7 + t * 1.0031e-9)));
  const double s = sqrt(R / rt);  // Rt^(1/2); Rp = 1 at the surface
  const double a = 0.0080 + s * (-0.1692 + s * (25.3851 + s * (14.0941 + s * (-7.0261 + s * 2.7081))));
  const double b = 0.0005 + s * (-0.0056 + s * (-0.0066 + s * (-0.0375 + s * (0.0636 + s * -0.0144))));
  const double dS = (t - 15.0) / (1.0 + 0.0162 * (t - 15.0)) * b;
  return (float)(a + dS);
}

// ---------------------------------------------------------- ammonia ---------
float emersonPKa(float tempC) { return 0.09018f + 2729.92f / (tempC + 273.2f); }

float emersonNh3Fraction(float ph, float tempC) {
  return 1.0f / (powf(10.0f, emersonPKa(tempC) - ph) + 1.0f);
}

}  // namespace sm
