# ph sense — earlier pH sketches (kept for reference)

**For the float node, flash `firmware/salimango_node/` instead.** These sketches are kept unchanged
until the website is finalised.

All three read the pH board on **GPIO 35**. On the perfboard (`perfboard_build_guide.pdf`) the pH
divider node goes to **GPIO 34** and GPIO 35 is the EC sensor, so these sketches read the wrong sensor
on the built board.

| Sketch | Algorithm | Status |
|--------|-----------|--------|
| `ph_cali_no_ten_k2` | Nernst-scaled slope relative to the calibration temperature; 1–3 buffers (4.01 / 6.86 / 9.18, temperature-corrected); slope check 0.12–0.24 V/pH; stored in NVS `ph` | **Reused** in the new firmware (`sensor_math.cpp`, `sensors.cpp`). A calibration saved with this sketch is picked up (version 1). |
| `ph_cali_final_not_sure` | Same buffers, but adds a fixed 0.03 pH/°C offset instead of scaling the slope; stored in NVS `ph_cmd` | Not used: an additive offset does not model the electrode's temperature dependence. Its calibration is **not** carried over. |
| `ph_cali_no_ten_k` | Fixed constants (2.5 V, 0.18 V/pH), no calibration | Not used. |

Divider: these sketches use a ratio of 1.5 (10 kΩ + 20 kΩ), which matches the board as built (R8 = 20 kΩ;
the perfboard guide lists 18 kΩ).
