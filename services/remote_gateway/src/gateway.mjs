import http from 'node:http';
import https from 'node:https';
import { readFile } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';

import { EventStore } from './event-store.mjs';
import { FirebasePushClient } from './fcm.mjs';
import {
  decodeDeviceSecret,
  NonceCache,
  normalizeDeviceId,
  TokenBucket,
  verifyBearerToken,
  verifySignedRequest,
} from './security.mjs';

const MAX_BODY_BYTES = 16 * 1024;
const EVENT_TYPE = /^[a-z][a-z0-9_.-]{2,63}$/;
const EVENT_ID = /^[A-Za-z0-9_.:-]{8,96}$/;
const SEVERITIES = new Set(['info', 'warning', 'critical']);
const STATES = new Set(['raised', 'resolved']);
const ALLOWED_METHODS = 'GET, POST, DELETE, OPTIONS';
const DEVICE_ROUTE = /^\/api\/v1\/devices\/([A-Za-z0-9_-]{4,64})(\/.*)?$/;
const SHA256_HEX = /^[a-f0-9]{64}$/;

function json(response, status, value, extraHeaders = {}) {
  const payload = Buffer.from(`${JSON.stringify(value)}\n`, 'utf8');
  response.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': payload.length,
    'cache-control': 'no-store',
    ...extraHeaders,
  });
  response.end(payload);
}

function securityHeaders(response) {
  response.setHeader('x-content-type-options', 'nosniff');
  response.setHeader('referrer-policy', 'no-referrer');
  response.setHeader('x-frame-options', 'DENY');
  response.setHeader(
    'content-security-policy',
    "default-src 'none'; frame-ancestors 'none'",
  );
  response.setHeader('permissions-policy', 'camera=(), microphone=(), geolocation=()');
}

