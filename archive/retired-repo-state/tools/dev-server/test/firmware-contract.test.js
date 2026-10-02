'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { test } = require('node:test');

const projectRoot = path.resolve(__dirname, '..', '..', '..');

function readProjectFile(relativePath) {
    return fs.readFileSync(path.join(projectRoot, relativePath), 'utf8');
}

test('HTTP responses are coalesced without stealing socket ownership', () => {
    const source = readProjectFile('firmware/cyd_controller/src/gui_app.cpp');
    const classMatch = source.match(/class FastWebServer[\s\S]*?\n};/);

    assert.ok(classMatch, 'FastWebServer definition must remain discoverable');
    assert.doesNotMatch(
        classMatch[0],
        /_currentClient\s*\.\s*(?:flush|stop)\s*\(/,
        'closing the active socket while streaming can block sendContent and starve the UI heartbeat'
    );
    assert.match(
        classMatch[0],
        /runtime_safety_heartbeat\([\s\S]*?RuntimeSafetyTask::Ui/,
        'socket writes must keep the UI supervisor heartbeat alive'
    );
    assert.match(
        classMatch[0],
        /CHUNK_BUFFER_BYTES\s*=\s*8192U/,
        'the normal status response should be emitted as one buffered chunk'
    );
    assert.match(
        classMatch[0],
        /setContentLength\(response_length\)/,
        'a buffered response should avoid repeated chunk framing writes'
    );
    assert.match(
        classMatch[0],
        /void send\(int code, const char \*content_type, const char \*content\)/,
        'literal empty bodies must enter the deferred Content-Length path'
    );
    assert.match(
        classMatch[0],
        /if \(!_currentClient\.connected\(\)\)\s*\{\s*chunk_len = 0U;/,
        'a disconnected browser must be discarded before a body write'
    );
    assert.match(
        classMatch[0],
        /WebServer::sendContent\("",\s*0U\)/,
        'an oversized chunked response must retain WebServer termination semantics'
    );
    assert.match(
        classMatch[0],
        /SOCKET_SEND_SLICE_BYTES\s*=\s*1024U/,
        'large responses must be divided into TCP writes small enough for lwIP'
    );
    assert.match(
        classMatch[0],
        /SOCKET_STALL_BUDGET_MS\s*=\s*1500U/,
        'a stalled socket must not block the UI loop indefinitely'
    );
    assert.match(
        classMatch[0],
        /while \(written_total < length\)[\s\S]*?::send\([\s\S]*?MSG_DONTWAIT\)/,
        'each slice must use a nonblocking socket write'
    );
    assert.match(
        classMatch[0],
        /if \(written_total != length\)\s*\{[\s\S]*?response_aborted_ = true;/,
        'a stalled or partial response must abort the remaining body'
    );
    assert.doesNotMatch(
        classMatch[0],
        /_currentClient\s*\.\s*(?:flush|stop)\s*\(/,
        'the response adapter must leave socket lifetime to WebServer'
    );
});

test('ESP-IDF diagnostics cannot corrupt the production UART transport', () => {
    const source = readProjectFile('firmware/cyd_controller/src/main.cpp');

    assert.match(source, /esp_log_set_vprintf\(discard_esp_idf_log\)/);
    assert.match(source, /esp_log_level_set\("\*",\s*ESP_LOG_NONE\)/);
    assert.match(
        source,
        /void setup\(\)\s*\{[\s\S]*?esp_log_set_vprintf[\s\S]*?WiFi\.mode/,
        'the log sink must be installed before the Wi-Fi and mDNS stacks start'
    );
});

test('startup splash keeps its permanent DMA reservation small', () => {
    const config = readProjectFile('firmware/cyd_controller/include/config.h');
    const display = readProjectFile('firmware/cyd_controller/src/hal_display.cpp');
    assert.match(config, /RGB565_STREAM_ROWS\s*=\s*8\s*;/);
    assert.match(display, /rgb565_stream_buffer\[[\s\S]*?RGB565_STREAM_ROWS\]/);
});

test('SD static files use the bounded FastWebServer transport', () => {
    const source = readProjectFile('firmware/cyd_controller/src/gui_app.cpp');
    const streamMatch = source.match(
        /static size_t ota_portal_stream_file[\s\S]*?static const char \*ota_portal_basename/
    );

    assert.ok(streamMatch, 'SD file streaming helper must remain discoverable');
    assert.match(streamMatch[0], /ota_http_server\.sendContent\(/);
    assert.match(streamMatch[0], /ota_http_server\.sendContent\("",\s*0U\)/);
    assert.doesNotMatch(
        streamMatch[0],
        /client\.write\(|malloc\(/,
        'SD files must not bypass bounded transport or allocate per request'
    );
});

test('portal lifecycle is serialized with request handling', () => {
    const source = readProjectFile('firmware/cyd_controller/src/gui_app.cpp');
    assert.match(source, /std::atomic<PortalMode> portal_requested_mode/);
    assert.match(
        source,
        /if \(!ota_http_server\.listening\(\)\)\s*\{[\s\S]*?ota_http_server\.stop\(\);\s*return false;/,
        'a failed bind or listen must not be reported as an active portal'
    );
    assert.match(
        source,
        /void gui_app_service_web_portal\(void\)[\s\S]*?reconcile_http_portal\(\);[\s\S]*?ota_http_server\.handleClient\(\);/,
        'listener transitions and client handling must run on the UI task'
    );
    assert.match(
        source,
        /static void stop_ota_portal\(\)\s*\{\s*portal_requested_mode\.store\(PortalMode::Off/,
        'the I/O task may request shutdown but must not close an active socket'
    );
    assert.match(
        source,
        /static void reconcile_http_portal\(\)[\s\S]*?WiFi\.status\(\) == WL_CONNECTED[\s\S]*?portal_requested_mode\.store\(PortalMode::Sta/,
        'the HTTP task must recover when the I/O task misses a connected transition'
    );
    assert.match(
        source,
        /void gui_app_service_web_portal\(void\)[\s\S]*?ota_http_server\.listening\(\)[\s\S]*?WWW: HTTP aktywny \| RAM %lu KB/,
        'the device must advertise HTTP only after the listener starts'
    );
    assert.match(
        source,
        /if \(!ota_http_server\.listening\(\)\)\s*\{\s*portal_last_listen_errno = errno;/,
        'the device must preserve the socket error before closing a failed listener'
    );
});

test('Wi-Fi profile filesystem work uses the SD mutex', () => {
    const source = readProjectFile('firmware/cyd_controller/src/gui_app.cpp');
    const save = source.match(/static bool save_wifi_profile_to_sd\([\s\S]*?static bool load_wifi_profile_file/);
    const load = source.match(/static void try_autoconnect_wifi_profile\(void\) \{[\s\S]*?static const char OTA_PORTAL_FALLBACK_INDEX/);
    assert.ok(save);
    assert.ok(load);
    assert.match(save[0], /SdMutexGuard sd_guard\(20U\)/);
    assert.match(load[0], /SdMutexGuard sd_guard\(20U\)/);
});

test('removed water probe remains absent while the MAX float is additive', () => {
    const activeSources = [
        'firmware/cyd_controller/src/gui_app.cpp',
        'firmware/shared/uart_protocol/include/uart_protocol.h',
        'web/app-core.js',
        'web/app-init.js',
        'web/dashboard.js',
        'web/index.html',
        'web/settings.js',
        'tools/dev-server/server.js',
        'tools/dev-server/simulator.js'
    ].map(readProjectFile).join('\n');

    for (const forbiddenToken of [
        'save_leak',
        'leak_detected',
        'leak_enabled',
        'settings-leak',
        'LeakAction',
        'EventType::Leak',
        'ALARM_LEAK'
    ]) {
        assert.equal(
            activeSources.includes(forbiddenToken),
            false,
            `obsolete contract token remains active: ${forbiddenToken}`
        );
    }

    assert.match(activeSources, /ALARM_RESERVED_INPUT/);
    assert.match(activeSources, /water_level_max_reached/);
    assert.match(activeSources, /water_level_max_valid/);
});

test('removed clients and radio transports stay absent', () => {
    const firmwareAndBuildConfig = [
        'platformio.ini',
        'firmware/cyd_controller/src/gui_app.cpp',
        'firmware/cyd_controller/include/gui_app.h'
    ].map(readProjectFile).join('\n');

    assert.doesNotMatch(
        firmwareAndBuildConfig,
        /\b(?:BLE|Bluetooth|NimBLE)\b/i,
        'firmware must not expose or link a Bluetooth transport'
    );
    assert.doesNotMatch(firmwareAndBuildConfig, /forget_ble_bonds|mobile_ble/i);
    assert.doesNotMatch(
        firmwareAndBuildConfig,
        /esp[-_ ]?now|AQUACYD_ESPNOW/i,
        'firmware must not expose or compile the removed radio link'
    );
    assert.equal(
        fs.existsSync(path.join(projectRoot, 'apps', 'aquacyd_service', 'pubspec.yaml')),
        false
    );
    assert.equal(
        fs.existsSync(path.join(projectRoot, 'apps', 'home_control', 'pubspec.yaml')),
        false
    );
});
