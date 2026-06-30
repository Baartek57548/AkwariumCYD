'use strict';

const assert = require('node:assert/strict');
const { after, before, test } = require('node:test');

const { createDevServer } = require('../server');

let server;
let baseUrl;

before(async () => {
    const created = createDevServer();
    server = created.server;
    await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(0, '127.0.0.1', resolve);
    });
    baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
    if (!server) return;
    await new Promise((resolve) => server.close(resolve));
});

test('status exposes complete finite DEV telemetry', async () => {
    const response = await fetch(`${baseUrl}/api/status`);
    assert.equal(response.status, 200);
    const status = await response.json();

    assert.equal(status.device, 'cydAkwarium');
    assert.equal(status.config.dev_mode, true);
    assert.equal(status.sensors.temp_valid, true);
    assert.equal(status.sensors.ph_valid, true);
    assert.equal(status.sensors.ec_valid, true);
    assert.equal(status.sensors.ldr_valid, true);
    assert.equal(status.sensors.mcp_valid, true);
    assert.ok(Number.isFinite(status.sensors.temp_c));
    assert.ok(Number.isFinite(status.sensors.ph));
    assert.ok(Number.isFinite(status.sensors.ec));
    assert.ok(Number.isFinite(status.battery.voltage));
    assert.ok(Array.isArray(status.temperature.history));
    assert.equal(status.temperature.history.length, 32);
    assert.equal(status.schedule.lightProfileName, 'DAY');
});

test('web session heartbeat tracks and releases clients', async () => {
    let response = await fetch(`${baseUrl}/api/web-session?sid=test_client_01&state=active`);
    assert.equal(response.status, 200);
    let payload = await response.json();
    assert.equal(payload.activeClients, 1);

    response = await fetch(`${baseUrl}/api/web-session?sid=test_client_01&state=close`);
    assert.equal(response.status, 200);
    payload = await response.json();
    assert.equal(payload.activeClients, 0);
});

test('admin authentication rejects invalid PIN and accepts valid PIN', async () => {
    let response = await fetch(`${baseUrl}/api/action`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ action: 'auth_check', pin: '9999' })
    });
    assert.equal(response.status, 403);

    response = await fetch(`${baseUrl}/api/action`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ action: 'auth_check', pin: '1234' })
    });
    assert.equal(response.status, 200);
    assert.equal((await response.json()).success, true);
});

test('settings actions update RAM state and remain API-compatible', async () => {
    const response = await fetch(`${baseUrl}/api/action`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({
            action: 'save_temperature',
            pin: '1234',
            heaterMode: '0',
            target: '25.7',
            hysteresis: '0.6'
        })
    });
    assert.equal(response.status, 200);

    const status = await (await fetch(`${baseUrl}/api/status`)).json();
    assert.equal(status.temperature.target, 25.7);
    assert.equal(status.temperature.hysteresis, 0.6);
});

test('logs require admin PIN and preserve UTF-8', async () => {
    let response = await fetch(`${baseUrl}/api/logs`);
    assert.equal(response.status, 403);

    response = await fetch(`${baseUrl}/api/logs?pin=1234`);
    assert.equal(response.status, 200);
    const logs = await response.json();
    assert.ok(logs.normal.length >= 3);
    assert.match(logs.normal[0].message, /DEV|RAM|Zapisano/u);
});

test('history CSV contains all simulated sensor columns', async () => {
    const response = await fetch(`${baseUrl}/api/history.csv`);
    assert.equal(response.status, 200);
    const csv = await response.text();
    assert.match(csv, /^epoch,temp_c,ph,ec,ldr,heap_bytes,heater_active/m);
    assert.ok(csv.trim().split('\n').length >= 33);
});

test('static server rejects traversal and serves the panel', async () => {
    const page = await fetch(`${baseUrl}/`);
    assert.equal(page.status, 200);
    assert.match(await page.text(), /cydAkwarium/u);

    const traversal = await fetch(`${baseUrl}/..%2Fplatformio.ini`);
    assert.equal(traversal.status, 403);
});
