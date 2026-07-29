import assert from 'node:assert/strict';
import { mkdtemp, readdir, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { EventStore } from '../src/event-store.mjs';

const deviceId = 'aquarium-main';

function event(sequence) {
  return {
    bootId: 'boot-storage-test',
    eventId: `evt-storage-${String(sequence).padStart(4, '0')}`,
    receiptId: `receipt-storage-${String(sequence).padStart(4, '0')}`,
    sequence,
    message: 'x'.repeat(700),
  };
}

test('rotates bounded JSONL segments and reads newest events first', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'aquacyd-store-'));
  try {
    const store = new EventStore(directory, {
      maximumEventFileBytes: 1024,
    });
    await store.initialize([deviceId]);
    await store.appendUnique(deviceId, event(1));
    await store.appendUnique(deviceId, event(2));

    const files = (await readdir(
      path.join(directory, 'devices', deviceId),
    )).filter((name) => name.startsWith('events-'));
    assert.equal(files.length, 2);
    assert.equal(files.some((name) => /-001\.jsonl$/.test(name)), true);

    const latest = await store.latest(deviceId, 2);
    assert.deepEqual(
      latest.map((item) => item.sequence),
      [2, 1],
    );
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('serializes concurrent push-token mutations without losing a token', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'aquacyd-store-'));
  const first = 'fcm-token-first-abcdefghijklmnopqrstuvwxyz-123456';
  const second = 'fcm-token-second-abcdefghijklmnopqrstuvwxyz-12345';
  try {
    const store = new EventStore(directory);
    await store.initialize([deviceId]);
    await Promise.all([
      store.addPushToken(deviceId, first),
      store.addPushToken(deviceId, second),
    ]);
    assert.deepEqual(
      new Set(await store.loadPushTokens(deviceId)),
      new Set([first, second]),
    );
    await assert.rejects(
      store.removePushToken(deviceId, 'invalid'),
      /Nieprawidłowy token FCM/,
    );
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
