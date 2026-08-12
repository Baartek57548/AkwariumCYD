import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { createGateway } from '../src/gateway.mjs';
import { sha256Hex, signRequest } from '../src/security.mjs';

const deviceId = 'aquarium-main';
const viewerToken = 'viewer-token-with-at-least-24-characters';
const secret = randomBytes(32);
let directory;
let gateway;
let origin;

function gatewayConfiguration() {
  return {
    host: '127.0.0.1',
    port: 0,
    stateDirectory: directory,
    devices: {
      [deviceId]: {
        hmacSecret: `base64:${secret.toString('base64')}`,
        viewerTokenSha256: sha256Hex(viewerToken),
      },
    },
  };
}

function signedHeaders(pathname, body, nonce = randomBytes(18).toString('base64url')) {
  const timestamp = Math.floor(Date.now() / 1000);
  return {
    'content-type': 'application/json',
    'x-aquacyd-timestamp': String(timestamp),
    'x-aquacyd-nonce': nonce,
    'x-aquacyd-signature': `v1=${signRequest(secret, {
      timestamp,
      nonce,
      method: 'POST',
      pathname,
      body,
    })}`,
  };
}

function sampleEvent(overrides = {}) {
  return {
    eventId: 'evt-00000001',
    bootId: 'boot-00000001',
    sequence: 1,
    type: 'sensor.temperature_high',
    severity: 'critical',
    state: 'raised',
    title: 'Za wysoka temperatura',
    message: 'Temperatura wody przekroczyła bezpieczny próg.',
    measurement: { value: 30.5, unit: 'C' },
    ...overrides,
  };
}

test('rejects malformed credentials and unsafe CORS origins at startup', async () => {
  await assert.rejects(
    createGateway({
      ...gatewayConfiguration(),
      stateDirectory: os.tmpdir(),
      devices: {
        [deviceId]: {
          hmacSecret: `base64:${secret.toString('base64')}`,
          viewerTokenSha256: 'not-a-sha256',
        },
      },
    }),
    /uwierzytelniania/,
  );
  await assert.rejects(
    createGateway({
      ...gatewayConfiguration(),
      stateDirectory: os.tmpdir(),
      allowedOrigins: ['http://panel.example.org'],
    }),
    /kanonicznym HTTPS/,
  );
});

test.before(async () => {
  directory = await mkdtemp(path.join(os.tmpdir(), 'aquacyd-gateway-'));
  gateway = await createGateway(gatewayConfiguration());
  const address = await gateway.listen();
  origin = `http://127.0.0.1:${address.port}`;
});

test.after(async () => {
  await gateway.close();
  await rm(directory, { recursive: true, force: true });
});

test('health endpoint exposes no device secrets', async () => {
  const response = await fetch(`${origin}/healthz`);
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.status, 'ok');
  assert.equal(JSON.stringify(body).includes(secret.toString('base64')), false);
});

test('publishes an explicit no-store opt-in for the read-only HTTPS PWA', async () => {
  const response = await fetch(
    `${origin}/.well-known/aquacyd-gateway-pwa.json`,
  );
  assert.equal(response.status, 200);
  assert.equal(response.headers.get('cache-control'), 'no-store');
  assert.deepEqual(await response.json(), {
    schemaVersion: 1,
    productId: 'aquacyd-https-gateway',
    mode: 'read-only-no-command-queue',
  });
});

test('accepts a signed event and makes it available to the viewer', async () => {
  const pathname = `/api/v1/devices/${deviceId}/events`;
  const body = Buffer.from(JSON.stringify(sampleEvent()));
  const response = await fetch(`${origin}${pathname}`, {
    method: 'POST',
    headers: signedHeaders(pathname, body),
    body,
  });
  assert.equal(response.status, 202);
  const accepted = await response.json();
  assert.equal(accepted.accepted, true);
  assert.equal(accepted.duplicate, false);

  const history = await fetch(`${origin}${pathname}?limit=10`, {
    headers: { authorization: `Bearer ${viewerToken}` },
  });
  assert.equal(history.status, 200);
  const payload = await history.json();
  assert.equal(payload.events.length, 1);
  assert.equal(payload.events[0].eventId, 'evt-00000001');
  assert.equal(payload.events[0].deviceId, deviceId);
});

test('deduplicates a retried event even when the nonce changes', async () => {
  const pathname = `/api/v1/devices/${deviceId}/events`;
  const body = Buffer.from(JSON.stringify(sampleEvent()));
  const response = await fetch(`${origin}${pathname}`, {
    method: 'POST',
    headers: signedHeaders(pathname, body),
    body,
  });
  assert.equal(response.status, 200);
  const duplicate = await response.json();
  assert.equal(duplicate.accepted, true);
  assert.equal(duplicate.duplicate, true);

  const history = await fetch(`${origin}${pathname}?limit=10`, {
    headers: { authorization: `Bearer ${viewerToken}` },
  });
  assert.equal(history.status, 200);
  const matching = (await history.json()).events.filter(
    (event) => event.eventId === 'evt-00000001',
  );
  assert.equal(matching.length, 1);
  assert.equal(matching[0].receiptId, duplicate.receiptId);
});