async function readBoundedBody(request) {
  const declared = Number(request.headers['content-length'] ?? 0);
  if (Number.isFinite(declared) && declared > MAX_BODY_BYTES) {
    const error = new Error('Żądanie przekracza limit 16 KiB.');
    error.statusCode = 413;
    throw error;
  }
  const chunks = [];
  let length = 0;
  for await (const chunk of request) {
    length += chunk.length;
    if (length > MAX_BODY_BYTES) {
      const error = new Error('Żądanie przekracza limit 16 KiB.');
      error.statusCode = 413;
      throw error;
    }
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

function textField(value, name, maximumLength, { required = true } = {}) {
  if (!required && (value === undefined || value === null)) {
    return '';
  }
  if (
    typeof value !== 'string'
    || value.length < 1
    || value.length > maximumLength
    || /[\u0000-\u0008\u000B\u000C\u000E-\u001F]/.test(value)
  ) {
    throw new TypeError(`Pole ${name} ma nieprawidłową wartość.`);
  }
  return value;
}

function validateEvent(raw, deviceId, receivedAt) {
  if (raw === null || typeof raw !== 'object' || Array.isArray(raw)) {
    throw new TypeError('Zdarzenie musi być obiektem JSON.');
  }
  const eventId = textField(raw.eventId, 'eventId', 96);
  const bootId = textField(raw.bootId, 'bootId', 96);
  const type = textField(raw.type, 'type', 64);
  const severity = textField(raw.severity, 'severity', 16);
  const state = textField(raw.state, 'state', 16);
  if (!EVENT_ID.test(eventId) || !EVENT_ID.test(bootId)) {
    throw new TypeError('eventId lub bootId ma nieprawidłowy format.');
  }
  if (!EVENT_TYPE.test(type)) {
    throw new TypeError('Typ zdarzenia ma nieprawidłowy format.');
  }
  if (!SEVERITIES.has(severity) || !STATES.has(state)) {
    throw new TypeError('Nieprawidłowy stan lub poziom zdarzenia.');
  }
  const sequence = Number(raw.sequence);
  if (!Number.isSafeInteger(sequence) || sequence < 0) {
    throw new TypeError('Sekwencja zdarzenia musi być liczbą całkowitą.');
  }
  let occurredAt = receivedAt;
  if (raw.occurredAt !== undefined) {
    const parsed = new Date(raw.occurredAt);
    if (!Number.isFinite(parsed.getTime())) {
      throw new TypeError('Nieprawidłowy czas zdarzenia.');
    }
    occurredAt = parsed.toISOString();
  }
  const measurement = raw.measurement;
  if (
    measurement !== undefined
    && (
      measurement === null
      || typeof measurement !== 'object'
      || Array.isArray(measurement)
      || JSON.stringify(measurement).length > 2048
    )
  ) {
    throw new TypeError('Nieprawidłowe dane pomiarowe.');
  }
  return {
    schemaVersion: 1,
    deviceId,
    eventId,
    bootId,
    sequence,
    type,
    severity,
    state,
    title: textField(raw.title, 'title', 96),
    message: textField(raw.message, 'message', 512),
    occurredAt,
    receivedAt,
    measurement: measurement ?? {},
  };
}

function normalizeConfiguration(configuration) {
  if (
    configuration === null
    || typeof configuration !== 'object'
    || Array.isArray(configuration)
  ) {
    throw new TypeError('Konfiguracja gateway musi być obiektem.');
  }
  const devices = new Map();
  for (const [rawId, rawDevice] of Object.entries(configuration.devices ?? {})) {
    const deviceId = normalizeDeviceId(rawId);
    if (
      rawDevice === null
      || typeof rawDevice !== 'object'
      || Array.isArray(rawDevice)
      || !SHA256_HEX.test(rawDevice.viewerTokenSha256 ?? '')
    ) {
      throw new TypeError(
        `Konfiguracja uwierzytelniania urządzenia ${deviceId} jest nieprawidłowa.`,
      );
    }
    devices.set(deviceId, {
      secret: decodeDeviceSecret(rawDevice.hmacSecret),
      viewerTokenSha256: rawDevice.viewerTokenSha256,
    });
  }
  if (devices.size === 0) {
    throw new TypeError('Gateway wymaga przynajmniej jednego urządzenia.');
  }
  const host = configuration.host ?? '127.0.0.1';
  const port = Number(configuration.port ?? 8787);
  const maximumClockSkewSeconds = Number(
    configuration.maximumClockSkewSeconds ?? 300,
  );
  if (typeof host !== 'string' || host.length < 1 || host.length > 255) {
    throw new TypeError('Adres nasłuchu gateway jest nieprawidłowy.');
  }
  if (!Number.isSafeInteger(port) || port < 0 || port > 65535) {
    throw new TypeError('Port gateway musi być liczbą od 0 do 65535.');
  }
  if (
    !Number.isSafeInteger(maximumClockSkewSeconds)
    || maximumClockSkewSeconds < 30
    || maximumClockSkewSeconds > 3600
  ) {
    throw new TypeError('Tolerancja zegara musi wynosić od 30 do 3600 sekund.');
  }
  if (
    typeof configuration.stateDirectory !== 'string'
    || configuration.stateDirectory.length === 0
  ) {
    throw new TypeError('Katalog stanu gateway jest wymagany.');
  }
  const origins = configuration.allowedOrigins ?? [];
  if (!Array.isArray(origins)) {
    throw new TypeError('Lista dozwolonych originów jest nieprawidłowa.');
  }
  const normalizedOrigins = new Set();
  for (const origin of origins) {
    let parsed;
    try {
      parsed = new URL(origin);
    } catch {
      throw new TypeError(`Nieprawidłowy dozwolony origin: ${origin}.`);
    }
    if (
      parsed.origin !== origin
      || !['https:', 'http:'].includes(parsed.protocol)
      || (
        parsed.protocol === 'http:'
        && !['127.0.0.1', 'localhost', '::1'].includes(parsed.hostname)
      )
    ) {
      throw new TypeError(
        `Origin ${origin} musi być kanonicznym HTTPS lub lokalnym adresem HTTP.`,
      );
    }
    normalizedOrigins.add(origin);
  }
  return {
    host,
    port,
    stateDirectory: configuration.stateDirectory,
    tls: configuration.tls ?? null,
    allowedOrigins: normalizedOrigins,
    maximumClockSkewSeconds,
    devices,
    firebaseServiceAccountPath:
      configuration.firebaseServiceAccountPath ?? '',
  };
}

export async function createGateway(configuration, dependencies = {}) {
  const config = normalizeConfiguration(configuration);
  const store = dependencies.store ?? new EventStore(config.stateDirectory);
  await store.initialize(config.devices.keys());
  const push = dependencies.push ?? new FirebasePushClient({
    serviceAccountPath: config.firebaseServiceAccountPath,
  });
  await push.initialize();
  const nonces = new NonceCache({
    lifetimeSeconds: config.maximumClockSkewSeconds * 2,
  });
  const rateLimit = new TokenBucket({ refillPerSecond: 1, burst: 30 });

  function applyCors(request, response) {
    const origin = request.headers.origin;
    if (typeof origin !== 'string') {
      return true;
    }
    if (!config.allowedOrigins.has(origin)) {
      return false;
    }
    response.setHeader('access-control-allow-origin', origin);
    response.setHeader('vary', 'origin');
    response.setHeader(
      'access-control-allow-headers',
      'authorization, content-type, x-aquacyd-nonce, x-aquacyd-signature, x-aquacyd-timestamp',
    );
    response.setHeader('access-control-allow-methods', ALLOWED_METHODS);
    response.setHeader('access-control-max-age', '600');
    return true;
  }

  async function handler(request, response) {
    securityHeaders(response);
    if (!applyCors(request, response)) {
      json(response, 403, { error: 'origin_not_allowed' });
      return;
    }
    if (request.method === 'OPTIONS') {
      response.writeHead(204);
      response.end();
      return;
    }
    const requestUrl = new URL(request.url, 'http://gateway.invalid');
    if (
      requestUrl.pathname === '/.well-known/aquacyd-gateway-pwa.json'
      && request.method === 'GET'
    ) {
      json(response, 200, {
        schemaVersion: 1,
        productId: 'aquacyd-https-gateway',
        mode: 'read-only-no-command-queue',
      });
      return;
    }
    if (requestUrl.pathname === '/healthz' && request.method === 'GET') {
      json(response, 200, {
        status: 'ok',
        service: 'aquacyd-secure-gateway',
        pushConfigured: push.configured,
      });
      return;
    }
    const match = DEVICE_ROUTE.exec(requestUrl.pathname);
    if (match === null) {
      json(response, 404, { error: 'not_found' });
      return;
    }
    const deviceId = match[1];
    const suffix = match[2] ?? '';
    const device = config.devices.get(deviceId);
    if (device === undefined) {
      json(response, 404, { error: 'unknown_device' });
      return;
    }

    if (suffix === '/health' && request.method === 'GET') {
      if (!verifyBearerToken(
        request.headers.authorization,
        device.viewerTokenSha256,
      )) {
        json(response, 401, { error: 'unauthorized' });
        return;
      }
      const latest = await store.latest(deviceId, 1);
      json(response, 200, {
        status: 'ok',
        deviceId,
        pushConfigured: push.configured,
        lastEventAt: latest[0]?.receivedAt ?? null,
      });
      return;
    }

    if (suffix === '/events' && request.method === 'GET') {
      if (!verifyBearerToken(
        request.headers.authorization,
        device.viewerTokenSha256,
      )) {
        json(response, 401, { error: 'unauthorized' });
        return;
      }
      const events = await store.latest(
        deviceId,
        requestUrl.searchParams.get('limit'),
      );
      json(response, 200, { schemaVersion: 1, deviceId, events });
      return;
    }

    if (suffix === '/events' && request.method === 'POST') {
      const body = await readBoundedBody(request);
      const nowSeconds = Math.floor(Date.now() / 1000);
      const verified = verifySignedRequest({
        secret: device.secret,
        timestampHeader: request.headers['x-aquacyd-timestamp'],
        nonce: request.headers['x-aquacyd-nonce'],
        signatureHeader: request.headers['x-aquacyd-signature'],
        method: request.method,
        pathname: requestUrl.pathname,
        body,
        nowSeconds,
        maximumClockSkewSeconds: config.maximumClockSkewSeconds,
      });
      if (!verified.ok) {
        json(response, 401, { error: verified.reason });
        return;
      }
      if (!nonces.consume(deviceId, verified.nonce, nowSeconds)) {
        json(response, 409, { error: 'replayed_nonce' });
        return;
      }
      // Nieautoryzowany klient nie może wyczerpać budżetu prawidłowo
      // podpisanych alarmów i zagłuszyć zdarzeń bezpieczeństwa.
      if (!rateLimit.consume(deviceId, Date.now())) {
        json(response, 429, { error: 'rate_limited' }, {
          'retry-after': '1',
        });
        return;
      }
      let parsed;
      try {
        parsed = JSON.parse(body.toString('utf8'));
      } catch {
        json(response, 400, { error: 'invalid_json' });
        return;
      }
      const receivedAt = new Date().toISOString();
      let event;
      try {
        event = validateEvent(parsed, deviceId, receivedAt);
      } catch (error) {
        json(response, 422, {
          error: 'invalid_event',
          message: error.message,
        });
        return;
      }
      const receiptId = randomUUID();
      const appendResult = await store.appendUnique(
        deviceId,
        { ...event, receiptId },
      );
      if (appendResult.duplicate) {
        json(response, 200, {
          accepted: true,
          duplicate: true,
          receiptId: appendResult.receiptId,
        });
        return;
      }
      let pushResult = { sent: 0, invalidTokens: [] };
      try {
        const tokens = await store.loadPushTokens(deviceId);
        pushResult = await push.send(tokens, event);
        for (const invalidToken of pushResult.invalidTokens) {
          await store.removePushToken(deviceId, invalidToken);
        }
      } catch (error) {
        console.error('FCM delivery failed:', error.message);
      }
      json(response, 202, {
        accepted: true,
        duplicate: false,
        receiptId,
        notificationsSent: pushResult.sent,
      });
      return;
    }

    if (suffix === '/push-tokens' && request.method === 'POST') {
      if (!verifyBearerToken(
        request.headers.authorization,
        device.viewerTokenSha256,
      )) {
        json(response, 401, { error: 'unauthorized' });
        return;
      }
      const body = await readBoundedBody(request);
      let token;
      try {
        token = JSON.parse(body.toString('utf8')).token;
        const registered = await store.addPushToken(deviceId, token);
        json(response, 200, { registered: true, tokenCount: registered });
      } catch (error) {
        json(response, 422, {
          error: 'invalid_push_token',
          message: error.message,
        });
      }
      return;
    }

    if (suffix === '/push-tokens' && request.method === 'DELETE') {
      if (!verifyBearerToken(
        request.headers.authorization,
        device.viewerTokenSha256,
      )) {
        json(response, 401, { error: 'unauthorized' });
        return;
      }
      const body = await readBoundedBody(request);
      try {
        const token = JSON.parse(body.toString('utf8')).token;
        const remaining = await store.removePushToken(deviceId, token);
        json(response, 200, { removed: true, tokenCount: remaining });
      } catch (error) {
        json(response, 422, {
          error: 'invalid_push_token',
          message: error.message,
        });
      }
      return;
    }

    json(response, 405, { error: 'method_not_allowed' }, {
      allow: ALLOWED_METHODS,
    });
  }

  const safeHandler = (request, response) => {
    Promise.resolve(handler(request, response)).catch((error) => {
      console.error('Gateway request failed:', error);
      if (!response.headersSent) {
        json(
          response,
          Number(error.statusCode) || 500,
          { error: 'internal_error' },
        );
      } else {
        response.destroy();
      }
    });
  };

  let server;
  if (config.tls?.certPath && config.tls?.keyPath) {
    server = https.createServer({
      cert: await readFile(config.tls.certPath),
      key: await readFile(config.tls.keyPath),
      minVersion: 'TLSv1.2',
    }, safeHandler);
  } else {
    server = http.createServer(safeHandler);
  }
  server.requestTimeout = 15_000;
  server.headersTimeout = 10_000;
  server.keepAliveTimeout = 5_000;
  server.maxRequestsPerSocket = 100;

  return {
    server,
    config,
    async listen() {
      await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(config.port, config.host, () => {
          server.off('error', reject);
          resolve();
        });
      });
      return server.address();
    },
    async close() {
      if (!server.listening) {
        return;
      }
      await new Promise((resolve, reject) => {
        server.close((error) => error ? reject(error) : resolve());
        server.closeIdleConnections();
      });
    },
  };
}
