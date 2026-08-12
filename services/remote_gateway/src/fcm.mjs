import { createSign } from 'node:crypto';
import { readFile } from 'node:fs/promises';

function base64Url(value) {
  return Buffer.from(value)
    .toString('base64')
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/g, '');
}

function assertServiceAccount(value) {
  if (
    value === null
    || typeof value !== 'object'
    || typeof value.client_email !== 'string'
    || typeof value.private_key !== 'string'
    || typeof value.project_id !== 'string'
  ) {
    throw new TypeError('Plik konta usługi Firebase jest nieprawidłowy.');
  }
}

export class FirebasePushClient {
  constructor({ serviceAccountPath, fetchImplementation = globalThis.fetch }) {
    this.serviceAccountPath = serviceAccountPath;
    this.fetch = fetchImplementation;
    this.serviceAccount = null;
    this.accessToken = null;
    this.accessTokenExpiresAt = 0;
  }

  get configured() {
    return typeof this.serviceAccountPath === 'string'
      && this.serviceAccountPath.length > 0;
  }

  async initialize() {
    if (!this.configured) {
      return;
    }
    const parsed = JSON.parse(await readFile(this.serviceAccountPath, 'utf8'));
    assertServiceAccount(parsed);
    this.serviceAccount = parsed;
  }

  async send(tokens, event) {
    if (!this.configured || tokens.length === 0) {
      return { sent: 0, invalidTokens: [] };
    }
    const accessToken = await this.getAccessToken();
    const invalidTokens = [];
    let sent = 0;
    for (const token of tokens) {
      const response = await this.fetch(
        `https://fcm.googleapis.com/v1/projects/${encodeURIComponent(
          this.serviceAccount.project_id,
        )}/messages:send`,
        {
          method: 'POST',
          headers: {
            authorization: `Bearer ${accessToken}`,
            'content-type': 'application/json; charset=utf-8',
          },
          body: JSON.stringify({
            message: {
              token,
              data: {
                deviceId: event.deviceId,
                eventId: event.eventId,
                id: event.eventId,
                type: 'alarm',
                eventType: event.type,
                title: event.title,
                body: event.message,
                severity: event.severity,
                state: event.state,
                occurredAt: event.occurredAt,
              },
              android: {
                // To wiadomość data-only: wysoki priorytet pozwala systemowi
                // uruchomić handler, który dopiero tworzy lokalny alert.
                priority: 'high',
                ttl: '3600s',
              },
            },
          }),
          signal: AbortSignal.timeout(10_000),
        },
      );
      if (response.ok) {
        sent += 1;
        continue;
      }
      const errorText = await response.text();
      if (
        response.status === 404
        || errorText.includes('UNREGISTERED')
        || errorText.includes('INVALID_ARGUMENT')
      ) {
        invalidTokens.push(token);
        continue;
      }
      throw new Error(
        `FCM odrzucił powiadomienie: HTTP ${response.status}.`,
      );
    }
    return { sent, invalidTokens };
  }

  async getAccessToken() {
    const now = Math.floor(Date.now() / 1000);
    if (
      this.accessToken !== null
      && this.accessTokenExpiresAt - now > 120
    ) {
      return this.accessToken;
    }
    if (this.serviceAccount === null) {
      await this.initialize();
    }
    const header = base64Url(JSON.stringify({ alg: 'RS256', typ: 'JWT' }));
    const claims = base64Url(JSON.stringify({
      iss: this.serviceAccount.client_email,
      scope: 'https://www.googleapis.com/auth/firebase.messaging',
      aud: 'https://oauth2.googleapis.com/token',
      iat: now,
      exp: now + 3600,
    }));
    const unsigned = `${header}.${claims}`;
    const signer = createSign('RSA-SHA256');
    signer.update(unsigned);
    signer.end();
    const signature = signer
      .sign(this.serviceAccount.private_key, 'base64')
      .replace(/\+/g, '-')
      .replace(/\//g, '_')
      .replace(/=+$/g, '');
    const assertion = `${unsigned}.${signature}`;
    const response = await this.fetch('https://oauth2.googleapis.com/token', {
      method: 'POST',
      headers: { 'content-type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({
        grant_type: 'urn:ietf:params:oauth:grant-type:jwt-bearer',
        assertion,
      }),
      signal: AbortSignal.timeout(10_000),
    });
    if (!response.ok) {
      throw new Error(
        `Firebase OAuth odrzucił konto usługi: HTTP ${response.status}.`,
      );
    }
    const payload = await response.json();
    if (
      typeof payload.access_token !== 'string'
      || !Number.isFinite(payload.expires_in)
    ) {
      throw new Error('Firebase OAuth zwrócił niepełną odpowiedź.');
    }
    this.accessToken = payload.access_token;
    this.accessTokenExpiresAt = now + Number(payload.expires_in);
    return this.accessToken;
  }
}