test('rejects a replayed nonce before processing the event', async () => {
  const pathname = `/api/v1/devices/${deviceId}/events`;
  const body = Buffer.from(JSON.stringify(sampleEvent({
    eventId: 'evt-00000002',
    sequence: 2,
  })));
  const nonce = 'fixed_nonce_1234567890';
  const headers = signedHeaders(pathname, body, nonce);
  const first = await fetch(`${origin}${pathname}`, {
    method: 'POST',
    headers,
    body,
  });
  assert.equal(first.status, 202);
  const replay = await fetch(`${origin}${pathname}`, {
    method: 'POST',
    headers,
    body,
  });
  assert.equal(replay.status, 409);
  assert.equal((await replay.json()).error, 'replayed_nonce');
});

test('rejects an invalid signature and unauthorized history reads', async () => {
  const pathname = `/api/v1/devices/${deviceId}/events`;
  const body = Buffer.from(JSON.stringify(sampleEvent({
    eventId: 'evt-00000003',
    sequence: 3,
  })));
  const bad = await fetch(`${origin}${pathname}`, {
    method: 'POST',
    headers: {
      ...signedHeaders(pathname, body),
      'x-aquacyd-signature': `v1=${'0'.repeat(64)}`,
    },
    body,
  });
  assert.equal(bad.status, 401);
  const history = await fetch(`${origin}${pathname}`, {
    headers: { authorization: 'Bearer wrong-token-with-at-least-24-chars' },
  });
  assert.equal(history.status, 401);
});

test('invalid signatures cannot exhaust the authenticated event budget', async () => {
  const pathname = `/api/v1/devices/${deviceId}/events`;
  const body = Buffer.from(JSON.stringify(sampleEvent({
    eventId: 'evt-rate-budget-01',
    sequence: 31,
  })));
  for (let attempt = 0; attempt < 35; attempt += 1) {
    const rejected = await fetch(`${origin}${pathname}`, {
      method: 'POST',
      headers: {
        ...signedHeaders(pathname, body),
        'x-aquacyd-signature': `v1=${'0'.repeat(64)}`,
      },
      body,
    });
    assert.equal(rejected.status, 401);
  }
  const valid = await fetch(`${origin}${pathname}`, {
    method: 'POST',
    headers: signedHeaders(pathname, body),
    body,
  });
  assert.equal(valid.status, 202);
});

test('registers and removes an authenticated FCM token', async () => {
  const pathname = `/api/v1/devices/${deviceId}/push-tokens`;
  const token = 'fcm-token-value-that-is-long-enough-1234567890';
  const registered = await fetch(`${origin}${pathname}`, {
    method: 'POST',
    headers: {
      authorization: `Bearer ${viewerToken}`,
      'content-type': 'application/json',
    },
    body: JSON.stringify({ token }),
  });
  assert.equal(registered.status, 200);
  assert.equal((await registered.json()).tokenCount, 1);
  const removed = await fetch(`${origin}${pathname}`, {
    method: 'DELETE',
    headers: {
      authorization: `Bearer ${viewerToken}`,
      'content-type': 'application/json',
    },
    body: JSON.stringify({ token }),
  });
  assert.equal(removed.status, 200);
  assert.equal((await removed.json()).tokenCount, 0);
});

test('preserves event idempotency after a gateway restart', async () => {
  const pathname = `/api/v1/devices/${deviceId}/events`;
  const body = Buffer.from(JSON.stringify(sampleEvent({
    eventId: 'evt-restart-0001',
    sequence: 40,
  })));
  const accepted = await fetch(`${origin}${pathname}`, {
    method: 'POST',
    headers: signedHeaders(pathname, body),
    body,
  });
  assert.equal(accepted.status, 202);
  const originalReceipt = (await accepted.json()).receiptId;

  await gateway.close();
  gateway = await createGateway(gatewayConfiguration());
  const address = await gateway.listen();
  origin = `http://127.0.0.1:${address.port}`;

  const retried = await fetch(`${origin}${pathname}`, {
    method: 'POST',
    headers: signedHeaders(pathname, body),
    body,
  });
  assert.equal(retried.status, 200);
  const duplicate = await retried.json();
  assert.equal(duplicate.duplicate, true);
  assert.equal(duplicate.receiptId, originalReceipt);
});
