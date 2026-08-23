import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import test from 'node:test';

import { decodeDeviceSecret } from '../src/security.mjs';

test('accepts only canonical Base64 secrets supported by firmware', () => {
  const minimum = randomBytes(32);
  const maximum = randomBytes(64);

  assert.deepEqual(
    decodeDeviceSecret(`base64:${minimum.toString('base64')}`),
    minimum,
  );
  assert.deepEqual(
    decodeDeviceSecret(`base64:${maximum.toString('base64')}`),
    maximum,
  );
  assert.throws(
    () => decodeDeviceSecret(`base64:${randomBytes(31).toString('base64')}`),
    /32 do 64/,
  );
  assert.throws(
    () => decodeDeviceSecret(`base64:${randomBytes(65).toString('base64')}`),
    /32 do 64/,
  );
  assert.throws(
    () => decodeDeviceSecret(`base64:${minimum.toString('base64')}!`),
    /kanonicznym Base64/,
  );
  assert.throws(
    () => decodeDeviceSecret(minimum.toString('base64')),
    /formatu base64:/,
  );
});
