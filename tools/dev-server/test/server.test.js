'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
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

async function postAction(action, payload = {}) {
    return fetch(`${baseUrl}/api/action`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ action, pin: '1234', ...payload })
    });
}

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
    assert.deepEqual(status.lights.supportedProfiles, ['day', 'daybreak', 'night']);
    assert.equal(status.schedules.light.profileCycle, true);
    assert.equal(status.schedules.plant_light.profileCycle, true);
    assert.equal(status.relays.light1, status.relays.light);
    assert.equal(status.relays.light2, status.relays.plantLight);
});

test('canonical light1/light2 API configures both Aquael profiles independently', async () => {
    assert.equal((await postAction('set_light1', { state: '1' })).status, 200);
    assert.equal((await postAction('set_light2', { state: '1' })).status, 200);
    const response = await postAction('save_schedule', {
        light1Mode: '0', light1Start: '08:00', light1End: '20:00', light1Profile: 'daybreak', light1ProfileCycle: '0',
        light2Mode: '0', light2Start: '09:00', light2End: '21:00', light2Profile: 'night', light2ProfileCycle: '0'
    });
    assert.equal(response.status, 200);

    const status = await (await fetch(`${baseUrl}/api/status`)).json();
    assert.equal(status.schedules.light.profileCycle, false);
    assert.equal(status.schedules.light.profileName, 'DAYBREAK');
    assert.equal(status.schedules.plant_light.profileCycle, false);
    assert.equal(status.schedules.plant_light.profileName, 'NIGHT');
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

test('bus diagnostics require admin PIN and identify I2C, UART and OneWire', async () => {
    let response = await fetch(`${baseUrl}/api/bus-diagnostics`);
    assert.equal(response.status, 403);

    response = await fetch(`${baseUrl}/api/bus-diagnostics?pin=1234`);
    assert.equal(response.status, 200);
    const scan = await response.json();

    assert.equal(scan.ok, true);
    assert.equal(scan.simulated, true);
    assert.equal(scan.sda, 27);
    assert.equal(scan.scl, 22);
    assert.equal(scan.frequencyHz, 400000);
    assert.equal(scan.count, 2);
    assert.equal(scan.truncated, false);
    assert.deepEqual(scan.devices.map((device) => device.address), [0x20, 0x48]);
    assert.deepEqual(scan.devices.map((device) => device.type), ['mcp23017', 'ads1115']);
    assert.ok(scan.devices.every((device) => device.configured === true));
    assert.equal(scan.uart.discoverySupported, false);
    assert.deepEqual(scan.uart.ports[0], {
        port: 0,
        active: true,
        role: 'console',
        tx: 1,
        rx: 3,
        baud: 115200,
        format: '8N1'
    });
    assert.equal(scan.oneWire.dataPin, 17);
    assert.equal(scan.oneWire.count, 1);
    assert.equal(scan.oneWire.devices[0].type, 'ds18b20');
    assert.equal(scan.oneWire.devices[0].rom, '28-FF641D871603-5F');
    assert.equal(scan.oneWire.devices[0].crcValid, true);

    const legacyResponse = await fetch(`${baseUrl}/api/i2c-scan?pin=1234`);
    assert.equal(legacyResponse.status, 200);
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

    const displayResponse = await fetch(`${baseUrl}/api/action`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({
            action: 'save_display',
            pin: '1234',
            autoBrightness: '0',
            profile: 'timeout_60s',
            brightness: '65'
        })
    });
    assert.equal(displayResponse.status, 200);

    const status = await (await fetch(`${baseUrl}/api/status`)).json();
    assert.equal(status.temperature.target, 25.7);
    assert.equal(status.temperature.hysteresis, 0.6);
    assert.equal(status.display.autoBrightness, false);
    assert.equal(status.display.profile, 'timeout_60s');
    assert.equal(status.display.brightness, 65);
    assert.equal(status.web.sensorControlIntervalMs, 1000);
});

test('every panel action is accepted and reflected by status where applicable', async () => {
    const actions = [
        ['set_light', { state: '1' }],
        ['set_filter', { state: '1' }],
        ['set_plant', { state: '1' }],
        ['set_heater', { state: '1' }],
        ['set_aeration', { state: '1' }],
        ['save_schedule', {
            lightMode: '0', dayStart: '09:15', dayEnd: '21:45', lightProfile: 'daybreak',
            plantLightMode: '0', plantLightStart: '09:30', plantLightEnd: '20:15', plantLightProfile: 'day',
            filterMode: '0', filterOn: '09:00', filterOff: '22:00',
            aerationMode: '0', airOn: '08:30', airOff: '18:30',
            heaterMode: '0', feedFreq: '1', feedTime: '13:45'
        }],
        ['save_co2', { co2Enabled: '1', targetPh: '6.75', co2Limit: '240' }],
        ['save_water', { waterEnabled: '1', waterTimeout: '90' }],
        ['save_leak', { leakEnabled: '1', leakAction: 'disable_valves' }],
        ['save_display', { autoBrightness: '0', profile: 'timeout_60s', brightness: '72' }],
        ['save_network', { staSsid: 'AkwariumTest', staPassword: 'testowe123' }],
        ['test_relay', { channel: '8', state: '1', duration: '3' }],
        ['save_relays', { data: JSON.stringify({ relayBoard: { channels: 8 }, relays: Array.from({ length: 8 }, (_, index) => ({ channel: index + 1 })) }) }],
        ['wifi_session_start', {}],
        ['wifi_session_stop', {}],
        ['sync_time_ntp', {}]
    ];

    for (const [action, payload] of actions) {
        const response = await postAction(action, payload);
        assert.equal(response.status, 200, `${action} returned ${response.status}: ${await response.text()}`);
    }

    const feedResponse = await postAction('feed_now');
    assert.equal(feedResponse.status, 200);
    assert.equal((await feedResponse.json()).success, true);

    const status = await (await fetch(`${baseUrl}/api/status`)).json();
    assert.equal(status.relays.light, true);
    assert.equal(status.relays.pump, true);
    assert.equal(status.schedule.dayStartHour, 9);
    assert.equal(status.schedule.dayStartMin, 15);
    assert.equal(status.schedule.dayEndHour, 21);
    assert.equal(status.schedule.dayEndMin, 45);
    assert.equal(status.schedule.plantStartHour, 9);
    assert.equal(status.schedule.filterEndHour, 22);
    assert.equal(status.schedule.airStartHour, 8);
    assert.equal(status.feeding.hour, 13);
    assert.equal(status.feeding.minute, 45);
    assert.equal(status.config.co2TargetPh, 6.75);
    assert.equal(status.config.co2MaxTimeMin, 240);
    assert.equal(status.water.timeoutSec, 90);
    assert.equal(status.leak.action, 'disable_valves');
    assert.equal(status.display.brightness, 72);
    assert.equal(status.network.configuredStaSsid, 'AkwariumTest');
    assert.equal(status.feeding.active, true);

    assert.equal((await postAction('clear_critical_logs')).status, 200);
    assert.equal((await postAction('restart_device')).status, 200);
    assert.equal((await postAction('factory_reset')).status, 200);
});

test('web action contract has matching firmware handlers', () => {
    const projectRoot = path.resolve(__dirname, '..', '..', '..');
    const firmwareSource = fs.readFileSync(path.join(projectRoot, 'src', 'gui_app.cpp'), 'utf8');
    const expectedActions = [
        'auth_check', 'clear_critical_logs', 'factory_reset', 'feed_now', 'restart_device',
        'save_co2', 'save_display', 'save_leak', 'save_network', 'save_relays', 'save_schedule',
        'save_temperature', 'save_water', 'set_aeration', 'set_filter', 'set_heater', 'set_light',
        'set_plant', 'sync_time_ntp', 'test_relay', 'wifi_session_start', 'wifi_session_stop'
    ];
    for (const action of expectedActions) {
        assert.match(firmwareSource, new RegExp(`action\\s*==\\s*"${action}"`), `Missing firmware handler: ${action}`);
    }
    for (const route of ['/api/status', '/api/logs', '/api/bus-diagnostics', '/api/i2c-scan', '/api/action', '/api/web-session', '/settime', '/api/history.csv', '/api/files', '/download', '/update']) {
        assert.ok(firmwareSource.includes(`"${route}"`), `Missing firmware route: ${route}`);
    }
});

test('time, archive and download endpoints implement the browser contract', async () => {
    const epoch = 1782928800;
    const timeResponse = await fetch(`${baseUrl}/settime`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ epoch: String(epoch), pin: '1234' })
    });
    assert.equal(timeResponse.status, 200);

    const filesResponse = await fetch(`${baseUrl}/api/files?dir=/aq/data/history`);
    assert.equal(filesResponse.status, 200);
    const files = await filesResponse.json();
    assert.equal(files.ok, true);
    assert.ok(files.files.length > 0);

    const downloadResponse = await fetch(`${baseUrl}/download?path=${encodeURIComponent(files.files[0].path)}`);
    assert.equal(downloadResponse.status, 200);
    assert.match(await downloadResponse.text(), /AQBIN-DEV-RAM/u);

    const otaPayload = Buffer.alloc(128, 0xA5);
    const otaResponse = await fetch(`${baseUrl}/update?pin=1234`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: otaPayload
    });
    assert.equal(otaResponse.status, 200);
    const otaResult = await otaResponse.json();
    assert.equal(otaResult.success, true);
    assert.equal(otaResult.bytes, otaPayload.length);
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
