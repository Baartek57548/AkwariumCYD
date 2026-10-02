/**
 * AquaCYD ESP32 CYD + WeMos D1 R3 (ESP8266) - Offline Simulator (Mock API)
 * Pełna symulacja backendu sterownika dla testowania w przeglądarce bez fizycznego sprzętu.
 */

(function () {
  'use strict';

  const isLocalFile = window.location.protocol === 'file:';
  const isLocalHost = window.location.hostname === 'localhost' || window.location.hostname === '127.0.0.1';
  const isSimulator = isLocalFile || isLocalHost || window.location.search.includes('mock=1');

  if (!isSimulator) {
    console.log('[AquaCYD API] Połączono z fizycznym mikrokontrolerem.');
    return;
  }

  console.info('[AquaCYD Mock API] Symulator offline aktywny (ESP32 CYD + WeMos D1 R3).');

  // Stan symulatora
  const mockState = {
    device: 'cydAkwarium',
    cyd: {
      online: true,
      display: 'TFT LCD ILI9341',
      brightness: 85,
      autoBrightness: true,
      profile: 'AlwaysOn'
    },
    wemos: {
      online: true,
      uart_baud: 115200,
      protocol: 'AquaProtocol',
      ota_mode: false,
      ota_ip: '192.168.1.121',
      mcp_present: true,
      ads_present: true,
      ds18b20_present: true
    },
    temperature: {
      current: 24.85,
      target: 25.0,
      hysteresis: 0.30,
      heaterMode: 0
    },
    ph: 6.85,
    ec: 450.0,
    ldr: 68,
    water_level_high: true,
    flow_active: true,
    supply_voltage: 5.04,
    relays: {
      light1: true,
      light2: true,
      light1_profile: 'sunny',
      light2_profile: 'sunny',
      pump: true,
      aeration: false,
      heater: false,
      co2: false,
      waterDosing: false
    },
    display: {
      brightness: 85,
      autoBrightness: true,
      profile: 'AlwaysOn',
      appliedBrightness: 85
    },
    alarms: {
      flags: 0,
      activeCount: 0,
      temperatureHigh: false,
      temperatureLow: false,
      phOutOfRange: false,
      waterLevelLow: false,
      supplyLow: false,
      sensorMissing: false,
      sensorStale: false,
      sensorBusFault: false,
      actuatorWriteFailed: false
    },
    network: {
      staConnected: true,
      apMode: false,
      staSsid: 'Akwarium_Home',
      ip: '192.168.1.120',
      rssi: -58
    },
    system: {
      uptime: 1054320, // ~12d 4h 32m
      freeHeap: 192512,
      largestHeap: 114688,
      bootCount: 14,
      resetReason: 'Software Reset (OTA)',
      bootId: '0x8F3A21BC',
      firmware: 'v2.4.0 (Flash No-SD PROGMEM)'
    },
    history: []
  };

  // Generowanie realistycznej historii telemetrii dobowej (288 próbek co 5 minut)
  const nowMs = Date.now();
  for (let i = 288; i >= 0; i--) {
    const t = nowMs - (i * 300000);
    const hour = new Date(t).getHours() + (new Date(t).getMinutes() / 60);
    const dayProgress = (hour / 24) * Math.PI * 2;

    const temp = 24.6 + Math.sin(dayProgress - 1.5) * 0.4 + (Math.sin(i * 0.2) * 0.05);
    const ph = 6.82 + Math.cos(dayProgress) * 0.12 + (Math.sin(i * 0.1) * 0.02);
    const ec = 448 + Math.sin(dayProgress + 0.5) * 6 + (Math.sin(i * 0.15) * 2);
    const ldr = hour >= 8 && hour <= 20 ? Math.round(50 + Math.sin((hour - 8) / 12 * Math.PI) * 40) : 5;

    mockState.history.push({
      timestamp: t,
      temperature: parseFloat(temp.toFixed(2)),
      ph: parseFloat(ph.toFixed(2)),
      ec: parseFloat(ec.toFixed(1)),
      ldr: ldr
    });
  }

  // Okresowa symulacja lekkich wahań w czasie rzeczywistym
  setInterval(() => {
    mockState.system.uptime += 2;
    mockState.temperature.current += (Math.random() - 0.49) * 0.03;
    mockState.temperature.current = parseFloat(mockState.temperature.current.toFixed(2));

    const lowThresh = mockState.temperature.target - mockState.temperature.hysteresis;
    if (mockState.temperature.current < lowThresh) {
      mockState.relays.heater = true;
    } else if (mockState.temperature.current >= mockState.temperature.target) {
      mockState.relays.heater = false;
    }

    if (mockState.history.length > 288) {
      mockState.history.shift();
    }
    mockState.history.push({
      timestamp: Date.now(),
      temperature: mockState.temperature.current,
      ph: mockState.ph,
      ec: mockState.ec,
      ldr: mockState.ldr
    });
  }, 2500);

  // Przechwytywanie globalnego `fetch`
  const originalFetch = window.fetch;
  window.fetch = async function (resource, init = {}) {
    const url = typeof resource === 'string' ? resource : resource.url;
    const method = (init.method || 'GET').toUpperCase();

    // 1. GET /api/status
    if (url.startsWith('/api/status')) {
      return new Response(JSON.stringify({
        status: 'ok',
        ...mockState
      }), {
        status: 200,
        headers: { 'Content-Type': 'application/json' }
      });
    }

    // 2. GET /api/bus-diagnostics
    if (url.startsWith('/api/bus-diagnostics')) {
      return new Response(JSON.stringify({
        status: 'ok',
        scanTimeMs: 12,
        uart: {
          device: 'WeMos D1 R3 (ESP8266)',
          baud: 115200,
          status: 'CONNECTED',
          last_rx_ms: 120
        },
        i2c: [
          { addr: '0x20', name: 'MCP23017 Relay Expander', status: 'OK' },
          { addr: '0x48', name: 'ADS1115 ADC Converter', status: 'OK' },
          { addr: '0x68', name: 'DS3231 High-Precision RTC', status: 'OK' }
        ],
        onewire: [
          { rom: '28-AA-3F-91-04-00-00', type: 'DS18B20', pin: 'D5 (GPIO14)', status: 'OK', temp: mockState.temperature.current }
        ]
      }), {
        status: 200,
        headers: { 'Content-Type': 'application/json' }
      });
    }

    // 3. POST /api/action
    if (url.startsWith('/api/action') && method === 'POST') {
      let body = {};
      try {
        body = JSON.parse(init.body || '{}');
      } catch (e) {}

      const action = body.action;

      if (action === 'set_light') {
        mockState.relays.light1 = body.state === 'on' || (body.state === 'auto' ? true : false);
        return new Response(JSON.stringify({ status: 'ok', action, light1: mockState.relays.light1 }));
      }

      if (action === 'set_light_profile') {
        mockState.relays.light1_profile = body.profile || 'sunny';
        return new Response(JSON.stringify({ status: 'ok', profile: mockState.relays.light1_profile }));
      }

      if (action === 'sync_aquael') {
        return new Response(JSON.stringify({ status: 'ok', message: 'Aquael synchronized' }));
      }

      if (action === 'set_plant') {
        mockState.relays.light2 = body.state === 'on' || (body.state === 'auto' ? true : false);
        return new Response(JSON.stringify({ status: 'ok', action, light2: mockState.relays.light2 }));
      }

      if (action === 'set_filter') {
        mockState.relays.pump = body.state === 'on';
        return new Response(JSON.stringify({ status: 'ok', action, pump: mockState.relays.pump }));
      }

      if (action === 'set_heater') {
        mockState.temperature.heaterMode = body.state === 'auto' ? 0 : 1;
        if (body.state === 'off') mockState.relays.heater = false;
        return new Response(JSON.stringify({ status: 'ok', action, heaterMode: mockState.temperature.heaterMode }));
      }

      if (action === 'save_co2') {
        mockState.relays.co2 = body.state === 'on';
        return new Response(JSON.stringify({ status: 'ok', action, co2: mockState.relays.co2 }));
      }

      if (action === 'test_ato') {
        if (body.state === 'off' || body.duration === 0) {
          mockState.relays.waterDosing = false;
          return new Response(JSON.stringify({ status: 'ok', message: 'ATO test stopped immediately' }));
        }
        mockState.relays.waterDosing = true;
        setTimeout(() => { mockState.relays.waterDosing = false; }, 10000);
        return new Response(JSON.stringify({ status: 'ok', message: 'ATO test initiated (10s)' }));
      }

      if (action === 'save_temperature') {
        if (body.target) mockState.temperature.target = parseFloat(body.target);
        if (body.hysteresis) mockState.temperature.hysteresis = parseFloat(body.hysteresis);
        return new Response(JSON.stringify({ status: 'ok', message: 'Temperature settings saved to NVS' }));
      }

      if (action === 'feed_now') {
        return new Response(JSON.stringify({ status: 'ok', message: 'Dispensing feed cycle complete' }));
      }

      if (action === 'test_relay') {
        const r = body.relay;
        return new Response(JSON.stringify({ status: 'ok', message: `Relay K${r} pulse executed (3s)` }));
      }

      if (action === 'clear_critical_logs') {
        mockState.alarms.activeCount = 0;
        mockState.alarms.temperatureHigh = false;
        mockState.alarms.temperatureLow = false;
        mockState.alarms.waterLevelLow = false;
        return new Response(JSON.stringify({ status: 'ok', message: 'Critical logs cleared' }));
      }

      if (action === 'save_schedule') {
        if (body.target_temp) mockState.temperature.target = body.target_temp;
        if (body.hysteresis) mockState.temperature.hysteresis = body.hysteresis;
        return new Response(JSON.stringify({ status: 'ok', message: 'Schedules saved to NVS' }));
      }

      if (action === 'save_network') {
        if (body.ssid) mockState.network.staSsid = body.ssid;
        return new Response(JSON.stringify({ status: 'ok', message: 'Network config updated' }));
      }

      if (action === 'save_display') {
        if (body.brightness) mockState.display.brightness = body.brightness;
        if (body.profile) mockState.display.profile = body.profile;
        return new Response(JSON.stringify({ status: 'ok', message: 'Display settings saved' }));
      }

      if (action === 'wemos_enter_ota') {
        mockState.wemos.ota_mode = true;
        return new Response(JSON.stringify({ status: 'ok', message: 'WeMos switched to OTA mode' }));
      }

      if (action === 'set_time') {
        return new Response(JSON.stringify({ status: 'ok', message: 'RTC clock updated' }));
      }

      if (action === 'restart_device') {
        return new Response(JSON.stringify({ status: 'ok', message: 'CYD restarting in 2 seconds' }));
      }

      if (action === 'factory_reset') {
        return new Response(JSON.stringify({ status: 'ok', message: 'NVS cleared. Rebooting to factory AP.' }));
      }

      return new Response(JSON.stringify({ status: 'ok', action: action || 'unknown' }));
    }

    // 4. GET /api/history.csv
    if (url.startsWith('/api/history.csv')) {
      let csv = 'Timestamp,DateTime,Temperature_C,pH,EC_uS,LDR_pct\n';
      mockState.history.forEach(pt => {
        csv += `${pt.timestamp},${new Date(pt.timestamp).toISOString()},${pt.temperature},${pt.ph},${pt.ec},${pt.ldr}\n`;
      });
      return new Response(csv, {
        status: 200,
        headers: {
          'Content-Type': 'text/csv',
          'Content-Disposition': 'attachment; filename="aquarium_history.csv"'
        }
      });
    }

    return originalFetch.apply(this, arguments);
  };

  // Przechwytywanie XMLHttpRequest dla testów OTA
  const OriginalXHR = window.XMLHttpRequest;
  window.XMLHttpRequest = function () {
    const xhr = new OriginalXHR();
    const origOpen = xhr.open;
    const origSend = xhr.send;

    let requestUrl = '';
    xhr.open = function (method, url) {
      requestUrl = url;
      return origOpen.apply(this, arguments);
    };

    xhr.send = function (data) {
      if (requestUrl.includes('/update')) {
        let progress = 0;
        const total = 524288;
        const timer = setInterval(() => {
          progress += 65536;
          if (progress < total) {
            if (xhr.upload && xhr.upload.onprogress) {
              xhr.upload.onprogress({
                lengthComputable: true,
                loaded: progress,
                total: total
              });
            }
          } else {
            clearInterval(timer);
            if (xhr.upload && xhr.upload.onprogress) {
              xhr.upload.onprogress({
                lengthComputable: true,
                loaded: total,
                total: total
              });
            }
            Object.defineProperty(xhr, 'status', { value: 200, writable: false });
            Object.defineProperty(xhr, 'readyState', { value: 4, writable: false });
            if (xhr.onload) xhr.onload();
          }
        }, 150);
        return;
      }
      return origSend.apply(this, arguments);
    };

    return xhr;
  };

})();
