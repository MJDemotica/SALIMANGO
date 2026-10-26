# Setup Guide

**Project:** Solar Automated LoRa-Integrated Mud Crab Aeration Network with Crab Grid Observation

From an empty desk to a float node reporting to the dashboard. Order: tools → Supabase → Vercel →
provision the device → flash the ESP32 → first power-up.

## 1. Builder pack map

| File | Use it when |
|------|-------------|
| [`SETUP.md`](SETUP.md) (this file) | Accounts, deployment, provisioning, first power-up |
| [`WIRING.md`](WIRING.md) | Perfboard pin map, wiring, continuity checks |
| [`../firmware/README.md`](../firmware/README.md) | Uploading the firmware |
| [`FIRMWARE.md`](FIRMWARE.md) | Control rules, telemetry and command contract |
| [`APP.md`](APP.md) | Dashboard behaviour, access and security, data model |
| [`CALIBRATION.md`](CALIBRATION.md) | pH, EC, DO procedures |
| [`TESTING.md`](TESTING.md) | Automated tests and the hardware test plan |
| [`TROUBLESHOOTING.md`](TROUBLESHOOTING.md) | Fault finding |
| [`CHANGELOG.md`](CHANGELOG.md) | What changed in this revision and what is still unverified |

## 2. Tools

- **Node.js 20 or newer** and **Git**.
- **Arduino IDE 2** with **esp32 by Espressif** (tested with 3.3.12) and the libraries **OneWire**,
  **DallasTemperature**, **ArduinoJson 7**.
- CH340 or CP210x USB driver for the ESP32 DevKit.
- In `web/`: `npm install`.

**Safety gate:** all 220 V AC work (shore-end RCD, breaker, outdoor run) is a licensed-electrician job
under the Philippine Electrical Code. Builders work only on the 12 V / 5 V DC side.

## 3. Supabase

1. Create a project at [supabase.com](https://supabase.com).
2. **SQL Editor** → paste the whole of
   [`supabase/migrations/20261026000000_salimango_initial.sql`](../supabase/migrations/20261026000000_salimango_initial.sql)
   → **Run**. (With the Supabase CLI, after `supabase link`, `supabase db push` applies the files in
   `supabase/migrations/`.) This creates the tables, row-level security, functions and the Realtime
   publication.
3. **Authentication → Sign In / Providers → Allow anonymous sign-ins: on.** The dashboard has no login
   page; every browser gets an anonymous session. Optionally enable CAPTCHA protection.
4. **Project Settings → API:** note the **Project URL** and the **anon / publishable key**. The
   **service_role** key stays on your own computer (provisioning only).

## 4. Vercel (dashboard)

1. Push the repository to GitHub and **Import** it in Vercel.
2. **Root Directory: `web`** (framework preset Vite; `web/vercel.json` sets the build and SPA routing).
3. Environment variables (Production and Preview):

| Variable | Value |
|----------|-------|
| `VITE_SUPABASE_URL` | Supabase Project URL |
| `VITE_SUPABASE_ANON_KEY` | Supabase anon / publishable key |

Never add the service_role key to Vercel: every `VITE_` variable is shipped to the browser.

4. Deploy, and note the site address (for example `https://salimango.vercel.app`). Opening it should
   show the access-code page.

Local development: copy `web/.env.example` to `web/.env.local`, fill in the same two values, then
`npm run dev`.

## 5. Provision a device (access code, QR sticker, device token)

From `web/` (once per float node):

```
npm run provision -- --name "Float node 1" --url https://salimango.vercel.app --sql
```

Copy the printed `select public.admin_register_device(...)` line into the Supabase SQL Editor, run it,
then clear the editor (it contains secrets). Alternatively, set `SUPABASE_URL` and
`SUPABASE_SERVICE_ROLE_KEY` in the terminal and run the command without `--sql`.

Output in `web/provisioning/<CODE>/` (git-ignored, keep private):

| File | Use |
|------|-----|
| `sticker-qr.svg` | Print as the sticker on the house-node enclosure |
| `README.txt` | The access code to print under the QR, and how to replace it |
| `secrets.h` | The `DEVICE_TOKEN` line for the firmware |

Running the command again creates a **second** device with new codes; it does not update the first.

### Replacing a leaked code

Anyone with the code can view and control the pond. If the sticker was copied by someone who should not
have it, generate a new code (e.g. run the provisioning script once with `--sql` and keep only the new
code from its output, or choose any 8 Crockford base-32 characters), then run in the SQL Editor:

```sql
select public.admin_rotate_access_code('<device id>', '<NEW-CODE>');
```

Every phone that used the old code loses access; print a new sticker. Device token compromise:
`admin_rotate_device_token('<device id>', '<new 32+ character token>')`, then update `secrets.h` and
re-flash.

## 6. Flash the ESP32

Follow [`../firmware/README.md`](../firmware/README.md): create `firmware/salimango_node/secrets.h`
(Wi-Fi, Supabase URL, anon key, device token), select **ESP32 Dev Module**, upload, check the Serial
Monitor (`status`).

The float node joins the **house Wi-Fi directly** (2.4 GHz). Check the signal at the float before
deployment; an outdoor repeater or access point may be needed. Without Wi-Fi the node keeps sensing and
controlling locally.

## 7. First power-up (bench)

1. Run the perfboard continuity checklist ([`WIRING.md`](WIRING.md) §8) with the ESP32 removed.
2. Pumps disconnected; ESP32 on laptop USB only. The firmware switches the air pumps **on at boot**.
3. Flash; confirm in the Serial Monitor: DS18B20 found, calibration lines, `[event:boot]`, then
   `status` shows `Supabase ok`.
4. Open the dashboard via the QR code: **Live**, values on all five cards (or an explained status).
5. Selector through M, O, A: the dashboard's "Selector on the box" follows.
6. Feed the 12 V supply from a current-limited bench supply; connect pumps in buckets; test each relay
   in MANUAL ([`TESTING.md`](TESTING.md) ACT-01).
7. Calibrate pH, EC and DO ([`CALIBRATION.md`](CALIBRATION.md)) before trusting any reading.
8. Mains last, after the electrician's sign-off; press the RCD test button monthly.

## 8. Trying the dashboard without hardware

With a provisioned device token:

```
cd web
$env:SUPABASE_URL = "https://<ref>.supabase.co"; $env:SUPABASE_ANON_KEY = "<anon key>"; $env:DEVICE_TOKEN = "<token>"
npm run simulate
```

(PowerShell syntax; in bash use `SUPABASE_URL=... npm run simulate`.) The simulator reports random-walk
values flagged **simulated**; the dashboard shows a red SIMULATED DATA banner and the CSV marks every
row. Use a separate test device for this, so simulated rows never mix with pond data.
