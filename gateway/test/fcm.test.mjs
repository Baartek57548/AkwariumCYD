import assert from 'node:assert/strict';
import test from 'node:test';

import { FirebasePushClient } from '../src/fcm.mjs';

const token = 'fcm-token-value-that-is-long-enough-1234567890';

function sampleEvent() {
  return {
    deviceId: 'aquarium-main',
    eventId: 'evt-00000001',
    type: 'temperature.high',
    severity: 'critical',
    state: 'raised',
    title: 'Za wysoka temperatura',
    message: 'Temperatura wody przekroczyła bezpieczny próg.',
    occurredAt: '2026-07-29T12:00:00.000Z',
  };
}

test('sends a validated data-only alarm payload for deterministic app actions', async () => {
  let capturedUrl;
  let capturedRequest;
  const client = new FirebasePushClient({
    serviceAccountPath: 'configured-at-runtime.json',
    fetchImplementation: async (url, request) => {
      capturedUrl = url;
      capturedRequest = request;
      return new Response('{}', { status: 200 });
    },
  });
  client.serviceAccount = { project_id: 'aquacyd-production' };
  client.getAccessToken = async () => 'access-token';

  const result = await client.send([token], sampleEvent());

  assert.deepEqual(result, { sent: 1, invalidTokens: [] });
  assert.equal(
    capturedUrl,
    'https://fcm.googleapis.com/v1/projects/aquacyd-production/messages:send',
  );
  assert.equal(capturedRequest.headers.authorization, 'Bearer access-token');
  const payload = JSON.parse(capturedRequest.body);
  assert.equal(payload.message.token, token);
  assert.equal(payload.message.notification, undefined);
  assert.deepEqual(payload.message.data, {
    deviceId: 'aquarium-main',
    eventId: 'evt-00000001',
    id: 'evt-00000001',
    type: 'alarm',
    eventType: 'temperature.high',
    title: 'Za wysoka temperatura',
    body: 'Temperatura wody przekroczyła bezpieczny próg.',
    severity: 'critical',
    state: 'raised',
    occurredAt: '2026-07-29T12:00:00.000Z',
  });
  assert.equal(payload.message.android.priority, 'high');
  assert.equal(payload.message.android.ttl, '3600s');
});

test('reports an unregistered token without retrying every recipient', async () => {
  const client = new FirebasePushClient({
    serviceAccountPath: 'configured-at-runtime.json',
    fetchImplementation: async () => new Response(
      JSON.stringify({ error: { status: 'UNREGISTERED' } }),
      { status: 404 },
    ),
  });
  client.serviceAccount = { project_id: 'aquacyd-production' };
  client.getAccessToken = async () => 'access-token';

  const result = await client.send([token], sampleEvent());

  assert.deepEqual(result, { sent: 0, invalidTokens: [token] });
});
