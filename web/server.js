const http = require('http');
const fs = require('fs');
const path = require('path');
const url = require('url');

const PORT = 8000;

// Simulated State
let state = {
    temperature: {
        current: 24.5,
        target: 25.0,
        hysteresis: 0.5,
        history: [],
        historyCapacity: 144
    },
    sensors: {
        ph: 6.82,
        ec: 450,
        ldr: 320,
        temp_c: 24.5,
        leak_detected: false,
        mcp_ok: true
    },
    modules: {
        light_on: false,
        plant_light_on: false,
        light1_on: false,
        light2_on: false,
        ph_sensor_enabled: true,
        ec_enabled: true,
        water_level_enabled: true,
        leak_enabled: true,
        flow_enabled: true
    },
    battery: {
        voltage: 3.25,
        percent: 94
    },
    network: {
        staConnected: true,
        apMode: false,
        staConnecting: false,
        serviceMode: false,
        serviceModePending: false,
        staSsid: "AkwariumWiFi_5G",
        configuredStaSsid: "AkwariumWiFi_5G",
        configuredApSsid: "cydAkwarium_AP",
        ssid: "AkwariumWiFi_5G",
        staLastConnectedEpoch: Math.floor(Date.now() / 1000) - 3600
    },
    schedule: {
        lightMode: 0, // 0 = auto, 1 = on, 2 = off
        dayStartHour: 10,
        dayStartMin: 0,
        dayEndHour: 21,
        dayEndMin: 0,
        lightProfile: 0,
        lightProfileName: 'DAY',
        plantLightMode: 0,
        plantStartHour: 10,
        plantStartMin: 0,
        plantEndHour: 22,
        plantEndMin: 0,
        plantLightProfile: 0,
        plantLightProfileName: 'DAY',
        filterMode: 0,
        airMode: 0,
        heaterMode: 0
    },
    relays: {
        light: false,
        light1: false,
        pump: true,
        plantLight: false,
        light2: false,
        heater: false,
        aeration: true
    },
    system: {
        uptime: 3600,
        powerMode: "normal",
        mcpConnected: true,
        i2cConnected: true
    },
    firmware: {
        version: "1.2.4-stable",
        buildDate: "Jun 22 2026",
        buildTime: "22:15:30"
    },
    eco: {
        enabled: false,
        blockers: []
    },
    feeding: {
        active: false,
        lastFeedEpoch: Math.floor(Date.now() / 1000) - 18000,
        lastResult: "ok"
    },
    clock: {
        year: 2026,
        month: 6,
        day: 23,
        hour: 10,
        minute: 55,
        second: 0
    }
};

// Generate some initial temperature history
const nowSec = Math.floor(Date.now() / 1000);
for (let i = 0; i < 20; i++) {
    const time = nowSec - (20 - i) * 600; // 10 mins apart
    const val = 24.0 + Math.sin(i / 3) * 0.8 + Math.random() * 0.1;
    state.temperature.history.push({
        t: val,
        h: val < 24.5, // heater active
        e: time
    });
}

// Simulated Logs
let logs = {
    normal: [
        { ts: Math.floor(Date.now() / 1000) - 3600, level: "info", code: "wifi", message: "Połączono z siecią AkwariumWiFi_5G (IP: 192.168.1.144)" },
        { ts: Math.floor(Date.now() / 1000) - 3550, level: "info", code: "sys", message: "Inicjalizacja MCP23017 zakończona pomyślnie" },
        { ts: Math.floor(Date.now() / 1000) - 3500, level: "info", code: "sys", message: "Zegar RTC zsynchronizowany z NTP" },
        { ts: Math.floor(Date.now() / 1000) - 1800, level: "info", code: "feed", message: "Uruchomiono automatyczne karmienie" }
    ],
    critical: [
        { ts: Math.floor(Date.now() / 1000) - 7200, level: "warning", code: "battery", message: "Niskie napięcie baterii podtrzymującej RTC (2.85V)" }
    ],
    counts: { normal: 4, critical: 1 }
};

