'use strict';

(function initializeGatewayPwa(global) {
    const DATABASE_NAME = 'aquacyd-gateway-readonly';
    const DATABASE_VERSION = 1;
    const STORE_NAME = 'snapshots';
    const SNAPSHOT_KEY = 'latest-safe-status';
    const MAX_SNAPSHOT_BYTES = 256 * 1024;
    const GATEWAY_PROBE_PATH = '/.well-known/aquacyd-gateway-pwa.json';
    const GATEWAY_PROBE_TIMEOUT_MS = 3000;
    const GATEWAY_OPT_IN_KEY = 'aquacyd.gateway.pwa.opt-in.v1';
    const GATEWAY_OPT_IN_VALUE = 'read-only-no-command-queue';
    const MAX_SNAPSHOT_DEPTH = 8;
    const MAX_OBJECT_KEYS = 128;
    const MAX_TOTAL_KEYS = 512;
    const MAX_ARRAY_ITEMS = 256;
    const MAX_TOTAL_NODES = 2048;
    const MAX_STRING_LENGTH = 4096;
    const OMIT_VALUE = Symbol('omit-snapshot-value');
    const SENSITIVE_KEY_TOKENS = new Set([
        'password',
        'passwords',
        'pin',
        'pins',
        'token',
        'tokens',
        'secret',
        'secrets',
        'credential',
        'credentials',
        'session',
        'sessions',
        'authorization',
        'authorizations',
        'hmac'
    ]);
    const SENSITIVE_KEY_FRAGMENTS = [
        'password',
        'token',
        'secret',
        'credential',
        'session',
        'authorization',
        'hmac'
    ];
    const UNSAFE_OBJECT_KEYS = new Set(['__proto__', 'prototype', 'constructor']);
    const SAFE_STATUS_KEYS = new Set([
        'temperature',
        'sensors',
        'modules',
        'battery',
        'system',
        'relays',
        'lights',
        'schedule',
        'eco',
        'feeding',
        'clock',
        'alarms'
    ]);
    const secureCapable = global.location.protocol === 'https:' &&
        global.isSecureContext === true &&
        'serviceWorker' in global.navigator &&
        'indexedDB' in global;
    let enabled = false;

    function readPersistedGatewayOptIn() {
        try {
            return global.localStorage.getItem(GATEWAY_OPT_IN_KEY) ===
                GATEWAY_OPT_IN_VALUE;
        } catch {
            return false;
        }
    }

    function persistGatewayOptIn(value) {
        try {
            if (value) {
                global.localStorage.setItem(
                    GATEWAY_OPT_IN_KEY,
                    GATEWAY_OPT_IN_VALUE
                );
            } else {
                global.localStorage.removeItem(GATEWAY_OPT_IN_KEY);
            }
        } catch {
            return;
        }
    }

    async function verifyGatewayOptIn() {
        if (!secureCapable) {
            return false;
        }
        let responseReceived = false;
        const controller = new AbortController();
        const timeout = global.setTimeout(
            () => controller.abort(),
            GATEWAY_PROBE_TIMEOUT_MS
        );
        try {
            const response = await global.fetch(GATEWAY_PROBE_PATH, {
                cache: 'no-store',
                credentials: 'same-origin',
                headers: { Accept: 'application/json' },
                signal: controller.signal
            });
            responseReceived = true;
            if (!response.ok) {
                persistGatewayOptIn(false);
                return false;
            }
            const contentType = response.headers.get('content-type') || '';
            if (!contentType.toLowerCase().startsWith('application/json')) {
                persistGatewayOptIn(false);
                return false;
            }
            const payload = await response.json();
            enabled = payload?.schemaVersion === 1 &&
                payload?.productId === 'aquacyd-https-gateway' &&
                payload?.mode === 'read-only-no-command-queue';
            persistGatewayOptIn(enabled);
            return enabled;
        } catch (error) {
            enabled = responseReceived ? false : readPersistedGatewayOptIn();
            if (responseReceived) {
                persistGatewayOptIn(false);
            }
            if (error?.name !== 'AbortError') {
                console.warn(
                    enabled
                        ? 'Brama HTTPS jest poza siecią; używam wcześniej potwierdzonego trybu tylko do odczytu.'
                        : 'Brama HTTPS nie potwierdziła bezpiecznego trybu PWA.',
                    error
                );
            }
            return enabled;
        } finally {
            global.clearTimeout(timeout);
        }
    }

    const ready = verifyGatewayOptIn();

    function keyTokens(key) {
        return key
            .replace(/([a-z0-9])([A-Z])/g, '$1 $2')
            .toLowerCase()
            .split(/[^a-z0-9]+/)
            .filter(Boolean);
    }

    function isSensitiveKey(key) {
        if (typeof key !== 'string' || key.length === 0 || key.length > 128) {
            return true;
        }
        const normalized = key.toLowerCase();
        if (UNSAFE_OBJECT_KEYS.has(normalized)) {
            return true;
        }
        const compact = normalized.replace(/[^a-z0-9]/g, '');
        if (SENSITIVE_KEY_FRAGMENTS.some((fragment) => compact.includes(fragment))) {
            return true;
        }
        return keyTokens(key).some((token) => SENSITIVE_KEY_TOKENS.has(token));
    }

    function sanitizeValue(value, depth, budget) {
        if (budget.nodes >= MAX_TOTAL_NODES || depth > MAX_SNAPSHOT_DEPTH) {
            return OMIT_VALUE;
        }
        budget.nodes += 1;
        if (value === null || typeof value === 'boolean') {
            return value;
        }
        if (typeof value === 'number') {
            return Number.isFinite(value) ? value : OMIT_VALUE;
        }
        if (typeof value === 'string') {
            return value.length <= MAX_STRING_LENGTH ? value : OMIT_VALUE;
        }
        if (Array.isArray(value)) {
            const output = [];
            const limit = Math.min(value.length, MAX_ARRAY_ITEMS);
            for (let index = 0; index < limit; index += 1) {
                const sanitized = sanitizeValue(value[index], depth + 1, budget);
                if (sanitized !== OMIT_VALUE) {
                    output.push(sanitized);
                }
            }
            return output;
        }
        if (!value || typeof value !== 'object') {
            return OMIT_VALUE;
        }
        const prototype = Object.getPrototypeOf(value);
        if (prototype !== Object.prototype && prototype !== null) {
            return OMIT_VALUE;
        }
        const output = {};
        const keys = Object.keys(value).slice(0, MAX_OBJECT_KEYS);
        for (const key of keys) {
            if (budget.keys >= MAX_TOTAL_KEYS) {
                break;
            }
            budget.keys += 1;
            if (isSensitiveKey(key)) {
                continue;
            }
            const sanitized = sanitizeValue(value[key], depth + 1, budget);
            if (sanitized !== OMIT_VALUE) {
                output[key] = sanitized;
            }
        }
        return output;
    }

    function openDatabase() {
        if (!enabled) {
            return Promise.resolve(null);
        }
        return new Promise((resolve, reject) => {
            const request = global.indexedDB.open(DATABASE_NAME, DATABASE_VERSION);
            request.onupgradeneeded = () => {
                const database = request.result;
                if (!database.objectStoreNames.contains(STORE_NAME)) {
                    database.createObjectStore(STORE_NAME);
                }
            };
            request.onsuccess = () => resolve(request.result);
            request.onerror = () => reject(request.error || new Error('Nie można otworzyć magazynu offline.'));
            request.onblocked = () => reject(new Error('Magazyn offline jest zablokowany przez inną kartę.'));
        });
    }

    function sanitizeStatus(status) {
        if (!status || typeof status !== 'object' || Array.isArray(status)) {
            return null;
        }
        const safe = {};
        const budget = { keys: 0, nodes: 0 };
        for (const key of SAFE_STATUS_KEYS) {
            const value = status[key];
            if (value !== undefined) {
                const sanitized = sanitizeValue(value, 1, budget);
                if (sanitized !== OMIT_VALUE) {
                    safe[key] = sanitized;
                }
            }
        }
        if (Object.keys(safe).length === 0) {
            return null;
        }
        const encoded = JSON.stringify(safe);
        const encodedBytes = new TextEncoder().encode(encoded).byteLength;
        if (encodedBytes === 0 || encodedBytes > MAX_SNAPSHOT_BYTES) {
            return null;
        }
        return JSON.parse(encoded);
    }

    async function persistSnapshot(status) {
        if (!await ready) {
            return false;
        }
        const safeStatus = sanitizeStatus(status);
        if (!safeStatus) {
            return false;
        }
        const database = await openDatabase();
        if (!database) {
            return false;
        }
        try {
            await new Promise((resolve, reject) => {
                const transaction = database.transaction(STORE_NAME, 'readwrite');
                transaction.objectStore(STORE_NAME).put({
                    savedAt: Date.now(),
                    status: safeStatus
                }, SNAPSHOT_KEY);
                transaction.oncomplete = resolve;
                transaction.onerror = () => reject(
                    transaction.error || new Error('Nie można zapisać snapshotu offline.')
                );
                transaction.onabort = () => reject(
                    transaction.error || new Error('Zapis snapshotu offline został przerwany.')
                );
            });
            return true;
        } finally {
            database.close();
        }
    }

    async function restoreSnapshot() {
        if (!await ready) {
            return null;
        }
        const database = await openDatabase();
        if (!database) {
            return null;
        }
        try {
            return await new Promise((resolve, reject) => {
                const transaction = database.transaction(STORE_NAME, 'readonly');
                const request = transaction.objectStore(STORE_NAME).get(SNAPSHOT_KEY);
                request.onsuccess = () => {
                    try {
                        const record = request.result;
                        const safeStatus = record
                            ? sanitizeStatus(record.status)
                            : null;
                        if (!safeStatus) {
                            resolve(null);
                            return;
                        }
                        resolve({
                            savedAt: Number(record.savedAt) || 0,
                            status: safeStatus
                        });
                    } catch (error) {
                        reject(error);
                    }
                };
                request.onerror = () => reject(
                    request.error || new Error('Nie można odczytać snapshotu offline.')
                );
            });
        } finally {
            database.close();
        }
    }

    function exposeManifest() {
        if (global.document.querySelector('link[rel="manifest"]')) {
            return;
        }
        const link = global.document.createElement('link');
        link.rel = 'manifest';
        link.href = '/manifest.webmanifest?v=20260729gateway3';
        global.document.head.appendChild(link);
        const appleIcon = global.document.createElement('link');
        appleIcon.rel = 'apple-touch-icon';
        appleIcon.sizes = '192x192';
        appleIcon.href = '/aquacyd-icon-192.png?v=20260729gateway3';
        global.document.head.appendChild(appleIcon);
    }

    async function register() {
        if (!await ready) {
            return null;
        }
        exposeManifest();
        try {
            return await global.navigator.serviceWorker.register(
                '/service-worker.js?v=20260729gateway3',
                { scope: '/', updateViaCache: 'none' }
            );
        } catch (error) {
            console.warn('Nie udało się uruchomić trybu PWA bramy HTTPS.', error);
            return null;
        }
    }

    const api = {
        get enabled() {
            return enabled;
        },
        ready,
        persistSnapshot,
        restoreSnapshot,
        sanitizeSnapshot: sanitizeStatus,
        register
    };
    global.AquaCydGatewayPwa = Object.freeze(api);

    if (secureCapable) {
        global.addEventListener('load', () => {
            register().catch((error) => {
                console.warn('Nie udało się aktywować PWA bramy HTTPS.', error);
            });
        }, { once: true });
    }
})(window);
