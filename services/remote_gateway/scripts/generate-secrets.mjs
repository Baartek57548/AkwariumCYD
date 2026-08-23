import { createHash, randomBytes } from 'node:crypto';
import { writeFile } from 'node:fs/promises';
import path from 'node:path';

const DEVICE_ID = /^[A-Za-z0-9_-]{4,64}$/;
const deviceId = process.argv[2] ?? 'aquarium-main';
const output = path.resolve(process.argv[3] ?? 'aquacyd-gateway-secrets.json');

if (!DEVICE_ID.test(deviceId)) {
  throw new TypeError(
    'Identyfikator urządzenia musi mieć 4-64 znaki: litery, cyfry, _ lub -.',
  );
}

const hmacSecret = randomBytes(32);
const hmacSecretBase64 = hmacSecret.toString('base64');
const viewerToken = randomBytes(32).toString('base64url');
const viewerTokenSha256 = createHash('sha256')
  .update(viewerToken)
  .digest('hex');
const configuration = {
  devices: {
    [deviceId]: {
      hmacSecret: `base64:${hmacSecretBase64}`,
      viewerTokenSha256,
    },
  },
};

await writeFile(output, `${JSON.stringify(configuration, null, 2)}\n`, {
  encoding: 'utf8',
  mode: 0o600,
  flag: 'wx',
});

process.stdout.write(
  [
    `Zapisano prywatną konfigurację: ${output}`,
    `Device ID: ${deviceId}`,
    `Sekret HMAC urządzenia Base64 (przekaż wyłącznie przez sparowane i szyfrowane BLE): ${hmacSecretBase64}`,
    `Token aplikacji (zapisz teraz, nie będzie możliwy do odtworzenia): ${viewerToken}`,
    'Sekretu HMAC nie wolno zapisywać w aplikacji ani przesyłać przez HTTP/HTTPS.',
    '',
  ].join('\n'),
);
