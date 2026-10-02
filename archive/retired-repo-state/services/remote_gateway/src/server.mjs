import { readFile } from 'node:fs/promises';
import path from 'node:path';

import { createGateway } from './gateway.mjs';

function requiredEnvironment(name) {
  const value = process.env[name];
  if (typeof value !== 'string' || value.trim().length === 0) {
    throw new Error(`Brak wymaganej zmiennej ${name}.`);
  }
  return value;
}

function loadTlsConfiguration() {
  const certPath = process.env.AQUACYD_GATEWAY_TLS_CERT?.trim() ?? '';
  const keyPath = process.env.AQUACYD_GATEWAY_TLS_KEY?.trim() ?? '';
  if ((certPath.length === 0) !== (keyPath.length === 0)) {
    throw new Error(
      'AQUACYD_GATEWAY_TLS_CERT i AQUACYD_GATEWAY_TLS_KEY muszą być ustawione razem.',
    );
  }
  return certPath.length > 0
    ? {
        certPath: path.resolve(certPath),
        keyPath: path.resolve(keyPath),
      }
    : null;
}

async function loadConfiguration() {
  const secretsPath = path.resolve(requiredEnvironment('AQUACYD_GATEWAY_SECRETS'));
  const secrets = JSON.parse(await readFile(secretsPath, 'utf8'));
  return {
    host: process.env.AQUACYD_GATEWAY_HOST ?? '127.0.0.1',
    port: Number(process.env.AQUACYD_GATEWAY_PORT ?? 8787),
    stateDirectory: path.resolve(
      process.env.AQUACYD_GATEWAY_STATE ?? './gateway-state',
    ),
    maximumClockSkewSeconds: Number(
      process.env.AQUACYD_GATEWAY_CLOCK_SKEW_SECONDS ?? 300,
    ),
    allowedOrigins: (process.env.AQUACYD_GATEWAY_ALLOWED_ORIGINS ?? '')
      .split(',')
      .map((value) => value.trim())
      .filter(Boolean),
    firebaseServiceAccountPath:
      process.env.AQUACYD_FIREBASE_SERVICE_ACCOUNT ?? '',
    tls: loadTlsConfiguration(),
    devices: secrets.devices,
  };
}

const gateway = await createGateway(await loadConfiguration());
const address = await gateway.listen();
console.log(
  `AquaCYD gateway listening on ${address.address}:${address.port}`,
);

let stopping = false;
async function stop(signal) {
  if (stopping) {
    return;
  }
  stopping = true;
  console.log(`Received ${signal}; closing gateway.`);
  const forced = setTimeout(() => process.exit(1), 10_000);
  forced.unref();
  await gateway.close();
  process.exitCode = 0;
}

process.on('SIGINT', () => {
  void stop('SIGINT');
});
process.on('SIGTERM', () => {
  void stop('SIGTERM');
});
