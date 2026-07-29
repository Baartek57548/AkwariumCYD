import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { promisify } from 'node:util';
import test from 'node:test';

const execFileAsync = promisify(execFile);
const script = path.resolve('scripts/generate-secrets.mjs');

test('generator prints the same one-time Base64 HMAC secret that it stores for the gateway', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'aquacyd-secrets-'));
  const output = path.join(directory, 'gateway-secrets.json');
  try {
    const { stdout, stderr } = await execFileAsync(
      process.execPath,
      [script, 'aquarium-test', output],
      { cwd: path.resolve('.') },
    );
    assert.equal(stderr, '');

    const match = stdout.match(
      /Sekret HMAC urządzenia Base64[^:]*: ([A-Za-z0-9+/]+={0,2})/,
    );
    assert.notEqual(match, null);
    const displayedSecret = match[1];
    assert.equal(Buffer.from(displayedSecret, 'base64').length, 32);

    const configuration = JSON.parse(await readFile(output, 'utf8'));
    assert.equal(
      configuration.devices['aquarium-test'].hmacSecret,
      `base64:${displayedSecret}`,
    );
    assert.match(stdout, /sparowane i szyfrowane BLE/);
    assert.match(stdout, /nie wolno zapisywać w aplikacji/);
    assert.match(stdout, /HTTP\/HTTPS/);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('generator refuses to overwrite an existing secrets file', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'aquacyd-secrets-'));
  const output = path.join(directory, 'gateway-secrets.json');
  try {
    await execFileAsync(
      process.execPath,
      [script, 'aquarium-test', output],
      { cwd: path.resolve('.') },
    );
    await assert.rejects(
      execFileAsync(
        process.execPath,
        [script, 'aquarium-test', output],
        { cwd: path.resolve('.') },
      ),
    );
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