// Active SSE Clients
let sseClients = [];

function broadcastSSE(event, data) {
    sseClients.forEach(client => {
        client.write(`event: ${event}\n`);
        client.write(`data: ${JSON.stringify(data)}\n\n`);
    });
}

// Periodically update sensor data to look alive
setInterval(() => {
    // Oscillate temperature around target
    const target = state.temperature.target;
    let current = state.temperature.current;
    
    // Simulate heater warming or natural cooling
    if (state.relays.heater) {
        current += 0.05 + Math.random() * 0.02;
    } else {
        current -= 0.02 + Math.random() * 0.01;
    }
    
    // Auto thermostat simulation
    if (state.schedule.heaterMode === 0) { // Auto
        if (current <= target - state.temperature.hysteresis) {
            state.relays.heater = true;
        } else if (current >= target) {
            state.relays.heater = false;
        }
    }
    
    state.temperature.current = parseFloat(current.toFixed(2));
    state.sensors.temp_c = state.temperature.current;

    // Ph oscillation
    state.sensors.ph = parseFloat((6.8 + Math.sin(Date.now() / 100000) * 0.15 + Math.random() * 0.02).toFixed(2));
    
    // EC oscillation
    state.sensors.ec = Math.round(450 + Math.sin(Date.now() / 150000) * 10 + Math.random() * 4);

    // LDR changes
    state.sensors.ldr = Math.round(300 + Math.sin(Date.now() / 50000) * 50);

    // Update uptime
    state.system.uptime += 1;
    
    // Update simulated clock
    const d = new Date();
    state.clock = {
        year: d.getFullYear(),
        month: d.getMonth() + 1,
        day: d.getDate(),
        hour: d.getHours(),
        minute: d.getMinutes(),
        second: d.getSeconds()
    };

    // Add temp reading to history if necessary
    const now = Math.floor(Date.now() / 1000);
    const lastHistory = state.temperature.history[state.temperature.history.length - 1];
    if (!lastHistory || now - lastHistory.e >= 60) { // record every minute in simulator
        state.temperature.history.push({
            t: state.temperature.current,
            h: state.relays.heater,
            e: now
        });
        if (state.temperature.history.length > state.temperature.historyCapacity) {
            state.temperature.history.shift();
        }
    }

    broadcastSSE('status', state);
}, 2000);

const mimeTypes = {
    '.html': 'text/html; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.js': 'application/javascript; charset=utf-8',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.gif': 'image/gif',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon'
};

