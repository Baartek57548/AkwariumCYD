import {
  createHash,
  createHmac,
  timingSafeEqual,
} from 'node:crypto';

const HEX_64 = /^[a-f0-9]{64}$/i;
const NONCE = /^[A-Za-z0-9_-]{16,96}$/;
const DEVICE_ID = /^[A-Za-z0-9_-]{4,64}$/;
const BASE64 = /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/;

export function sha256Hex(value) {
  return createHash('sha256').update(value).digest('hex');
}

export function normalizeDeviceId(value) {
  if (typeof value !== 'string' || !DEVICE_ID.test(value)) {
    throw new TypeError('Nieprawidłowy identyfikator urządzenia.');
  }
  return value;
}

export function decodeDeviceSecret(encoded) {
  if (typeof encoded !== 'string' || !encoded.startsWith('base64:')) {
    throw new TypeError('Sekret urządzenia musi używać formatu base64:.');
  }
  const base64 = encoded.slice(7);
  if (!BASE64.test(base64)) {
    throw new TypeError('Sekret urządzenia nie jest kanonicznym Base64.');
  }
  const value = Buffer.from(base64, 'base64');
  if (
    value.length < 32
    || value.length > 64
    || value.toString('base64') !== base64
  ) {
    throw new TypeError('Sekret urządzenia musi mieć od 32 do 64 bajtów.');
  }
  return value;
}

export function canonicalRequest({
  timestamp,
  nonce,
  method,
  pathname,
  body,
}) {
  return [
    String(timestamp),
    nonce,
    method.toUpperCase(),
    pathname,
    sha256Hex(body),
  ].join('\n');
}

export function signRequest(secret, request) {
  return createHmac('sha256', secret)
    .update(canonicalRequest(request))
    .digest('hex');
}

export function verifySignedRequest({
  secret,
  timestampHeader,
  nonce,
  signatureHeader,
  method,
  pathname,
  body,
  nowSeconds,
  maximumClockSkewSeconds,
}) {
  const timestamp = Number(timestampHeader);
  if (!Number.isSafeInteger(timestamp) || timestamp <= 0) {
    return { ok: false, reason: 'invalid_timestamp' };
  }
  if (Math.abs(nowSeconds - timestamp) > maximumClockSkewSeconds) {
    return { ok: false, reason: 'expired_timestamp' };
  }
  if (typeof nonce !== 'string' || !NONCE.test(nonce)) {
    return { ok: false, reason: 'invalid_nonce' };
  }
  const rawSignature = typeof signatureHeader === 'string'
    ? signatureHeader.replace(/^v1=/, '')
    : '';
  if (!HEX_64.test(rawSignature)) {
    return { ok: false, reason: 'invalid_signature' };
  }
  const expected = signRequest(secret, {
    timestamp,
    nonce,
    method,
    pathname,
    body,
  });
  const expectedBuffer = Buffer.from(expected, 'hex');
  const receivedBuffer = Buffer.from(rawSignature, 'hex');
  if (
    expectedBuffer.length !== receivedBuffer.length
    || !timingSafeEqual(expectedBuffer, receivedBuffer)
  ) {
    return { ok: false, reason: 'invalid_signature' };
  }
  return { ok: true, timestamp, nonce };
}

export function verifyBearerToken(rawAuthorization, expectedHash) {
  if (
    typeof rawAuthorization !== 'string'
    || !rawAuthorization.startsWith('Bearer ')
    || typeof expectedHash !== 'string'
    || !HEX_64.test(expectedHash)
  ) {
    return false;
  }
  const token = rawAuthorization.slice(7);
  if (token.length < 24 || token.length > 512) {
    return false;
  }
  const received = Buffer.from(sha256Hex(token), 'hex');
  const expected = Buffer.from(expectedHash, 'hex');
  return timingSafeEqual(received, expected);
}

export class NonceCache {
  constructor({ lifetimeSeconds = 600, maximumEntries = 10_000 } = {}) {
    this.lifetimeSeconds = lifetimeSeconds;
    this.maximumEntries = maximumEntries;
    this.entries = new Map();
  }

  consume(deviceId, nonce, nowSeconds) {
    this.prune(nowSeconds);
    const key = `${deviceId}:${nonce}`;
    if (this.entries.has(key)) {
      return false;
    }
    if (this.entries.size >= this.maximumEntries) {
      const oldest = this.entries.keys().next().value;
      if (oldest !== undefined) {
        this.entries.delete(oldest);
      }
    }
    this.entries.set(key, nowSeconds + this.lifetimeSeconds);
    return true;
  }

  prune(nowSeconds) {
    for (const [key, expiresAt] of this.entries) {
      if (expiresAt > nowSeconds) {
        break;
      }
      this.entries.delete(key);
    }
  }
}

export class TokenBucket {
  constructor({ refillPerSecond, burst }) {
    this.refillPerSecond = refillPerSecond;
    this.burst = burst;
    this.state = new Map();
  }

  consume(key, nowMilliseconds) {
    const previous = this.state.get(key) ?? {
      tokens: this.burst,
      updatedAt: nowMilliseconds,
    };
    const elapsedSeconds = Math.max(
      0,
      (nowMilliseconds - previous.updatedAt) / 1000,
    );
    const tokens = Math.min(
      this.burst,
      previous.tokens + elapsedSeconds * this.refillPerSecond,
    );
    if (tokens < 1) {
      this.state.set(key, { tokens, updatedAt: nowMilliseconds });
      return false;
    }
    this.state.set(key, {
      tokens: tokens - 1,
      updatedAt: nowMilliseconds,
    });
    return true;
  }
}
