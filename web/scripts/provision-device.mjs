#!/usr/bin/env node
// Provisions one float node: generates the sticker access code and the device
// token, registers the device in Supabase, and writes the sticker QR code.
//
// Usage (run on an admin computer, never in the browser or on the ESP32):
//   node scripts/provision-device.mjs --name "Float node 1" --url https://<your-app>.vercel.app
//       registers through the Supabase API; needs SUPABASE_URL and
//       SUPABASE_SERVICE_ROLE_KEY in the environment (admin machine only)
//   node scripts/provision-device.mjs --name "Float node 1" --url https://... --sql
//       prints the SQL to paste into the Supabase SQL editor instead
//
// Output goes to ./provisioning/<code>/ (git-ignored):
//   sticker-qr.svg   QR code that opens <url>/?code=<CODE>
//   secrets.h        values to copy into firmware/salimango_node/secrets.h
//   README.txt       the code to print under the QR and how to revoke it
import { randomBytes } from 'node:crypto';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { parseArgs } from 'node:util';
import QRCode from 'qrcode';
import { createClient } from '@supabase/supabase-js';

// Crockford base-32 (no I, L, O, U) — matches public.normalize_access_code().
const ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';

function randomCode(length = 8) {
  const bytes = randomBytes(length);
  let out = '';
  for (const b of bytes) out += ALPHABET[b % 32]; // 256 is a multiple of 32: no bias
  return out;
}

const { values: args } = parseArgs({
  options: {
    name: { type: 'string' },
    url: { type: 'string' },
    sql: { type: 'boolean', default: false },
  },
});

if (!args.name || !args.url) {
  console.error('Usage: provision-device.mjs --name "<device name>" --url <dashboard URL> [--sql]');
  process.exit(1);
}

const dashboardUrl = new URL(args.url);
const code = randomCode(8); // 40 bits
const displayCode = `${code.slice(0, 4)}-${code.slice(4)}`;
const deviceToken = randomBytes(32).toString('base64url');
const accessUrl = new URL(dashboardUrl);
accessUrl.searchParams.set('code', displayCode);

const quote = (s) => `'${String(s).replaceAll("'", "''")}'`;
let deviceId = '(see the result of the SQL statement)';

if (args.sql) {
  console.log('-- Paste into the Supabase SQL editor. It contains secrets: clear the editor afterwards.');
  console.log(
    `select public.admin_register_device(${quote(args.name)}, ${quote(displayCode)}, ${quote(deviceToken)});`,
  );
} else {
  const url = process.env.SUPABASE_URL;
  const key = process.env.SUPABASE_SERVICE_ROLE_KEY;
  if (!url || !key) {
    console.error('Set SUPABASE_URL and SUPABASE_SERVICE_ROLE_KEY, or use --sql.');
    process.exit(1);
  }
  const admin = createClient(url, key, { auth: { persistSession: false } });
  const { data, error } = await admin.rpc('admin_register_device', {
    p_name: args.name,
    p_access_code: displayCode,
    p_device_token: deviceToken,
  });
  if (error) {
    console.error('Registration failed:', error.message);
    process.exit(1);
  }
  deviceId = data;
}

const outDir = join('provisioning', code);
mkdirSync(outDir, { recursive: true });
writeFileSync(join(outDir, 'sticker-qr.svg'), await QRCode.toString(accessUrl.href, { type: 'svg', margin: 2 }));
writeFileSync(
  join(outDir, 'secrets.h'),
  `// Copy these lines into firmware/salimango_node/secrets.h (never commit that file).
#define DEVICE_TOKEN "${deviceToken}"
`,
);
writeFileSync(
  join(outDir, 'README.txt'),
  `Device:        ${args.name}
Device id:     ${deviceId}
Access code:   ${displayCode}   (print this under the QR code)
QR opens:      ${accessUrl.href}

Anyone who has this code can view AND control the device.
If the sticker is copied or photographed by someone who should not have it,
replace the code (this signs out every phone that used the old one):
  select public.admin_rotate_access_code('<device id>', '<new code>');
then print a new sticker.
`,
);

console.log(`\nDevice:      ${args.name}`);
console.log(`Device id:   ${deviceId}`);
console.log(`Access code: ${displayCode}`);
console.log(`QR URL:      ${accessUrl.href}`);
console.log(`Files:       ${outDir}${args.sql ? '  (run the SQL above to register the device)' : ''}`);
