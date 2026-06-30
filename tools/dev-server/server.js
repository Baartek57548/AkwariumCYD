'use strict';

const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { URL, URLSearchParams } = require('node:url');

const { DevSimulator } = require('./simulator');

const DEFAULT_PORT = 8000;
const HOST = '127.0.0.1';
const ADMIN_PIN = '1234';
const MAX_BODY_BYTES = 64 * 1024;
const WEB_ROOT = path.resolve(__dirname, '..', '..', 'web');
const MIME_TYPES = Object.freeze({
    '.css': 'text/css; charset=utf-8',
    '.gif': 'image/gif',
    '.html': 'text/html; charset=utf-8',
    '.ico': 'image/x-icon',
    '.jpeg': 'image/jpeg',
    '.jpg': 'image/jpeg',
    '.js': 'application/javascript; charset=utf-8',
    '.json': 'application/json; charset=utf-8',
    '.png': 'image/png',
    '.svg': 'image/svg+xml; charset=utf-8',
    '.woff2': 'font/woff2'
});

function parsePort(argv = process.argv.slice(2)) {
    const index = argv.indexOf('--port');
    if (index < 0) return DEFAULT_PORT;
    const value = Number(argv[index + 1]);
    if (!Number.isInteger(value) || value < 1024 || value > 65535) {
        throw new Error('Parametr --port musi być liczbą z zakresu 1024-65535.');
    }
    return value;
}

function sendJson(response, status, payload) {
    const body = JSON.stringify(payload);
    response.writeHead(status, {
        'Content-Type': 'application/json; charset=utf-8',
        'Content-Length': Buffer.byteLength(body),
        'Cache-Control': 'no-store'
    });
    response.end(body);
}

function sendText(response, status, body, contentType = 'text/plain; charset=utf-8', headers = {}) {
    response.writeHead(status, {
        'Content-Type': contentType,
        'Content-Length': Buffer.byteLength(body),
        'Cache-Control': 'no-store',
        ...headers
    });
    response.end(body);
}

function readBody(request) {
    return new Promise((resolve, reject) => {
        let body = '';
        request.setEncoding('utf8');
        request.on('data', (chunk) => {
            body += chunk;
            if (Buffer.byteLength(body) > MAX_BODY_BYTES) {
                reject(Object.assign(new Error('request_too_large'), { status: 413 }));
                request.destroy();
            }
        });
        request.on('end', () => resolve(body));
        request.on('error', reject);
    });
}

function requirePin(params, response) {
    if (params.get('pin') === ADMIN_PIN) return true;
    sendJson(response, 403, { success: false, code: 'invalid_pin', message: 'Błędny PIN administratora.' });
    return false;
}

