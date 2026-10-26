# DOSENSE — earlier bench sketches (kept for reference)

**For the float node, flash `firmware/salimango_node/` instead.** These sketches are kept unchanged
until the website is finalised. Pins in the new firmware follow `perfboard_build_guide.pdf` (the
built board); some of these sketches do not.

| Sketch | Status | Differences from the perfboard / new firmware |
|--------|--------|-----------------------------------------------|
| `DO_ESP32_DS18B20_WiFi4` | Superseded. Its DO conversion, calibration (`enterdo`/`caldo`/`exitdo`) and low-DO relay rule were reused in the new firmware. | **DO on GPIO 34** (perfboard: GPIO 36/VP). **Relay on GPIO 26** (perfboard IN1: GPIO 25). Drives one relay only (new firmware: K1 + K5, staggered 5 s). |
| `relay_wifi_test_k1k4` | Still the relay bench test named in the perfboard guide's testing checklist. | Pins match the perfboard (25/27/33/13). The relay lead from J1 IN2 now goes to module IN5 (K2 is defective), so the button labelled "K2 Air pump 2" switches **K5**. |
| `selector_test` | Still the selector bench test named in the perfboard guide. | Matches the perfboard (M = GPIO 4, A = GPIO 21) and the new firmware's decoding. |

**Security:** `DO_ESP32_DS18B20_WiFi4` and `relay_wifi_test_k1k4` contain a Wi-Fi name and password in
plain text, and they are in the git history. Change that router password. Their local web pages have
no password; use them only on a private network for bench tests.

`DO_ESP32_DS18B20_WiFi4` stores DO calibration in the `do_cal` NVS namespace, which the new firmware
reads, so a DO calibration saved with it carries over (shown as version 1).
