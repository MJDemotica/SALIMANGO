# Sensor Calibration Guide

**Project:** Solar Automated LoRa-Integrated Mud Crab Aeration Network with Crab Grid Observation

Calibrate before trusting any reading, and log every calibration. Calibration runs **on the ESP32**;
the dashboard's **Calibration** tab and the serial commands only start, capture, save or cancel it.
Saved values are stored in the ESP32's flash (NVS), applied to every following reading, and recorded
in Supabase (`calibration_records`) with a version and an id. Every history row records which
calibration was in force.

> **Software configuration is not physical calibration.** The firmware uses the DFR0300's cell constant
> **K = 1** and the documented algorithms, but a sensor is only calibrated once the procedure below has
> been carried out with real standards, and only validated once it has been cross-checked against an
> independent reference. None of the sensors was calibrated as part of this software revision.

## 1. Schedule

| Task | Frequency |
|------|-----------|
| pH calibration (2–3 buffers) | Before deployment, then every 2–4 weeks, after every probe cleaning |
| EC calibration (1.413 + 12.88 mS/cm) | Before deployment, monthly, after probe cleaning |
| DO saturation calibration | Before each deployment |
| DO membrane cap + electrolyte service | Monthly or on drift ([`BOM.md`](BOM.md) #25) |
| Temperature cross-check | Before deployment, monthly |
| Refractometer zero check | Weekly and before every salinity cross-check |

## 2. How a calibration session works

| Step | Dashboard (Calibration tab) | Serial (115200, newline) |
|------|-----------------------------|--------------------------|
| Start | **Start** | `enterph` / `enterec` / `enterdo` |
| Capture a point | **Capture** (when it says *Stable*) | `calph` / `calec` / `caldo` |
| Save | **Save** | `exitph` / `exitec` / `exitdo` |
| Cancel without saving | **Cancel** | `abortph` / `abortec` / `abortdo` |

- The device must be online for the dashboard buttons (sessions are in the device, not the browser).
- Each button shows the device's answer: *Done* with the result, or *Refused by the device* with the
  reason (not stable, no temperature, solution not recognised, slope out of range…).
- A session left open for 30 minutes is cancelled automatically, nothing saved.
- The water temperature probe must read correctly: every capture is refused while the DS18B20 is
  faulted.

## 3. pH (E-201-C electrode + PH-4502C board)

Algorithm: from [`ph sense/ph_cali_no_ten_k2`](../ph%20sense/ph_cali_no_ten_k2/ph_cali_no_ten_k2.ino),
now in `sensor_math.cpp`.

- **Buffers:** 4.01 (phthalate), 6.86 (phosphate), **9.18** (borax), values at 25 °C (team-confirmed
  labels). The firmware recognises the buffer automatically (nearest within ±1 pH) and uses its value at
  the measured temperature (table 15–40 °C).
- **1 buffer:** offset only (slope kept). **2–3 buffers:** least-squares line through the points.
- **Temperature compensation:** the slope is scaled with absolute temperature relative to the
  calibration temperature (Nernst).
- **Rejected when:** no valid temperature; voltage not stable (5 readings within 5 mV); reading not
  near any buffer; resulting slope outside 0.12–0.24 V/pH (worn or dry electrode, or wrong buffer).
- **Divider:** the pin voltage is scaled by 1.5 (10 kΩ + 20 kΩ as built). Recalibrate after any change
  to R7/R8.

Procedure:

1. Rinse the probe with distilled water and blot (never wipe) dry.
2. If the board has never been adjusted, set its offset trimmer so pH 7 reads about 2.50 V
   (the default the firmware uses before calibration).
3. Start, place the probe **and the temperature probe** in 6.86, wait for *Stable*, Capture.
4. Rinse, repeat with 9.18 (and 4.01 for three points), then Save.
5. Acceptance: re-read each buffer; within ±0.05 pH.
6. Store the electrode in **KCl storage solution** ([`BOM.md`](BOM.md) #26) — never dry, never in
   distilled water.

## 4. Salinity / EC (DFRobot DFR0300, K = 1)

Algorithm: from [`salinity sense/salimango_ec_calibration_wifi`](../salinity%20sense/salimango_ec_calibration_wifi/salimango_ec_calibration_wifi.ino),
now in `sensor_math.cpp` / `sensors.cpp`. DFRobot references:
[temperature compensation](https://wiki.dfrobot.com/dfr0300/docs/19240),
[calibration](https://wiki.dfrobot.com/dfr0300/docs/20349).

**Cell constant and correction factors are different things:**

| Quantity | Value | Where |
|----------|-------|-------|
| Cell constant **K** | **1** (team-confirmed probe) | `EC_CELL_CONSTANT_K` in `sensor_math.h`; applied once, in the raw conversion |
| DFR0300 conversion | raw (mS/cm) = K × 1000 × mV / (820 × 200) | `ecRawMsCm()` |
| Correction factors (DFRobot "kvalue") | ≈ 1.0, one for the low range, one for the high range; accepted 0.5–1.5 | Result of calibration, NVS `ec_cal` |
| Temperature compensation | EC25 = EC / (1 + 0.0185 (T − 25)) | `ecTempFactor()` |

The correction factor multiplies the raw K = 1 reading; the cell constant is never applied a second
time.

- **Standards:** 1.413 mS/cm (1413 µS/cm) and 12.88 mS/cm (values at 25 °C). The firmware recognises
  which one the probe is in at the measured temperature.
- **12.88 mS/cm is a conductivity standard, not seawater.** At 25 °C, 35 ppt seawater is about
  53 mS/cm.
- **Range limit of K = 1:** reliable to about 18 mS/cm on the ESP32 ADC (≈ 11 ppt at 25 °C). Above that
  the reading is reported as *over range*, never as a number. The SEAFDEC target of 28–32 ppt
  (≈ 43–50 mS/cm) cannot be measured with this probe.
- **Rejected when:** no valid temperature; not stable (5 one-second windows within ±1 %, min 5 mV);
  reading matches neither standard; factor outside 0.5–1.5 (fouled probe, wrong solution, or not a K = 1
  probe).

Procedure:

1. Rinse with distilled water, blot dry.
2. Start; place the EC probe **and the temperature probe** in 1.413 mS/cm; wait for *Stable*; Capture.
3. Rinse; 12.88 mS/cm; *Stable*; Capture; Save. (Saving with one standard updates only that range's
   factor; the other keeps its previous value.)
4. Acceptance: each standard reads within ±2 %.

**Salinity validation is separate from conductivity calibration.** Salinity is computed with PSS-78
from the calibrated conductivity and the water temperature. Cross-check it against the handheld
refractometer ([`BOM.md`](BOM.md) #27, zeroed in distilled water first) at deployment and after rain
events, and record the result. The refractometer is the reference for salinity — never the reverse.

## 5. Dissolved oxygen (DFRobot SEN0237-A)

Algorithm: from [`DOSENSE/DO_ESP32_DS18B20_WiFi4`](../DOSENSE/DO_ESP32_DS18B20_WiFi4/DO_ESP32_DS18B20_WiFi4.ino),
following the [SEN0237-A wiki](https://wiki.dfrobot.com/sen0237-a/).

- **Principle:** saturation calibration. DO = V × DO_sat(T) / V_sat(T), with the DFRobot saturation
  table (0–40 °C).
- **Single point:** one capture in saturated conditions; V_sat follows +35 mV/°C from that point.
- **Two point:** a second capture in a sample at least 5 °C warmer or colder; V_sat is interpolated.
- **Rejected when:** no valid temperature; voltage outside 100–3100 mV; second point within 5 °C of
  the first.
- **While calibrating, AUTO aeration is held** (the probe is out of the pond). Calibrate when someone
  is watching the crabs, or put the selector at M and run the air pumps manually.

Procedure:

1. Service the membrane cap and electrolyte if due; no air bubbles under the membrane.
2. Start; hold the **wet** probe in air just above water (or in water aerated for several minutes);
   wait for the voltage to stop drifting; Capture; Save (or capture a second temperature first).
3. During normal readings, provide gentle flow across the membrane (stagnant water reads low).

**DO9100 handheld meter.** The DO9100 is a separate reference instrument with its own calibration
procedure, described in its own manual. It is not calibrated by this system and its procedure is not
the SEN0237-A procedure. Use it only to **cross-check** the SEN0237-A reading after calibration
(acceptance: within ±0.3 mg/L, [`TESTING.md`](TESTING.md) SEN-04), and as the fallback while the probe
is serviced. One DO probe is a single point of failure: keep spare membrane caps and electrolyte.

## 6. Temperature (DS18B20)

There is no temperature calibration in the firmware or the dashboard. Cross-check the probe against a
reference thermometer in the same water bath (within ±0.5 °C, [`TESTING.md`](TESTING.md) SEN-01).
A reading of −127 °C means a wiring or pull-up fault ([`TROUBLESHOOTING.md`](TROUBLESHOOTING.md)).

Temperature feeds pH (Nernst slope), EC (compensation and standard recognition), salinity (PSS-78),
DO (saturation) and the NH₃ fraction. When it is faulted, pH, EC and DO are computed at 25 °C and
marked *approximate*; salinity and NH₃ are not shown.

## 7. Ammonia toxicity (calculated, not calibrated)

The unionized-ammonia fraction is calculated, so there is nothing to calibrate. It depends on the
calibrated pH and the water temperature:

pKa = 0.09018 + 2729.92 / T(K), T(K) = °C + 273.2;  NH₃ fraction = 1 / (10^(pKa − pH) + 1)

Source: Emerson, K., Russo, R. C., Lund, R. E. & Thurston, R. V. (1975). Aqueous ammonia equilibrium
calculations: effect of pH and temperature. *J. Fish. Res. Board Can.* 32: 2379–2383. (Check value:
25 °C, pH 8.0 → 5.38 %.)

Assumptions: fresh-water equilibrium (in brackish water the true fraction is somewhat lower, so the
estimate errs on the safe side); shown only for pH 6–10 and 0–40 °C. It is a **fraction**. An NH₃
concentration needs a measured total ammonia nitrogen (TAN) value, which this system does not have.

## 8. Calibration log template

| Date | Sensor | Standards / buffers | Result shown by the device (version, slope / factors / points) | Cross-check (reference, result) | By |
|------|--------|---------------------|-----------------------------------------------------------------|----------------------------------|----|
| | | | | | |

The dashboard keeps the device-side history automatically (Calibration tab → *Previous calibrations*);
this log adds the physical details (buffer lot, reference reading, who did it).