function createDevServer(options = {}) {
    const simulator = options.simulator || new DevSimulator();
    const sseClients = new Set();

    function broadcast(eventName, payload) {
        const frame = `event: ${eventName}\ndata: ${JSON.stringify(payload)}\n\n`;
        for (const client of sseClients) {
            try {
                client.write(frame);
            } catch (_) {
                sseClients.delete(client);
            }
        }
    }

    const updateTimer = setInterval(() => {
        const status = simulator.step();
        broadcast('status', status);
    }, 1000);
    updateTimer.unref();

    async function handleAction(request, response) {
        const params = new URLSearchParams(await readBody(request));
        const action = params.get('action') || '';
        if (action === 'auth_check') {
            if (params.get('pin') !== ADMIN_PIN) {
                sendJson(response, 403, { success: false, code: 'invalid_pin', message: 'Błędny PIN administratora.' });
                return;
            }
            sendJson(response, 200, { success: true, code: 'ok', message: 'Autoryzacja poprawna.' });
            return;
        }
        if (!requirePin(params, response)) return;

        const booleanValue = (name, fallback) => params.has(name) ? params.get(name) === '1' : fallback;
        const numberValue = (name, fallback, minimum, maximum) => {
            const value = Number(params.get(name));
            return Number.isFinite(value) ? Math.max(minimum, Math.min(maximum, value)) : fallback;
        };

        switch (action) {
        case 'feed_now': {
            const result = simulator.triggerFeed();
            sendJson(response, result.success ? 200 : 409, result);
            break;
        }
        case 'clear_critical_logs':
            simulator.logs.critical.length = 0;
            sendJson(response, 200, { success: true, code: 'ok', message: 'Usunięto logi krytyczne.' });
            break;
        case 'set_light':
            simulator.manualRelays.light = params.get('state') === '1';
            simulator.addLog(`Światło ustawiono ${simulator.manualRelays.light ? 'ON' : 'OFF'} z panelu.`);
            sendJson(response, 200, { success: true, code: 'ok' });
            break;
        case 'set_filter':
            simulator.manualRelays.pump = params.get('state') === '1';
            simulator.addLog(`Filtr ustawiono ${simulator.manualRelays.pump ? 'ON' : 'OFF'} z panelu.`);
            sendJson(response, 200, { success: true, code: 'ok' });
            break;
        case 'save_schedule':
            if (params.has('lightMode')) simulator.config.lightMode = numberValue('lightMode', 0, 0, 2);
            if (params.has('plantLightMode')) simulator.config.plantLightMode = numberValue('plantLightMode', 0, 0, 2);
            if (params.has('filterMode')) simulator.config.filterMode = numberValue('filterMode', 0, 0, 2);
            if (params.has('airMode') || params.has('aerationMode')) simulator.config.airMode = numberValue(params.has('airMode') ? 'airMode' : 'aerationMode', 0, 0, 2);
            if (params.has('heaterMode')) simulator.config.heaterMode = numberValue('heaterMode', 0, 0, 1);
            simulator.addLog('Zapisano harmonogram w symulatorze RAM.');
            sendJson(response, 200, { success: true, code: 'ok', message: 'Harmonogram zapisany w RAM.' });
            break;
        case 'save_temperature':
            simulator.config.heaterMode = numberValue('heaterMode', simulator.config.heaterMode, 0, 1);
            simulator.config.targetTemp = numberValue(params.has('target') ? 'target' : 'targetTemp', simulator.config.targetTemp, 18, 30);
            simulator.config.tempHysteresis = numberValue(params.has('hysteresis') ? 'hysteresis' : 'tempHyst', simulator.config.tempHysteresis, 0.1, 5);
            simulator.addLog('Zapisano ustawienia termostatu w RAM.');
            sendJson(response, 200, { success: true, code: 'ok', message: 'Termostat zapisany.' });
            break;
        case 'save_co2':
            simulator.config.modules.co2 = booleanValue('co2Enabled', simulator.config.modules.co2);
            simulator.config.co2TargetPh = numberValue('targetPh', simulator.config.co2TargetPh, 5, 8.5);
            simulator.config.co2MaxTimeMin = numberValue('co2Limit', simulator.config.co2MaxTimeMin, 1, 1440);
            sendJson(response, 200, { success: true, code: 'ok', message: 'CO2 zapisane w RAM.' });
            break;
        case 'save_water':
            simulator.config.modules.waterLevel = booleanValue('waterEnabled', simulator.config.modules.waterLevel);
            sendJson(response, 200, { success: true, code: 'ok', message: 'Dolewka zapisana w RAM.' });
            break;
        case 'save_leak':
            simulator.config.modules.leak = booleanValue('leakEnabled', simulator.config.modules.leak);
            sendJson(response, 200, { success: true, code: 'ok', message: 'Zabezpieczenie wycieku zapisane w RAM.' });
            break;
        case 'save_display':
            simulator.config.alwaysScreenOn = booleanValue('alwaysScreenOn', simulator.config.alwaysScreenOn);
            sendJson(response, 200, { success: true, code: 'ok', message: 'Ekran zapisany w RAM.' });
            break;
        case 'save_network':
            simulator.config.staSsid = String(params.get('staSsid') || simulator.config.staSsid).slice(0, 32);
            sendJson(response, 200, { success: true, code: 'ok', message: 'Profil WiFi zapisany w RAM symulatora.' });
            break;
        case 'save_relays':
        case 'test_relay':
            sendJson(response, 200, { success: true, code: 'ok', message: 'Operacja przekaźnika zasymulowana.' });
            break;
        case 'wifi_session_start':
            sendJson(response, 200, { success: true, code: 'ok', message: 'Sesja WiFi DEV aktywna.' });
            break;
        case 'wifi_session_stop':
            sendJson(response, 200, { success: true, code: 'ok', message: 'Sesja WiFi DEV zatrzymana.' });
            break;
        case 'sync_time_ntp':
            simulator.clockBase = Date.now();
            simulator.startedAt = Date.now();
            sendJson(response, 200, { success: true, code: 'ok', message: 'Czas DEV zsynchronizowany.' });
            break;
        case 'restart_device':
        case 'factory_reset':
            sendJson(response, 200, { success: true, code: 'simulated', message: 'Akcja urządzenia zasymulowana bez restartu procesu.' });
            break;
        default:
            sendJson(response, 400, { success: false, code: 'unknown_action', message: `Nieobsługiwana akcja: ${action || 'brak'}.` });
        }

        broadcast('status', simulator.step());
        broadcast('logs', simulator.logPayload());
    }

    function serveStatic(pathname, response) {
        let decoded;
        try {
            decoded = decodeURIComponent(pathname);
        } catch (_) {
            sendText(response, 400, 'Nieprawidłowa ścieżka.');
            return;
        }
        const relative = decoded === '/' ? 'index.html' : decoded.replace(/^\/+/, '');
        const target = path.resolve(WEB_ROOT, relative);
        if (target !== WEB_ROOT && !target.startsWith(`${WEB_ROOT}${path.sep}`)) {
            sendText(response, 403, 'Dostęp zabroniony.');
            return;
        }
        fs.stat(target, (error, stats) => {
            if (error || !stats.isFile()) {
                sendText(response, 404, 'Nie znaleziono pliku.');
                return;
            }
            response.writeHead(200, {
                'Content-Type': MIME_TYPES[path.extname(target).toLowerCase()] || 'application/octet-stream',
                'Content-Length': stats.size,
                'Cache-Control': 'no-store',
                'X-Content-Type-Options': 'nosniff'
            });
            const stream = fs.createReadStream(target);
            stream.on('error', () => response.destroy());
            stream.pipe(response);
        });
    }

    const server = http.createServer(async (request, response) => {
        try {
            const requestUrl = new URL(request.url, `http://${request.headers.host || `${HOST}:${DEFAULT_PORT}`}`);
            const pathname = requestUrl.pathname;
            if (request.method === 'GET' && pathname === '/api/status') {
                sendJson(response, 200, simulator.step());
                return;
            }
            if (request.method === 'GET' && pathname === '/api/events') {
                response.writeHead(200, {
                    'Content-Type': 'text/event-stream; charset=utf-8',
                    'Cache-Control': 'no-cache',
                    Connection: 'keep-alive'
                });
                response.write('event: ready\ndata: {}\n\n');
                response.write(`event: status\ndata: ${JSON.stringify(simulator.step())}\n\n`);
                sseClients.add(response);
                request.on('close', () => sseClients.delete(response));
                return;
            }
            if ((request.method === 'GET' || request.method === 'POST') && pathname === '/api/web-session') {
                const bodyParams = request.method === 'POST' ? new URLSearchParams(await readBody(request)) : new URLSearchParams();
                const sessionId = requestUrl.searchParams.get('sid') || bodyParams.get('sid') || '';
                const state = requestUrl.searchParams.get('state') || bodyParams.get('state') || 'active';
                const valid = ['close', 'closed', 'release', 'inactive'].includes(state)
                    ? simulator.closeWebSession(sessionId) || /^[A-Za-z0-9_-]{6,24}$/.test(sessionId)
                    : simulator.touchWebSession(sessionId);
                sendJson(response, valid ? 200 : 400, { ok: valid, activeClients: simulator.activeWebSessions(), timeoutMs: simulator.webSessionTimeoutMs });
                return;
            }
            if (request.method === 'GET' && pathname === '/api/logs') {
                if (!requirePin(requestUrl.searchParams, response)) return;
                const payload = simulator.logPayload();
                if (requestUrl.searchParams.get('format') === 'text') {
                    const type = requestUrl.searchParams.get('type') === 'critical' ? 'critical' : 'normal';
                    const text = payload[type].map((entry) => `${entry.ts}\t${entry.level}\t${entry.message}`).join('\n');
                    sendText(response, 200, `${text}\n`);
                } else {
                    sendJson(response, 200, payload);
                }
                return;
            }
            if (request.method === 'GET' && pathname === '/api/history.csv') {
                const header = 'epoch,temp_c,ph,ec,ldr,heap_bytes,heater_active\n';
                const rows = simulator.history.map((sample) => `${sample.epoch},${sample.value},${sample.ph},${sample.ec},${sample.ldr},${sample.heap},${sample.heater ? 1 : 0}`).join('\n');
                sendText(response, 200, `${header}${rows}\n`, 'text/csv; charset=utf-8', { 'Content-Disposition': 'attachment; filename="cydAkwarium-current-history.csv"' });
                return;
            }
            if (request.method === 'GET' && pathname === '/api/files') {
                sendJson(response, 200, { ok: true, dir: '/aq/data/history', files: [{ name: '2026-06.aqbin', path: '/aq/data/history/2026-06.aqbin', size: 4128 }] });
                return;
            }
            if (request.method === 'GET' && pathname === '/download') {
                sendText(response, 200, 'AQBIN-DEV-RAM\n', 'application/octet-stream', { 'Content-Disposition': 'attachment; filename="2026-06.aqbin"' });
                return;
            }
            if (request.method === 'POST' && pathname === '/api/action') {
                await handleAction(request, response);
                return;
            }
            if (request.method === 'POST' && pathname === '/settime') {
                const params = new URLSearchParams(await readBody(request));
                if (!requirePin(params, response)) return;
                const epoch = Number(params.get('epoch'));
                if (!Number.isInteger(epoch) || epoch < 1700000000 || epoch > 4102444800) {
                    sendText(response, 400, 'invalid_epoch');
                    return;
                }
                simulator.clockBase = epoch * 1000;
                simulator.startedAt = Date.now();
                sendText(response, 200, 'ok');
                return;
            }
            if (request.method === 'POST' && pathname === '/api/ota/stop') {
                sendJson(response, 200, { success: true, code: 'simulated', message: 'OTA DEV zatrzymane.' });
                return;
            }
            if (request.method !== 'GET' && request.method !== 'HEAD') {
                sendText(response, 405, 'Metoda niedozwolona.', 'text/plain; charset=utf-8', { Allow: 'GET, HEAD, POST' });
                return;
            }
            serveStatic(pathname, response);
        } catch (error) {
            if (!response.headersSent) {
                sendJson(response, error.status || 500, { success: false, code: error.message || 'internal_error' });
            } else {
                response.destroy();
            }
        }
    });

    server.on('close', () => clearInterval(updateTimer));
    return { server, simulator };
}

if (require.main === module) {
    try {
        const port = parsePort();
        const { server } = createDevServer();
        server.listen(port, HOST, () => {
            process.stdout.write(`cydAkwarium DEV: http://${HOST}:${port}/\n`);
        });
    } catch (error) {
        process.stderr.write(`${error.message}\n`);
        process.exitCode = 1;
    }
}

module.exports = { ADMIN_PIN, createDevServer, parsePort };