const server = http.createServer((req, res) => {
    const parsedUrl = url.parse(req.url, true);
    const pathname = parsedUrl.pathname;

    // CORS Headers
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
        res.writeHead(200);
        res.end();
        return;
    }

    // Server Sent Events (SSE)
    if (pathname === '/api/events') {
        res.writeHead(200, {
            'Content-Type': 'text/event-stream',
            'Cache-Control': 'no-cache',
            'Connection': 'keep-alive'
        });

        // Send ready event
        res.write('event: ready\n');
        res.write('data: {}\n\n');

        // Send initial status and logs
        res.write(`event: status\n`);
        res.write(`data: ${JSON.stringify(state)}\n\n`);
        res.write(`event: logs\n`);
        res.write(`data: ${JSON.stringify(logs)}\n\n`);

        sseClients.push(res);

        req.on('close', () => {
            sseClients = sseClients.filter(c => c !== res);
        });
        return;
    }

    // API: GET /api/status
    if (pathname === '/api/status' && req.method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(state));
        return;
    }

    // API: GET /api/logs
    if (pathname === '/api/logs' && req.method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(logs));
        return;
    }

    // API: GET /api/history.csv
    if (pathname === '/api/history.csv' && req.method === 'GET') {
        let csv = 'epoch,temp_c,heater_active\n';
        state.temperature.history.forEach(item => {
            csv += `${item.e},${item.t},${item.h ? 1 : 0}\n`;
        });
        res.writeHead(200, { 'Content-Type': 'text/csv' });
        res.end(csv);
        return;
    }

    // API: GET /api/files
    if (pathname === '/api/files' && req.method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify([
            { name: "history.csv", type: "file", size: 4096 },
            { name: "config.cfg", type: "file", size: 512 }
        ]));
        return;
    }

    // API: POST /api/action
    if (pathname === '/api/action' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => {
            body += chunk.toString();
        });
        req.on('end', () => {
            const params = new URLSearchParams(body);
            const action = params.get('action');
            const pin = params.get('pin');

            console.log(`Action requested: ${action}, PIN provided: ${pin}`);

            // Pin check
            if (action === 'auth_check') {
                if (pin === '1234') {
                    res.writeHead(200, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ success: true, code: "ok", message: "Autoryzacja pomyślna." }));
                } else {
                    res.writeHead(403, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ success: false, code: "invalid_pin", message: "Błędny PIN administratora." }));
                }
                return;
            }

            // Other actions:
            if (action === 'feed_now') {
                state.feeding.active = true;
                state.feeding.lastFeedEpoch = Math.floor(Date.now() / 1000);
                broadcastSSE('status', state);

                logs.normal.unshift({
                    ts: Math.floor(Date.now() / 1000),
                    level: "info",
                    code: "feed",
                    message: "Karmienie ręczne uruchomione z panelu web"
                });
                logs.counts.normal += 1;
                broadcastSSE('logs', logs);

                // Simulate feeding sequence finish
                setTimeout(() => {
                    state.feeding.active = false;
                    state.feeding.lastResult = "ok";
                    broadcastSSE('status', state);
                }, 5000);

                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: true }));
                return;
            }

            if (action === 'clear_critical_logs') {
                logs.critical = [];
                logs.counts.critical = 0;
                broadcastSSE('logs', logs);
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: true }));
                return;
            }

            // Quick relay toggles
            if (action === 'set_light' || action === 'set_light1') {
                const val = params.get('state') === '1';
                state.relays.light = val;
                state.relays.light1 = val;
                state.modules.light_on = val;
                state.modules.light1_on = val;
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: true }));
                return;
            }
            if (action === 'set_plant' || action === 'set_light2') {
                const val = params.get('state') === '1';
                state.relays.plantLight = val;
                state.relays.light2 = val;
                state.modules.plant_light_on = val;
                state.modules.light2_on = val;
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: true }));
                return;
            }
            if (action === 'set_filter') {
                const val = params.get('state') === '1';
                state.relays.pump = val;
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: true }));
                return;
            }

            // General save schedule / config
            if (action === 'save_schedule') {
                // Update schedule state
                if (params.get('plantLightMode') !== null) {
                    state.relays.plantLight = params.get('plantLightMode') === '1';
                }
                if (params.get('aerationMode') !== null) {
                    state.relays.aeration = params.get('aerationMode') === '1';
                }
                if (params.get('heaterMode') !== null) {
                    state.schedule.heaterMode = parseInt(params.get('heaterMode'));
                }
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: true }));
                return;
            }

            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: true }));
        });
        return;
    }

    // Static Files Server
    let filePath = path.join(__dirname, pathname === '/' ? 'index.html' : pathname);
    
    // Normalize Windows/Unix path separators
    filePath = path.normalize(filePath);

    // Security check to prevent traversing outside __dirname
    if (!filePath.startsWith(__dirname)) {
        res.writeHead(403);
        res.end('Access Denied');
        return;
    }

    fs.stat(filePath, (err, stats) => {
        if (err || !stats.isFile()) {
            res.writeHead(404);
            res.end('File Not Found');
            return;
        }

        const ext = path.extname(filePath).toLowerCase();
        const contentType = mimeTypes[ext] || 'application/octet-stream';

        res.writeHead(200, { 'Content-Type': contentType });
        const stream = fs.createReadStream(filePath);
        stream.pipe(res);
    });
});

server.listen(PORT, () => {
    console.log(`Mock server running at http://localhost:${PORT}/`);
});
