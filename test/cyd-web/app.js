/* ==========================================================================
   AquaSync CYD 2.8" TFT Touch Interface JavaScript Controller
   ========================================================================== */

// API Endpoints (matching ESP32 web server paths)
const API_STATUS = '/api/status';
const API_ACTION = '/api/action';
const API_LOGS = '/api/logs';
const API_SETTIME = '/settime';

// Global State
let isSimulatorMode = false;
let isConnected = false;
let statusPollingTimer = null;
let clockUpdateTimer = null;
let currentTab = 'home';
let activeSubpage = null;

// Controller Data Cache (initial mock/default data for simulation)
let deviceData = {
    clock: {
        year: 2026, month: 5, day: 30,
        hour: 16, minute: 30, second: 0
    },
    temperature: {
        current: 24.8,
        target: 25.0,
        hysteresis: 0.5
    },
    battery: {
        voltage: 3.82,
        percent: 82
    },
    network: {
        staConnected: false,
        apMode: true,
        staConnecting: false,
        serviceMode: true,
        staSsid: "AquaNet_Local",
        ip: "192.168.4.1",
        rssi: -58,
        configuredStaSsid: "DomoweWiFi",
        configuredApSsid: "AquaSync_AP",
        clients: 1
    },
    relays: {
        light1: false,
        light2: false,
        pump: true,  // Filter
        heater: false,
        air: false
    },
    modules: {
        airEnabled: true,
        feedEnabled: true
    },
    lcd: {
        alwaysOn: true,
        ldrSensitivity: 50
    },
    schedule: {
        light1Mode: 0,
        light1ColorMode: 0,
        light1StartHour: 10, light1StartMin: 0,
        light1EndHour: 21, light1EndMin: 30,
        light2Mode: 0,
        light2ColorMode: 2,
        light2StartHour: 12, light2StartMin: 0,
        light2EndHour: 18, light2EndMin: 0,
        filterMode: 0,
        filterStartHour: 10, filterStartMin: 30,
        filterEndHour: 20, filterEndMin: 30,
        airMode: 0,
        airStartHour: 10, airStartMin: 0,
        airEndHour: 19, airEndMin: 0,
        heaterMode: 0
    },
    feeding: {
        freq: 1, // 1 = Codziennie, 2 = Co 2 dni, 3 = Co 3 dni, 0 = Off
        hour: 18,
        minute: 0,
        active: false,
        lastFeedEpoch: 0,
        lastResult: "ok"
    },
    system: {
        uptimeSeconds: 12450,
        resetReason: "Power On Reset (Vbat)",
        resetCount: 3,
        powerMode: "active",
        sleepBlockers: ["sta_active", "idle_window"],
        firmware: {
            version: "v1.4.2-cyd",
            buildDate: "May 30 2026",
            buildTime: "12:00:00"
        }
    }
};

// Simulated History for trend calculations
let simulatedTempHistory = [24.5, 24.6, 24.7, 24.7, 24.8];

// DOMContentLoaded Entrypoint
document.addEventListener('DOMContentLoaded', () => {
    initNavigation();
    initScreenSimulatorControls();
    initSystemEventHandlers();
    
    // Proactively check connection on boot
    checkBackendConnection().then(() => {
        startUpdateLoops();
    });
});

/* ==========================================================================
   1. NAVIGATION & LAYOUT CONTROLS
   ========================================================================== */

function initNavigation() {
    // Bottom Tab Bar buttons
    const tabButtons = document.querySelectorAll('.tab-btn');
    tabButtons.forEach(btn => {
        btn.addEventListener('click', () => {
            const viewId = btn.getAttribute('data-view');
            switchTab(viewId);
        });
    });
}

function switchTab(viewId) {
    if (!viewId) return;
    
    // Update active tab buttons
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.classList.toggle('active', btn.getAttribute('data-view') === viewId);
    });
    
    // Switch active view section
    document.querySelectorAll('.view-section').forEach(view => {
        view.classList.toggle('active', view.id === `view-${viewId}`);
    });
    
    currentTab = viewId;
    closeAllOverlays();
}

function closeAllOverlays() {
    document.querySelectorAll('.cyd-overlay').forEach(overlay => {
        overlay.classList.remove('active');
    });
    activeSubpage = null;
}

// Fullscreen Screen Simulator Frame Toggle
function initScreenSimulatorControls() {
    const btnFullscreen = document.getElementById('enter-fullscreen');
    const btnSimMode = document.getElementById('toggle-sim-mode');
    
    // Load persisted preference
    if (localStorage.getItem('cyd-native-view') === 'true') {
        document.body.classList.add('native-view');
    }
    
    btnFullscreen.addEventListener('click', () => {
        const isNative = document.body.classList.toggle('native-view');
        localStorage.setItem('cyd-native-view', isNative ? 'true' : 'false');
        
        // Try actual browser fullscreen API if possible
        if (isNative) {
            const el = document.documentElement;
            if (el.requestFullscreen) el.requestFullscreen().catch(() => {});
        } else {
            if (document.exitFullscreen) document.exitFullscreen().catch(() => {});
        }
    });

    // Check for keyboard Escape to leave native simulator view
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && document.body.classList.contains('native-view')) {
            document.body.classList.remove('native-view');
            localStorage.setItem('cyd-native-view', 'false');
        }
    });

    // Toggle between online fetch and offline simulated engine
    btnSimMode.addEventListener('click', () => {
        isSimulatorMode = !isSimulatorMode;
        btnSimMode.classList.toggle('sim-btn-active', isSimulatorMode);
        
        if (isSimulatorMode) {
            btnSimMode.innerHTML = `<span class="indicator-dot offline"></span> Tryb Symulatora`;
            updateRGBLED('blue');
            showFeedbackModal('progress', 'Włączono symulator', 'System działa w trybie bezprzewodowym.', 1500);
        } else {
            btnSimMode.innerHTML = `<span class="indicator-dot online"></span> Sprawdzam połączenie...`;
            checkBackendConnection().then(() => {
                if (isConnected) {
                    btnSimMode.innerHTML = `<span class="indicator-dot online"></span> Tryb Online`;
                    showFeedbackModal('success', 'Sterownik Online', 'Połączono ze sterownikiem ESP32.', 1500);
                } else {
                    isSimulatorMode = true;
                    btnSimMode.classList.add('sim-btn-active');
                    btnSimMode.innerHTML = `<span class="indicator-dot offline"></span> Tryb Symulatora`;
                    showFeedbackModal('error', 'Brak sterownika', 'Brak połączenia. Uruchomiono symulator.', 2000);
                }
            });
        }
        updateUi();
    });
}

/* ==========================================================================
   2. TIMERS & AUTOMATIC ENGINE LOOPS
   ========================================================================== */

function startUpdateLoops() {
    // 1. Clock timer (every second)
    if (clockUpdateTimer) clearInterval(clockUpdateTimer);
    clockUpdateTimer = setInterval(() => {
        tickDeviceClock();
        updateClockUi();
    }, 1000);

    // 2. Status Polling loop (every 3 seconds)
    if (statusPollingTimer) clearInterval(statusPollingTimer);
    statusPollingTimer = setInterval(() => {
        if (isSimulatorMode) {
            tickSimulatorEngine();
        } else {
            fetchStatusFromBackend();
        }
    }, 3000);

    // Trigger immediate updates
    tickDeviceClock();
    updateUi();
}

// Sync clock internally by adding 1 second
function tickDeviceClock() {
    let { hour, minute, second, day, month, year } = deviceData.clock;
    second++;
    if (second >= 60) {
        second = 0;
        minute++;
        if (minute >= 60) {
            minute = 0;
            hour++;
            if (hour >= 24) {
                hour = 0;
                // Simplified day tick
                day++;
                if (day > 30) { day = 1; month++; if (month > 12) { month = 1; year++; } }
            }
        }
    }
    deviceData.clock = { hour, minute, second, day, month, year };
}

// Simulates real-time aquarium variables drift & heater automation
function tickSimulatorEngine() {
    // Simulate minor temperature drift (-0.05 to +0.05 deg)
    let tempDiff = (Math.random() - 0.5) * 0.08;
    
    // Simulate heater activity impact
    if (deviceData.relays.heater) {
        tempDiff += 0.07; // Heater warms the water up
    } else {
        tempDiff -= 0.035; // Natural heat dissipation
    }
    
    deviceData.temperature.current = parseFloat((deviceData.temperature.current + tempDiff).toFixed(2));
    
    // Push temperature to history
    simulatedTempHistory.push(deviceData.temperature.current);
    if (simulatedTempHistory.length > 20) simulatedTempHistory.shift();

    // Heater automation check
    if (deviceData.schedule.heaterMode !== 1) { // Auto mode
        const target = deviceData.temperature.target;
        const hyst = deviceData.temperature.hysteresis;
        
        if (deviceData.temperature.current <= (target - hyst)) {
            deviceData.relays.heater = true;
            updateRGBLED('red');
        } else if (deviceData.temperature.current >= (target + hyst)) {
            deviceData.relays.heater = false;
            updateRGBLED('green');
        }
    }

    // Aeration active simulation (schedule matches time)
    if (deviceData.schedule.airMode === 0) {
        const timeNow = deviceData.clock.hour * 60 + deviceData.clock.minute;
        const airOn = deviceData.schedule.airStartHour * 60 + deviceData.schedule.airStartMin;
        const airOff = deviceData.schedule.airEndHour * 60 + deviceData.schedule.airEndMin;
        deviceData.relays.aerationPercent = (timeNow >= airOn && timeNow < airOff) ? 40 : 0;
    }

    // Light schedule simulation
    if (deviceData.schedule.lightMode === 0) {
        const timeNow = deviceData.clock.hour * 60 + deviceData.clock.minute;
        const lightOn = deviceData.schedule.dayStartHour * 60 + deviceData.schedule.dayStartMin;
        const lightOff = deviceData.schedule.dayEndHour * 60 + deviceData.schedule.dayEndMin;
        deviceData.relays.light = (timeNow >= lightOn && timeNow < lightOff);
    }

    // Filter schedule simulation
    if (deviceData.schedule.filterMode === 0) {
        const timeNow = deviceData.clock.hour * 60 + deviceData.clock.minute;
        const filterOn = deviceData.schedule.filterStartHour * 60 + deviceData.schedule.filterStartMin;
        const filterOff = deviceData.schedule.filterEndHour * 60 + deviceData.schedule.filterEndMin;
        deviceData.relays.pump = (timeNow >= filterOn && timeNow < filterOff);
    }

    // Battery voltage micro adjustments
    deviceData.battery.voltage = parseFloat((3.80 + Math.random() * 0.05).toFixed(2));
    deviceData.system.uptimeSeconds += 3;

    updateUi();
}

/* ==========================================================================
   3. API OPERATIONS & BACKEND COMMUNICATION
   ========================================================================== */

// Check if ESP32 server endpoints are reachable
async function checkBackendConnection() {
    try {
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 2000);
        
        const response = await fetch(API_STATUS, { 
            method: 'GET', 
            signal: controller.signal,
            cache: 'no-store'
        });
        clearTimeout(timeoutId);
        
        if (response.ok) {
            isConnected = true;
            isSimulatorMode = false;
            updateConnectionStatusDot(true);
            updateRGBLED('green');
            document.getElementById('overlay-disconnected').classList.remove('active');
            return true;
        }
    } catch (e) {
        // Failed to reach ESP32
    }
    
    isConnected = false;
    isSimulatorMode = true; // Auto-fallback to simulator
    updateConnectionStatusDot(false);
    updateRGBLED('blue');
    
    // Auto-show disconnected overlay if we expect to be connected
    // For now we just show a toast or stay in simulator mode
    const btnSimMode = document.getElementById('toggle-sim-mode');
    if (btnSimMode) {
        btnSimMode.classList.add('sim-btn-active');
        btnSimMode.innerHTML = `<span class="indicator-dot offline"></span> Tryb Symulatora`;
    }
    return false;
}

// Fetch status payload
async function fetchStatusFromBackend() {
    try {
        const response = await fetch(`${API_STATUS}?history=1`, { cache: 'no-store' });
        if (!response.ok) throw new Error();
        
        const payload = await response.json();
        
        // Map payload attributes
        deviceData.clock = payload.clock || deviceData.clock;
        deviceData.temperature = payload.temperature || deviceData.temperature;
        deviceData.battery = payload.battery || deviceData.battery;
        deviceData.network = payload.network || deviceData.network;
        deviceData.relays = payload.relays || deviceData.relays;
        deviceData.schedule = payload.schedule || deviceData.schedule;
        deviceData.feeding = payload.feeding || deviceData.feeding;
        deviceData.system = payload.system || deviceData.system;
        
        isConnected = true;
        updateConnectionStatusDot(true);
        updateUi();
    } catch (e) {
        // Connection dropped
        isConnected = false;
        isSimulatorMode = true;
        updateConnectionStatusDot(false);
        updateRGBLED('blue');
        
        showFeedbackModal('error', 'Utracono połączenie', 'Brak odpowiedzi. Włączono symulator.', 2500);
    }
}

// Send POST requests to ESP32 actions
async function postAction(action, payload = {}) {
    if (isSimulatorMode) {
        // Handle in-memory simulation actions
        return handleSimulatedAction(action, payload);
    }
    
    try {
        showFeedbackModal('progress', 'Wysyłanie...', 'Zapisywanie na sterowniku.');
        
        const params = new URLSearchParams({ action, ...payload });
        const response = await fetch(API_ACTION, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: params.toString()
        });
        
        if (!response.ok) throw new Error("action_failed");
        
        let result;
        const contentType = response.headers.get('content-type') || '';
        if (contentType.includes('application/json')) {
            result = await response.json();
        } else {
            result = { success: true };
        }
        
        showFeedbackModal('success', 'Zapisano', 'Ustawienia zsynchronizowane.', 1200);
        await fetchStatusFromBackend();
        return result;
    } catch (e) {
        showFeedbackModal('error', 'Błąd zapisu', 'Nie udało się wykonać polecenia.', 2000);
        throw e;
    }
}

/* ==========================================================================
   4. UI RENDERING & RENDERING UTILITIES
   ========================================================================== */

function updateUi() {
    updateHeaderUi();
    
    // Update active view layout
    if (currentTab === 'home') {
        renderHomeTab();
    } else if (currentTab === 'relays') {
        renderRelaysTab();
    } else if (currentTab === 'schedules') {
        renderSchedulesTab();
    } else if (currentTab === 'temp') {
        renderTempTab();
    } else if (currentTab === 'system') {
        renderSystemTab();
    } else if (currentTab === 'charts') {
        renderChartsTab();
    }
}

function updateHeaderUi() {
    // Current temperature tag
    const tempEl = document.getElementById('sb-temp');
    if (tempEl) {
        const val = deviceData.temperature.current;
        tempEl.textContent = val !== null ? `${val.toFixed(1)}°C` : '--.-°C';
    }
    
    // Wifi signal icon indicator
    const wifiEl = document.getElementById('sb-wifi-icon');
    if (wifiEl) {
        const isWifiActive = deviceData.network.staConnected || deviceData.network.apMode;
        wifiEl.style.opacity = isWifiActive ? '1' : '0.35';
        wifiEl.style.fill = deviceData.network.staConnected ? 'var(--accent-cyan)' : '#cbd5e1';
    }

    // Battery widget
    const voltsEl = document.getElementById('sb-battery-volts');
    const fillEl = document.getElementById('sb-battery-fill');
    if (voltsEl && fillEl) {
        voltsEl.textContent = `${deviceData.battery.voltage.toFixed(2)}V`;
        const pct = deviceData.battery.percent;
        fillEl.style.width = `${pct}%`;
        
        if (pct <= 20) {
            fillEl.style.backgroundColor = 'var(--danger-color)';
        } else if (pct <= 45) {
            fillEl.style.backgroundColor = 'var(--warning-color)';
        } else {
            fillEl.style.backgroundColor = 'var(--success-color)';
        }
    }
}

function updateClockUi() {
    const timeEl = document.getElementById('sb-time');
    if (timeEl) {
        const { hour, minute } = deviceData.clock;
        timeEl.textContent = `${padZero(hour)}:${padZero(minute)}`;
    }
}

// 4.1 Home Screen Render
function renderHomeTab() {
    const tempVal = document.getElementById('home-temp-current');
    const tempTarget = document.getElementById('home-temp-target');
    const trendIndicator = document.getElementById('home-temp-trend');
    const feedNext = document.getElementById('home-feed-next');
    
    if (tempVal) tempVal.textContent = deviceData.temperature.current !== null ? deviceData.temperature.current.toFixed(1) : '--.-';
    if (tempTarget) tempTarget.textContent = `${deviceData.temperature.target.toFixed(1)}°C`;
    
    // Calculate simulated or real trend
    if (trendIndicator) {
        let history = isSimulatorMode ? simulatedTempHistory : (deviceData.temperature.history || []);
        if (history.length >= 2) {
            const diff = history[history.length - 1] - history[history.length - 2];
            if (diff > 0.02) {
                trendIndicator.className = "trend-indicator trend-up";
                trendIndicator.textContent = "▲";
            } else if (diff < -0.02) {
                trendIndicator.className = "trend-indicator trend-down";
                trendIndicator.textContent = "▼";
            } else {
                trendIndicator.className = "trend-indicator trend-stable";
                trendIndicator.textContent = "●";
            }
        } else {
            trendIndicator.className = "trend-indicator";
            trendIndicator.textContent = "—";
        }
    }
    
    // Feeder next run schedule label
    if (feedNext) {
        if (!deviceData.modules.feedEnabled) {
            feedNext.parentElement.parentElement.style.display = 'none'; // hide feed action
        } else {
            feedNext.parentElement.parentElement.style.display = 'flex';
            if (deviceData.feeding.freq === 0) {
                feedNext.textContent = "OFF";
                feedNext.style.color = 'var(--text-muted)';
            } else {
                feedNext.textContent = `${padZero(deviceData.feeding.hour)}:${padZero(deviceData.feeding.minute)}`;
                feedNext.style.color = '#e2e8f0';
            }
        }
    }
    
    // Toggles states (Light 1 is main light in home)
    syncToggleWidget('light1', deviceData.relays.light1);
    syncToggleWidget('pump', deviceData.relays.pump);
    
    // Heater status in-home
    const heaterItem = document.getElementById('qs-heater');
    const heaterState = document.getElementById('qs-heater-state');
    const heaterInd = document.getElementById('ind-heater');
    if (heaterItem && heaterState && heaterInd) {
        const isActive = deviceData.relays.heater;
        heaterState.textContent = isActive ? "GRZEJE" : (deviceData.schedule.heaterMode === 1 ? "WYŁĄCZONY" : "CZEKA");
        heaterState.style.color = isActive ? 'var(--warning-color)' : 'var(--text-muted)';
        heaterInd.className = `qs-status-indicator ${isActive ? 'heating' : ''}`;
    }
}

function syncToggleWidget(relayKey, state) {
    const row = document.getElementById(`qs-${relayKey}`);
    const label = document.getElementById(`qs-${relayKey}-state`);
    const btn = document.getElementById(`btn-toggle-${relayKey}`);
    
    if (!row || !label || !btn) return;
    
    row.classList.toggle('state-on', state);
    
    row.classList.toggle('state-on', state);
    
    // Check mode
    let mode = 0;
    if (relayKey === 'light1') mode = deviceData.schedule.light1Mode;
    else if (relayKey === 'light2') mode = deviceData.schedule.light2Mode;
    else mode = deviceData.schedule.filterMode;
    
    if (mode === 1) {
        label.textContent = "ZAW. ON";
    } else if (mode === 2) {
        label.textContent = "ZAW. OFF";
    } else {
        label.textContent = state ? "AUTO: ON" : "AUTO: OFF";
    }
}

// 4.2 Relays Screen Render
function renderRelaysTab() {
    // Helper to render relay tile cards
    renderRelayTile('light1', deviceData.relays.light1, deviceData.schedule.light1Mode, 
        `${padZero(deviceData.schedule.light1StartHour)}:${padZero(deviceData.schedule.light1StartMin)} - ${padZero(deviceData.schedule.light1EndHour)}:${padZero(deviceData.schedule.light1EndMin)}`);
        
    renderRelayTile('light2', deviceData.relays.light2, deviceData.schedule.light2Mode, 
        `${padZero(deviceData.schedule.light2StartHour)}:${padZero(deviceData.schedule.light2StartMin)} - ${padZero(deviceData.schedule.light2EndHour)}:${padZero(deviceData.schedule.light2EndMin)}`);
        
    renderRelayTile('pump', deviceData.relays.pump, deviceData.schedule.filterMode, 
        `${padZero(deviceData.schedule.filterStartHour)}:${padZero(deviceData.schedule.filterStartMin)} - ${padZero(deviceData.schedule.filterEndHour)}:${padZero(deviceData.schedule.filterEndMin)}`);
        
    // Heater card
    const tileHeater = document.getElementById('tile-heater');
    const valHeaterTarget = document.getElementById('relay-heater-target');
    const valHeaterStatus = document.getElementById('relay-heater-status');
    const isHeaterActive = deviceData.relays.heater;
    
    if (tileHeater && valHeaterTarget && valHeaterStatus) {
        tileHeater.classList.toggle('state-active-on', isHeaterActive);
        valHeaterTarget.textContent = `Cel: ${deviceData.temperature.target.toFixed(1)}°C ±${deviceData.temperature.hysteresis.toFixed(1)}`;
        
        if (deviceData.schedule.heaterMode === 1) {
            valHeaterStatus.textContent = "OFF";
            valHeaterStatus.style.color = 'var(--text-muted)';
            valHeaterStatus.style.backgroundColor = '#1e293b';
        } else {
            valHeaterStatus.textContent = isHeaterActive ? "GRZEJE" : "CZEKA";
            valHeaterStatus.style.color = isHeaterActive ? '#0b0f19' : 'var(--text-muted)';
            valHeaterStatus.style.backgroundColor = isHeaterActive ? 'var(--warning-color)' : '#1e293b';
        }
    }
    
    // Aeration card
    const tileAir = document.getElementById('tile-air');
    const valAirStatus = document.getElementById('relay-air-status');
    const isAirActive = deviceData.relays.aerationPercent > 0;
    
    if (tileAir && valAirStatus) {
        tileAir.classList.toggle('state-active-on', isAirActive);
        valAirStatus.textContent = isAirActive ? `ON (${deviceData.relays.aerationPercent}%)` : "OFF";
        valAirStatus.style.backgroundColor = isAirActive ? 'var(--success-color)' : '#1e293b';
        valAirStatus.style.color = isAirActive ? '#0b0f19' : 'var(--text-muted)';
    }
}

function renderRelayTile(id, active, mode, rangeText) {
    const tile = document.getElementById(`tile-${id}`);
    const modeEl = document.getElementById(`relay-${id}-mode`);
    const rangeEl = document.getElementById(`relay-${id}-time`);
    
    if (!tile || !modeEl || !rangeEl) return;
    
    tile.classList.toggle('state-active-on', active);
    
    if (mode === 1) {
        modeEl.textContent = "ZAW. ON";
    } else if (mode === 2) {
        modeEl.textContent = "ZAW. OFF";
    } else {
        modeEl.textContent = "AUTO";
    }
    
    rangeEl.textContent = mode === 0 ? rangeText : "Sterowanie ręczne";
}

// 4.3 Schedules Screen Render
function renderSchedulesTab() {
    const metaLight1 = document.getElementById('sched-meta-light1');
    const metaLight2 = document.getElementById('sched-meta-light2');
    const metaPump = document.getElementById('sched-meta-pump');
    const metaAir = document.getElementById('sched-meta-air');
    const metaFeed = document.getElementById('sched-meta-feed');
    
    if (metaLight1) {
        metaLight1.textContent = deviceData.schedule.light1Mode === 0 
            ? `Harmonogram: ${padZero(deviceData.schedule.light1StartHour)}:${padZero(deviceData.schedule.light1StartMin)} - ${padZero(deviceData.schedule.light1EndHour)}:${padZero(deviceData.schedule.light1EndMin)}`
            : (deviceData.schedule.light1Mode === 1 ? "Zawsze włączone" : "Zawsze wyłączone");
    }
    
    if (metaLight2) {
        metaLight2.textContent = deviceData.schedule.light2Mode === 0 
            ? `Harmonogram: ${padZero(deviceData.schedule.light2StartHour)}:${padZero(deviceData.schedule.light2StartMin)} - ${padZero(deviceData.schedule.light2EndHour)}:${padZero(deviceData.schedule.light2EndMin)}`
            : (deviceData.schedule.light2Mode === 1 ? "Zawsze włączone" : "Zawsze wyłączone");
    }
    
    if (metaPump) {
        metaPump.textContent = deviceData.schedule.filterMode === 0 
            ? `Harmonogram: ${padZero(deviceData.schedule.filterStartHour)}:${padZero(deviceData.schedule.filterStartMin)} - ${padZero(deviceData.schedule.filterEndHour)}:${padZero(deviceData.schedule.filterEndMin)}`
            : (deviceData.schedule.filterMode === 1 ? "Zawsze włączony" : "Zawsze wyłączony");
    }

    const itemAir = document.getElementById('sched-item-air');
    if (itemAir) {
        if (!deviceData.modules.airEnabled) {
            itemAir.style.display = 'none';
        } else {
            itemAir.style.display = 'flex';
            if (metaAir) {
                metaAir.textContent = deviceData.schedule.airMode === 0 
                    ? `Harmonogram: ${padZero(deviceData.schedule.airStartHour)}:${padZero(deviceData.schedule.airStartMin)} - ${padZero(deviceData.schedule.airEndHour)}:${padZero(deviceData.schedule.airEndMin)}`
                    : (deviceData.schedule.airMode === 1 ? "Zawsze włączone" : "Zawsze wyłączone");
            }
        }
    }

    const itemFeed = document.getElementById('sched-item-feed');
    if (itemFeed) {
        if (!deviceData.modules.feedEnabled) {
            itemFeed.style.display = 'none';
        } else {
            itemFeed.style.display = 'flex';
            if (metaFeed) {
                const freq = deviceData.feeding.freq;
                const timeStr = `${padZero(deviceData.feeding.hour)}:${padZero(deviceData.feeding.minute)}`;
                if (freq === 1) metaFeed.textContent = `Codziennie o ${timeStr}`;
                else if (freq === 2) metaFeed.textContent = `Co 2 dni o ${timeStr}`;
                else if (freq === 3) metaFeed.textContent = `Co 3 dni o ${timeStr}`;
                else metaFeed.textContent = "Automatyka wyłączona";
            }
        }
    }
}

// 4.4 Temperature Screen Render
function renderTempTab() {
    const automEnable = document.getElementById('temp-autom-enable');
    const targetTemp = document.getElementById('val-target-temp');
    const hysteresis = document.getElementById('val-hysteresis');
    
    if (automEnable) automEnable.checked = (deviceData.schedule.heaterMode !== 1);
    if (targetTemp) targetTemp.textContent = `${deviceData.temperature.target.toFixed(1)}°C`;
    if (hysteresis) hysteresis.textContent = `${deviceData.temperature.hysteresis.toFixed(1)}°C`;
}

// 4.5 System Setup Screen Render
function renderSystemTab() {
    // Fill Diagnostic Panel Fields if open
    const sysWifiSSID = document.getElementById('sys-wifi-ssid');
    const sysWifiIP = document.getElementById('sys-wifi-ip');
    
    if (sysWifiSSID) sysWifiSSID.textContent = deviceData.network.staConnected ? deviceData.network.staSsid : "Brak STA (Tryb AP)";
    if (sysWifiIP) sysWifiIP.textContent = deviceData.network.ip;
}

// 4.6 Charts Screen Render
let currentChartRange = '1h';

function renderChartsTab() {
    const svg = document.getElementById('cyd-temp-chart-svg');
    const minEl = document.getElementById('chart-min');
    const maxEl = document.getElementById('chart-max');
    const curEl = document.getElementById('chart-cur');
    const emptyState = document.getElementById('chart-empty-state');
    
    if (!svg || !minEl) return;
    
    let baseHistory = isSimulatorMode ? simulatedTempHistory : (deviceData.temperature.history || []);
    
    // Simulate time ranges by slicing the array (Mock for CYD visualization)
    let history = [...baseHistory];
    if (currentChartRange === '1h') {
        history = history.slice(-20); // last 20 points
    } else if (currentChartRange === '7d') {
        // Mock a 7 day view by expanding the data artificially to look dense
        history = [...baseHistory, ...baseHistory.map(v => v + (Math.random()-0.5))];
    }
    
    if (history.length < 2) {
        emptyState.style.display = 'flex';
        svg.innerHTML = '';
        minEl.textContent = '--.-';
        maxEl.textContent = '--.-';
        curEl.textContent = '--.-';
        return;
    }
    
    emptyState.style.display = 'none';
    
    const minT = Math.min(...history);
    const maxT = Math.max(...history);
    const curT = history[history.length - 1];
    const targetT = deviceData.temperature.target;
    
    minEl.textContent = minT.toFixed(1);
    maxEl.textContent = maxT.toFixed(1);
    curEl.textContent = curT.toFixed(1);
    
    // SVG viewBox is 300x100
    const w = 300;
    const h = 100;
    const pad = 10; // vertical padding
    
    // Define bounds for scaling
    // We add some margin above and below min/max to not touch the borders
    const vMin = Math.min(minT, targetT - deviceData.temperature.hysteresis) - 0.2;
    const vMax = Math.max(maxT, targetT + deviceData.temperature.hysteresis) + 0.2;
    const vRange = Math.max(0.5, vMax - vMin);
    
    // Calculate Y coords
    const getY = (val) => h - pad - ((val - vMin) / vRange) * (h - pad * 2);
    const getX = (index) => (index / (history.length - 1)) * w;
    
    // Draw Target Band
    const yTop = getY(targetT + deviceData.temperature.hysteresis);
    const yBot = getY(targetT - deviceData.temperature.hysteresis);
    const yTarget = getY(targetT);
    
    let svgContent = `<rect x="0" y="${yTop}" width="${w}" height="${yBot - yTop}" class="cyd-chart-band" />`;
    svgContent += `<line x1="0" y1="${yTarget}" x2="${w}" y2="${yTarget}" class="cyd-chart-target-line" />`;
    
    // Draw Heater Simulation Area
    let heaterArea = '';
    for (let i = 0; i < history.length - 1; i++) {
        if (history[i] < targetT - 0.1) { // Simulating heater ON when below target
            const x1 = getX(i);
            const x2 = getX(i+1);
            heaterArea += `<rect x="${x1}" y="0" width="${Math.max(1, x2-x1)}" height="${h}" class="cyd-chart-heater-area" />`;
        }
    }
    svgContent += heaterArea;
    
    // Draw Path
    let pathD = `M 0 ${getY(history[0])}`;
    for (let i = 1; i < history.length; i++) {
        pathD += ` L ${getX(i)} ${getY(history[i])}`;
    }
    
    svgContent += `<path d="${pathD}" class="cyd-chart-line" />`;
    
    svg.innerHTML = svgContent;
}

/* ==========================================================================
   5. MODALS & SUBPAGES INTERACTIVITY
   ========================================================================== */

// Open and load schedule values into editor modal overlay
window.openScheduleEditor = function(device) {
    const overlay = document.getElementById('overlay-schedule-editor');
    const title = document.getElementById('sched-editor-title');
    const modeSelect = document.getElementById('sched-editor-mode');
    const startGroup = document.getElementById('sched-time-range-group');
    const pointGroup = document.getElementById('sched-time-point-group');
    
    document.getElementById('sched-editor-device').value = device;
    overlay.classList.add('active');
    activeSubpage = 'schedule';
    
    // Load current values
    let options = "";
    if (device === 'feed') {
        title.textContent = "Harmonogram Karmnika";
        options = `
            <option value="wylaczone">Wyłączone</option>
            <option value="codziennie">Codziennie</option>
            <option value="co_2_dni">Co 2 dni</option>
            <option value="co_3_dni">Co 3 dni</option>
        `;
        modeSelect.innerHTML = options;
        
        // Select current value
        const freqVal = ["wylaczone", "codziennie", "co_2_dni", "co_3_dni"][deviceData.feeding.freq];
        modeSelect.value = freqVal;
        
        startGroup.style.display = 'none';
        pointGroup.style.display = 'flex';
        document.getElementById('sched-color-mode-group').style.display = 'none';
        
        document.getElementById('sched-editor-time').value = `${padZero(deviceData.feeding.hour)}:${padZero(deviceData.feeding.minute)}`;
    } else {
        // Relays mode select
        options = `
            <option value="harmonogram">Automatyczny (Harm.)</option>
            <option value="zawsze_wlaczone">Zawsze włączone</option>
            <option value="zawsze_wylaczone">Zawsze wyłączone</option>
        `;
        modeSelect.innerHTML = options;
        
        startGroup.style.display = 'flex';
        pointGroup.style.display = 'none';
        const colorModeGroup = document.getElementById('sched-color-mode-group');
        const colorModeSelect = document.getElementById('sched-editor-color-mode');
        
        if (device === 'light1') {
            title.textContent = "Harmonogram Oświetlenia 1";
            modeSelect.value = ["harmonogram", "zawsze_wlaczone", "zawsze_wylaczone"][deviceData.schedule.light1Mode];
            colorModeGroup.style.display = 'flex';
            colorModeSelect.value = String(deviceData.schedule.light1ColorMode);
            document.getElementById('sched-editor-start').value = `${padZero(deviceData.schedule.light1StartHour)}:${padZero(deviceData.schedule.light1StartMin)}`;
            document.getElementById('sched-editor-end').value = `${padZero(deviceData.schedule.light1EndHour)}:${padZero(deviceData.schedule.light1EndMin)}`;
        } else if (device === 'light2') {
            title.textContent = "Harmonogram Oświetlenia 2";
            modeSelect.value = ["harmonogram", "zawsze_wlaczone", "zawsze_wylaczone"][deviceData.schedule.light2Mode];
            colorModeGroup.style.display = 'flex';
            colorModeSelect.value = String(deviceData.schedule.light2ColorMode);
            document.getElementById('sched-editor-start').value = `${padZero(deviceData.schedule.light2StartHour)}:${padZero(deviceData.schedule.light2StartMin)}`;
            document.getElementById('sched-editor-end').value = `${padZero(deviceData.schedule.light2EndHour)}:${padZero(deviceData.schedule.light2EndMin)}`;
        } else if (device === 'pump') {
            title.textContent = "Harmonogram Filtra";
            colorModeGroup.style.display = 'none';
            modeSelect.value = ["harmonogram", "zawsze_wlaczone", "zawsze_wylaczone"][deviceData.schedule.filterMode];
            document.getElementById('sched-editor-start').value = `${padZero(deviceData.schedule.filterStartHour)}:${padZero(deviceData.schedule.filterStartMin)}`;
            document.getElementById('sched-editor-end').value = `${padZero(deviceData.schedule.filterEndHour)}:${padZero(deviceData.schedule.filterEndMin)}`;
        } else if (device === 'air') {
            title.textContent = "Harmonogram Napowietrzania";
            colorModeGroup.style.display = 'none';
            modeSelect.value = ["harmonogram", "zawsze_wlaczone", "zawsze_wylaczone"][deviceData.schedule.airMode];
            document.getElementById('sched-editor-start').value = `${padZero(deviceData.schedule.airStartHour)}:${padZero(deviceData.schedule.airStartMin)}`;
            document.getElementById('sched-editor-end').value = `${padZero(deviceData.schedule.airEndHour)}:${padZero(deviceData.schedule.airEndMin)}`;
        }
    }
};

window.closeScheduleEditor = function() {
    closeAllOverlays();
};

// Open Sub-panels inside System Tab (WiFi, Clock, Diag, Power)
window.openSystemSub = function(subName) {
    const overlay = document.getElementById('overlay-system-sub');
    overlay.classList.add('active');
    
    // Hide all panels, show requested one
    document.querySelectorAll('.sys-sub').forEach(p => p.style.display = 'none');
    
    const targetPanel = document.getElementById(`sub-${subName}`);
    if (targetPanel) targetPanel.style.display = 'flex';
    
    activeSubpage = subName;
    
    // Load system values
    if (subName === 'wifi') {
        document.getElementById('wifi-sta-ssid').value = deviceData.network.configuredStaSsid;
        document.getElementById('wifi-sta-pass').value = ""; // Don't pre-populate pass
        
        // Show status
        document.getElementById('sys-wifi-ssid').textContent = deviceData.network.staConnected ? deviceData.network.staSsid : "AP Mode Active";
        document.getElementById('sys-wifi-ip').textContent = deviceData.network.ip;
    } else if (subName === 'clock') {
        updateSubpageClock();
    } else if (subName === 'screen') {
        // Populate CYD screen toggle
        const isAlwaysOn = deviceData.display ? deviceData.display.alwaysScreenOn : false;
        document.getElementById('lcd-always-on').checked = isAlwaysOn;
    } else if (subName === 'logs') {
        fetchLogsForSubpage();
    } else if (subName === 'diagnostics') {
        const { uptimeSeconds, resetReason, resetCount, powerMode, sleepBlockers } = deviceData;
        const firmware = deviceData.system.firmware || {};
        
        document.getElementById('diag-uptime').textContent = formatUptime(deviceData.system.uptimeSeconds);
        document.getElementById('diag-version').textContent = firmware.version || "v1.0";
        document.getElementById('diag-power').textContent = deviceData.system.powerMode;
        document.getElementById('diag-resets').textContent = deviceData.system.resetCount;
        document.getElementById('diag-reason').textContent = deviceData.system.resetReason;
        
        const ramEl = document.getElementById('diag-ram');
        if (ramEl) ramEl.textContent = "124 KB"; // Mock RAM for now
        
        const vbatEl = document.getElementById('diag-vbat');
        if (vbatEl) vbatEl.textContent = deviceData.battery ? `${deviceData.battery.voltage.toFixed(2)}V` : "-";
        
        const rssi = deviceData.network.rssi;
        document.getElementById('diag-rssi').textContent = deviceData.network.staConnected ? `${rssi} dBm` : "OFFLINE";
        
        document.getElementById('diag-blockers').textContent = (deviceData.system.sleepBlockers || []).join(', ') || 'ready';
    }
};

window.closeSystemSub = function() {
    closeAllOverlays();
};

// Updates time variables specifically in clock subpage
function updateSubpageClock() {
    if (activeSubpage !== 'clock') return;
    
    const timeVal = document.getElementById('sys-clock-time');
    const dateVal = document.getElementById('sys-clock-date');
    if (!timeVal || !dateVal) return;
    
    const { hour, minute, second, day, month, year } = deviceData.clock;
    timeVal.textContent = `${padZero(hour)}:${padZero(minute)}:${padZero(second)}`;
    
    const monthNames = ["sty", "lut", "mar", "kwi", "maj", "cze", "lip", "sie", "wrz", "paź", "lis", "gru"];
    dateVal.textContent = `${padZero(day)} ${monthNames[month - 1]} ${year}`;
}

// Intermittently push updates to clock page if visible
setInterval(updateSubpageClock, 1000);

// Populate logs list with dummy data
let localLogs = [
    { time: "16:15", type: "info", msg: "ESP32 Boot OK" },
    { time: "16:16", type: "info", msg: "WiFi AP Started: AquaSync_AP" },
    { time: "16:20", type: "warn", msg: "Sensor temperatury nie odpowiedział w porę" },
    { time: "16:21", type: "info", msg: "Połączono: AquaNet_Local" }
];

let currentLogsFilter = 'all';

function fetchLogsForSubpage() {
    const container = document.getElementById('cyd-logs-container');
    if (!container) return;
    
    const filteredLogs = localLogs.filter(log => {
        if (currentLogsFilter === 'all') return true;
        if (currentLogsFilter === 'info' && log.type === 'info') return true;
        if (currentLogsFilter === 'warn' && (log.type === 'warn' || log.type === 'error')) return true;
        return false;
    });
    
    if (filteredLogs.length === 0) {
        container.innerHTML = `<div class="log-item empty">Brak wpisów w dzienniku.</div>`;
        return;
    }
    
    container.innerHTML = filteredLogs.map(log => 
        `<div class="log-item ${log.type}">
            <span class="log-time">${log.time}</span> <span class="log-info">${log.msg}</span>
         </div>`
    ).reverse().join('');
}

window.clearLogs = function() {
    localLogs = [];
    fetchLogsForSubpage();
    showFeedbackModal('success', 'Wyczyszczono', 'Logi zostały wyczyszczone.', 1000);
};

// Edge Case: Confirmation Modal
window.confirmAction = function(message, onYes) {
    const overlay = document.getElementById('overlay-confirm');
    document.getElementById('confirm-msg').textContent = message;
    overlay.classList.add('active');
    
    const btnYes = document.getElementById('btn-confirm-yes');
    const btnNo = document.getElementById('btn-confirm-no');
    
    const cleanup = () => {
        // Clone to remove event listeners safely
        btnYes.replaceWith(btnYes.cloneNode(true));
        btnNo.replaceWith(btnNo.cloneNode(true));
        overlay.classList.remove('active');
    };
    
    document.getElementById('btn-confirm-no').addEventListener('click', cleanup);
    document.getElementById('btn-confirm-yes').addEventListener('click', () => {
        cleanup();
        onYes();
    });
};

/* ==========================================================================
   6. USER SYSTEM ACTIONS & INPUTS INTERACTIONS
   ========================================================================== */

function initSystemEventHandlers() {
    // Quick triggers toggles from Home Page
    const btnLight = document.getElementById('btn-toggle-light');
    const btnPump = document.getElementById('btn-toggle-pump');
    
    if (btnLight) {
        btnLight.addEventListener('click', () => {
            const next = !deviceData.relays.light1; // Defaulting to light1 for quick toggle
            postAction({ type: 'relay', target: 'light1', state: next });
        });
    }
    if (btnPump) {
        btnPump.addEventListener('click', () => {
            const next = !deviceData.relays.pump;
            postAction({ type: 'relay', target: 'pump', state: next });
        });
    }

    // Buttons for quick actions
    const btnFeed = document.getElementById('btn-feed-now');
    if (btnFeed) btnFeed.addEventListener('click', () => {
        postAction({ type: 'feed' });
        showToast('Trwa karmienie...');
    });
    
    // Relay toggles
    document.getElementById('btn-relay-light1-toggle')?.addEventListener('click', () => {
        const next = !deviceData.relays.light1;
        postAction({ type: 'relay', target: 'light1', state: next });
    });
    document.getElementById('btn-relay-light2-toggle')?.addEventListener('click', () => {
        const next = !deviceData.relays.light2;
        postAction({ type: 'relay', target: 'light2', state: next });
    });
    document.getElementById('btn-relay-pump-toggle')?.addEventListener('click', () => {
        const next = !deviceData.relays.pump;
        postAction({ type: 'relay', target: 'pump', state: next });
    });
    document.getElementById('btn-toggle-pump')?.addEventListener('click', () => {
        const next = !deviceData.relays.pump;
        postAction({ type: 'relay', target: 'pump', state: next });
    });

    // Save Temperature button
    const btnSaveTemp = document.getElementById('btn-save-temp');
    btnSaveTemp.addEventListener('click', () => {
        const enabled = document.getElementById('temp-autom-enable').checked;
        const target = deviceData.temperature.target;
        const hyst = deviceData.temperature.hysteresis;
        
        postAction('save_temperature', {
            heaterMode: enabled ? '0' : '1', // 0 = Enabled, 1 = Disabled
            targetTemp: String(target),
            tempHyst: String(hyst)
        });
    });

    // Tile buttons in Relays page
    document.getElementById('btn-relay-light1-toggle').addEventListener('click', () => {
        openScheduleEditor('light1');
    });
    document.getElementById('btn-relay-light2-toggle')?.addEventListener('click', () => {
        openScheduleEditor('light2');
    });
    document.getElementById('btn-relay-pump-toggle').addEventListener('click', () => {
        openScheduleEditor('pump');
    });

    // Save Schedule configurations
    document.getElementById('btn-save-schedule').addEventListener('click', () => {
        saveActiveScheduleInEditor();
    });

    // WiFi actions inside WiFi subpage
    const wifiSsidInput = document.getElementById('wifi-sta-ssid');
    const wifiPassInput = document.getElementById('wifi-sta-pass');

    // Trigger OSK for WiFi inputs
    if (wifiSsidInput) {
        wifiSsidInput.addEventListener('focus', (e) => {
            e.target.blur();
            openKeyboard(e.target.id, 'Wprowadź SSID sieci WiFi', false);
        });
    }
    
    if (wifiPassInput) {
        wifiPassInput.addEventListener('focus', (e) => {
            e.target.blur();
            openKeyboard(e.target.id, 'Wprowadź hasło WiFi', true);
        });
    }

    document.getElementById('btn-save-wifi').addEventListener('click', () => {
        const ssid = wifiSsidInput.value;
        const pass = wifiPassInput.value;
        const payload = { staSsid: ssid, apSsid: deviceData.network.configuredApSsid };
        if (pass) payload.staPassword = pass;
        
        postAction('save_network', payload);
    });

    document.getElementById('btn-wifi-connect').addEventListener('click', () => {
        postAction('wifi_session_start');
    });
    document.getElementById('btn-wifi-disconnect').addEventListener('click', () => {
        postAction('wifi_session_stop');
    });

    // Time Sync buttons
    document.getElementById('btn-sync-browser').addEventListener('click', async () => {
        const epoch = Math.trunc(Date.now() / 1000);
        if (isSimulatorMode) {
            const dt = new Date();
            deviceData.clock = {
                year: dt.getFullYear(), month: dt.getMonth() + 1, day: dt.getDate(),
                hour: dt.getHours(), minute: dt.getMinutes(), second: dt.getSeconds()
            };
            showFeedbackModal('success', 'Czas zsynchronizowany', 'Czas pobrany z przeglądarki.', 1500);
            updateUi();
            showToast('Czas pomyślnie zsynchronizowany.');
        } else {
            try {
                showFeedbackModal('progress', 'Ustawianie czasu...', 'Synchronizacja czasu z PC/Telefonem.');
                const response = await fetch(API_SETTIME, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: new URLSearchParams({ epoch: String(epoch) }).toString()
                });
                if (!response.ok) throw new Error();
                showFeedbackModal('success', 'Czas zsynchronizowany', 'Czas przeglądarki wysłany.', 1500);
                fetchStatusFromBackend();
            } catch (e) {
                showFeedbackModal('error', 'Błąd synchronizacji', 'Nie udało się ustawić czasu.', 2000);
            }
        }
    });

    document.getElementById('btn-sync-ntp').addEventListener('click', () => {
        postAction('sync_time_ntp');
        if (isSimulatorMode) showToast('Wymuszono synchronizację NTP.');
    });

    // Power Actions
    document.getElementById('btn-reboot')?.addEventListener('click', () => {
        window.confirmAction('Czy zresetować sterownik?', () => {
            postAction({ type: 'sys', cmd: 'reboot' });
            showToast('Restartowanie...');
        });
    });
    
    document.getElementById('btn-factory-reset')?.addEventListener('click', () => {
        window.confirmAction('UWAGA: Wymazać dane i przywrócić fabryczne?', () => {
            postAction({ type: 'sys', cmd: 'factory_reset' });
            showToast('Wykonywanie resetu...');
        });
    });
    
    // Screen LCD actions
    document.getElementById('btn-save-screen')?.addEventListener('click', () => {
        const chk = document.getElementById('lcd-always-on');
        const sens = document.getElementById('lcd-ldr-sens');
        if (chk && sens) {
            postAction({ type: 'lcd', alwaysOn: chk.checked, ldrSensitivity: parseInt(sens.value, 10) });
            showToast('Ustawienia ekranu zapisane');
        }
        if (isSimulatorMode) {
            if (!deviceData.display) deviceData.display = {};
            deviceData.display.alwaysScreenOn = chk.checked;
            showFeedbackModal('success', 'Zapisano', 'Ustawienia ekranu zastosowane.', 1500);
            closeSystemSub();
        }
    });

    document.getElementById('lcd-ldr-sens')?.addEventListener('input', (e) => {
        const v = document.getElementById('ldr-sens-val');
        if (v) v.textContent = e.target.value + '%';
    });

    // Modules Actions
    document.getElementById('btn-save-modules')?.addEventListener('click', () => {
        const air = document.getElementById('module-air-enable');
        const feed = document.getElementById('module-feed-enable');
        if (air && feed) {
            postAction({ type: 'modules', airEnabled: air.checked, feedEnabled: feed.checked });
            showToast('Moduły zaktualizowane');
        }
    });
    
    document.getElementById('btn-clear-logs')?.addEventListener('click', () => {
        clearLogs();
    });

    // Dismiss feedback popup
    document.getElementById('btn-modal-dismiss').addEventListener('click', () => {
        closeAllOverlays();
    });

    // Chart timeframe buttons
    document.querySelectorAll('.chart-btn').forEach(btn => {
        btn.addEventListener('click', (e) => {
            document.querySelectorAll('.chart-btn').forEach(b => b.classList.remove('active'));
            e.target.classList.add('active');
            // MOCK: Wymuś przerysowanie wykresu by zasymulować zmianę zakresu
            currentChartRange = e.target.getAttribute('data-range');
            renderChartsTab();
        });
    });

    // Logs filters
    document.querySelectorAll('.log-flt-btn').forEach(btn => {
        btn.addEventListener('click', (e) => {
            document.querySelectorAll('.log-flt-btn').forEach(b => b.classList.remove('active'));
            e.target.classList.add('active');
            currentLogsFilter = e.target.getAttribute('data-filter');
            fetchLogsForSubpage();
        });
    });
}

// Adjust Target Temperature in UI cache (requires Save button click)
window.adjustTargetTemp = function(delta) {
    let t = deviceData.temperature.target + delta;
    t = Math.max(18.0, Math.min(30.0, t));
    deviceData.temperature.target = parseFloat(t.toFixed(1));
    renderTempTab();
};

// Adjust Hysteresis in UI cache
window.adjustHysteresis = function(delta) {
    let h = deviceData.temperature.hysteresis + delta;
    h = Math.max(0.1, Math.min(5.0, h));
    deviceData.temperature.hysteresis = parseFloat(h.toFixed(1));
    renderTempTab();
};

// Feeding sequence trigger with feedback overlay (Spinner -> Success/Failure)
function triggerFeedAction() {
    if (isSimulatorMode) {
        showFeedbackModal('progress', 'Trwa karmienie...', 'Sensor położenia w trakcie odczytu.');
        updateRGBLED('blue');
        
        setTimeout(() => {
            showFeedbackModal('success', 'Sukces', 'Karmienie zakończyło się pomyślnie.', 2400);
            deviceData.feeding.lastFeedEpoch = Math.trunc(Date.now() / 1000);
            updateRGBLED('green');
            updateUi();
        }, 3500);
        return;
    }
    
    // Remote trigger
    postAction('feed_now');
}

// Save schedule data from overlay inputs
function saveActiveScheduleInEditor() {
    const device = document.getElementById('sched-editor-device').value;
    const mode = document.getElementById('sched-editor-mode').value;
    
    // Construct request parameters matching schedules.js logic
    const payload = {
        light1Mode: String(deviceData.schedule.light1Mode),
        light1ColorMode: String(deviceData.schedule.light1ColorMode),
        light1Start: `${padZero(deviceData.schedule.light1StartHour)}:${padZero(deviceData.schedule.light1StartMin)}`,
        light1End: `${padZero(deviceData.schedule.light1EndHour)}:${padZero(deviceData.schedule.light1EndMin)}`,
        light2Mode: String(deviceData.schedule.light2Mode),
        light2ColorMode: String(deviceData.schedule.light2ColorMode),
        light2Start: `${padZero(deviceData.schedule.light2StartHour)}:${padZero(deviceData.schedule.light2StartMin)}`,
        light2End: `${padZero(deviceData.schedule.light2EndHour)}:${padZero(deviceData.schedule.light2EndMin)}`,
        aerationMode: String(deviceData.schedule.airMode),
        airOn: `${padZero(deviceData.schedule.airStartHour)}:${padZero(deviceData.schedule.airStartMin)}`,
        airOff: `${padZero(deviceData.schedule.airEndHour)}:${padZero(deviceData.schedule.airEndMin)}`,
        filterMode: String(deviceData.schedule.filterMode),
        filterOn: `${padZero(deviceData.schedule.filterStartHour)}:${padZero(deviceData.schedule.filterStartMin)}`,
        filterOff: `${padZero(deviceData.schedule.filterEndHour)}:${padZero(deviceData.schedule.filterEndMin)}`,
        feedFreq: String(deviceData.feeding.freq),
        feedTime: `${padZero(deviceData.feeding.hour)}:${padZero(deviceData.feeding.minute)}`
    };

    if (device === 'feed') {
        const modeMap = { "wylaczone": 0, "codziennie": 1, "co_2_dni": 2, "co_3_dni": 3 };
        payload.feedFreq = String(modeMap[mode]);
        payload.feedTime = document.getElementById('sched-editor-time').value;
    } else {
        const modeMap = { "harmonogram": 0, "zawsze_wlaczone": 1, "zawsze_wylaczone": 2 };
        const timeStart = document.getElementById('sched-editor-start').value;
        const timeEnd = document.getElementById('sched-editor-end').value;
        
        if (device === 'light1') {
            payload.light1Mode = String(modeMap[mode]);
            payload.light1ColorMode = document.getElementById('sched-editor-color-mode').value;
            payload.light1Start = timeStart;
            payload.light1End = timeEnd;
        } else if (device === 'light2') {
            payload.light2Mode = String(modeMap[mode]);
            payload.light2ColorMode = document.getElementById('sched-editor-color-mode').value;
            payload.light2Start = timeStart;
            payload.light2End = timeEnd;
        } else if (device === 'pump') {
            payload.filterMode = String(modeMap[mode]);
            payload.filterOn = timeStart;
            payload.filterOff = timeEnd;
        } else if (device === 'air') {
            payload.aerationMode = String(modeMap[mode]);
            payload.airOn = timeStart;
            payload.airOff = timeEnd;
        }
    }

    postAction('save_schedule', payload).then(() => {
        closeAllOverlays();
    });
}

/* ==========================================================================
   7. SIMULATION FALLBACK ENGINE HANDLERS
   ========================================================================== */

function handleSimulatedAction(action, payload) {
    return new Promise((resolve) => {
        showFeedbackModal('progress', 'Zapisywanie...', 'Konfiguracja symulatora...');
        updateRGBLED('blue');
        
        setTimeout(() => {
            if (action === 'set_light') {
                deviceData.relays.light = (payload.state === '1');
            } else if (action === 'set_filter') {
                deviceData.relays.pump = (payload.state === '1');
            } else if (action === 'save_temperature') {
                deviceData.schedule.heaterMode = Number(payload.heaterMode);
                deviceData.temperature.target = Number(payload.targetTemp);
                deviceData.temperature.hysteresis = Number(payload.tempHyst);
            } else if (action === 'save_network') {
                deviceData.network.configuredStaSsid = payload.staSsid;
                deviceData.network.configuredApSsid = payload.apSsid;
            } else if (action === 'save_schedule') {
                // Apply relays schedule modifications in simulator memory
                deviceData.schedule.lightMode = Number(payload.lightMode);
                const [lsH, lsM] = payload.dayStart.split(':').map(Number);
                const [leH, leM] = payload.dayEnd.split(':').map(Number);
                deviceData.schedule.dayStartHour = lsH; deviceData.schedule.dayStartMin = lsM;
                deviceData.schedule.dayEndHour = leH; deviceData.schedule.dayEndMin = leM;

                deviceData.schedule.filterMode = Number(payload.filterMode);
                const [fsH, fsM] = payload.filterOn.split(':').map(Number);
                const [feH, feM] = payload.filterOff.split(':').map(Number);
                deviceData.schedule.filterStartHour = fsH; deviceData.schedule.filterStartMin = fsM;
                deviceData.schedule.filterEndHour = feH; deviceData.schedule.filterEndMin = feM;

                deviceData.schedule.airMode = Number(payload.aerationMode);
                const [asH, asM] = payload.airOn.split(':').map(Number);
                const [aeH, aeM] = payload.airOff.split(':').map(Number);
                deviceData.schedule.airStartHour = asH; deviceData.schedule.airStartMin = asM;
                deviceData.schedule.airEndHour = aeH; deviceData.schedule.airEndMin = aeM;

                deviceData.feeding.freq = Number(payload.feedFreq);
                const [fH, fM] = payload.feedTime.split(':').map(Number);
                deviceData.feeding.hour = fH; deviceData.feeding.minute = fM;
            } else if (action === 'wifi_session_start') {
                deviceData.network.staConnected = true;
                deviceData.network.staConnecting = false;
                deviceData.network.apMode = false;
                deviceData.network.ip = "192.168.1.134";
                deviceData.network.rssi = -64;
            } else if (action === 'wifi_session_stop') {
                deviceData.network.staConnected = false;
                deviceData.network.apMode = true;
                deviceData.network.ip = "192.168.4.1";
                deviceData.network.clients = 1;
            } else if (action === 'sync_time_ntp') {
                showFeedbackModal('success', 'NTP Synchronized', 'RTC synced with NTP server.', 1200);
                resolve({ success: true });
                return;
            } else if (action === 'restart') {
                showFeedbackModal('progress', 'Rebooting ESP...', 'Sterownik uruchamia się ponownie.');
                updateRGBLED('red');
                setTimeout(() => {
                    showFeedbackModal('success', 'Uruchomiono', 'Sterownik gotowy.', 1200);
                    updateRGBLED('green');
                    updateUi();
                }, 3000);
                resolve({ success: true });
                return;
            } else if (action === 'factory_reset') {
                showFeedbackModal('progress', 'Wymazywanie...', 'Przywracanie ustawień fabrycznych.');
                updateRGBLED('red');
                setTimeout(() => {
                    showFeedbackModal('success', 'Wymazano NVS', 'Uruchomiono ponownie.', 1500);
                    setTimeout(() => location.reload(), 1600);
                }, 4000);
                resolve({ success: true });
                return;
            }
            
            showFeedbackModal('success', 'Zapisano (Sym)', 'Zaktualizowano dane w pamięci symulatora.', 1200);
            updateRGBLED('green');
            updateUi();
            resolve({ success: true });
        }, 1200);
    });
}

/* ==========================================================================
   8. COMMON AUXILIARY HELPERS
   ========================================================================== */

function showFeedbackModal(kind, title, message, autoDismissMs = 0) {
    const overlay = document.getElementById('overlay-feedback');
    const titleEl = document.getElementById('overlay-title');
    const msgEl = document.getElementById('overlay-msg');
    const spinner = document.getElementById('modal-spinner');
    const successIcon = document.getElementById('modal-success-icon');
    const errorIcon = document.getElementById('modal-error-icon');
    const dismissBtn = document.getElementById('btn-modal-dismiss');
    
    if (!overlay || !titleEl || !msgEl) return;
    
    overlay.classList.add('active');
    titleEl.textContent = title;
    msgEl.textContent = message;
    
    // Reset displays
    spinner.style.display = 'none';
    successIcon.style.display = 'none';
    errorIcon.style.display = 'none';
    dismissBtn.style.display = 'none';
    
    if (kind === 'progress') {
        spinner.style.display = 'block';
    } else if (kind === 'success') {
        successIcon.style.display = 'block';
        updateRGBLED('green');
    } else if (kind === 'error') {
        errorIcon.style.display = 'block';
        updateRGBLED('red');
    }
    
    if (autoDismissMs > 0) {
        setTimeout(() => {
            overlay.classList.remove('active');
        }, autoDismissMs);
    } else if (kind !== 'progress') {
        dismissBtn.style.display = 'inline-block';
    }
}

// Status lights toggler (physical RGB LED simulation on the yellow board bezel)
function updateRGBLED(color) {
    const led = document.getElementById('board-led');
    if (!led) return;
    
    led.className = "pcb-component led-rgb"; // reset
    if (color === 'green') led.classList.add('led-active-green');
    else if (color === 'red') led.classList.add('led-active-red');
    else if (color === 'blue') led.classList.add('led-active-blue');
}

function updateConnectionStatusDot(isOnline) {
    const dot = document.getElementById('status-dot');
    if (dot) {
        dot.className = `status-dot ${isOnline ? 'state-online' : 'state-offline'}`;
        dot.title = isOnline ? "Połączono ze sterownikiem" : "Brak połączenia (Tryb Symulatora)";
    }
}

// Formatting helpers
function padZero(num) {
    return String(Math.max(0, Math.trunc(num || 0))).padStart(2, '0');
}

function formatUptime(secs) {
    const d = Math.floor(secs / 86400);
    const h = Math.floor((secs % 86400) / 3600);
    const m = Math.floor((secs % 3600) / 60);
    const s = secs % 60;
    
    if (d > 0) {
        return `${d}d ${padZero(h)}:${padZero(m)}:${padZero(s)}`;
    }
    return `${padZero(h)}:${padZero(m)}:${padZero(s)}`;
}

/* ==========================================================================
   9. ON-SCREEN KEYBOARD (OSK) LOGIC
   ========================================================================== */
let oskTargetId = null;
let oskIsPassword = false;
let oskIsShift = false;
let oskIsSymbols = false;

const layouts = {
    normal: [
        ['q','w','e','r','t','y','u','i','o','p'],
        ['a','s','d','f','g','h','j','k','l'],
        ['SHIFT','z','x','c','v','b','n','m','BACK']
    ],
    shift: [
        ['Q','W','E','R','T','Y','U','I','O','P'],
        ['A','S','D','F','G','H','J','K','L'],
        ['SHIFT','Z','X','C','V','B','N','M','BACK']
    ],
    symbols: [
        ['1','2','3','4','5','6','7','8','9','0'],
        ['-','/',':',';','(',')','$','&','@','"'],
        ['SYM','.',',','?','!',"'",'_','BACK']
    ]
};

window.openKeyboard = function(targetId, title, isPassword) {
    oskTargetId = targetId;
    oskIsPassword = isPassword;
    oskIsShift = false;
    oskIsSymbols = false;
    
    document.getElementById('osk-title').textContent = title;
    document.getElementById('overlay-keyboard').classList.add('active');
    
    const targetInput = document.getElementById(targetId);
    if (targetInput) {
        document.getElementById('osk-input-preview').value = targetInput.value;
    }
    
    renderKeyboard();
};

window.closeKeyboard = function() {
    document.getElementById('overlay-keyboard').classList.remove('active');
    oskTargetId = null;
};

window.applyKeyboard = function() {
    if (oskTargetId) {
        const targetInput = document.getElementById(oskTargetId);
        if (targetInput) {
            targetInput.value = document.getElementById('osk-input-preview').value;
        }
    }
    closeKeyboard();
};

window.oskKeyPress = function(key) {
    const preview = document.getElementById('osk-input-preview');
    
    if (key === 'BACK') {
        preview.value = preview.value.slice(0, -1);
    } else if (key === 'SHIFT') {
        oskIsShift = !oskIsShift;
        renderKeyboard();
    } else if (key === 'SYM') {
        oskIsSymbols = !oskIsSymbols;
        renderKeyboard();
    } else if (key === 'SPACE') {
        preview.value += ' ';
    } else {
        preview.value += key;
        if (oskIsShift && !oskIsSymbols) {
            oskIsShift = false; // Auto unshift
            renderKeyboard();
        }
    }
};

function renderKeyboard() {
    const container = document.getElementById('cyd-keyboard-container');
    container.innerHTML = '';
    
    let layoutType = oskIsSymbols ? 'symbols' : (oskIsShift ? 'shift' : 'normal');
    const layout = layouts[layoutType];
    
    layout.forEach(row => {
        const rowDiv = document.createElement('div');
        rowDiv.className = 'osk-row';
        row.forEach(key => {
            const btn = document.createElement('div');
            btn.className = 'key-btn';
            
            if (key === 'SHIFT') {
                btn.classList.add('key-special');
                btn.textContent = '⇧';
                if (oskIsShift) btn.style.background = 'var(--accent-cyan)';
            } else if (key === 'BACK') {
                btn.classList.add('key-special');
                btn.textContent = '⌫';
            } else if (key === 'SYM') {
                btn.classList.add('key-special');
                btn.textContent = oskIsSymbols ? 'ABC' : '123';
            } else {
                btn.textContent = key;
            }
            
            btn.addEventListener('mousedown', (e) => { e.preventDefault(); oskKeyPress(key); });
            btn.addEventListener('touchstart', (e) => { e.preventDefault(); oskKeyPress(key); }, {passive: false});
            rowDiv.appendChild(btn);
        });
        container.appendChild(rowDiv);
    });
    
    // Bottom row (Sym, Space, .)
    const bottomRow = document.createElement('div');
    bottomRow.className = 'osk-row';
    
    const symBtn = document.createElement('div');
    symBtn.className = 'key-btn key-special';
    symBtn.textContent = oskIsSymbols ? 'ABC' : '123';
    symBtn.addEventListener('mousedown', (e) => { e.preventDefault(); oskKeyPress('SYM'); });
    symBtn.addEventListener('touchstart', (e) => { e.preventDefault(); oskKeyPress('SYM'); }, {passive: false});
    
    const spaceBtn = document.createElement('div');
    spaceBtn.className = 'key-btn key-space';
    spaceBtn.textContent = 'Spacja';
    spaceBtn.addEventListener('mousedown', (e) => { e.preventDefault(); oskKeyPress('SPACE'); });
    spaceBtn.addEventListener('touchstart', (e) => { e.preventDefault(); oskKeyPress('SPACE'); }, {passive: false});
    
    const dotBtn = document.createElement('div');
    dotBtn.className = 'key-btn key-special';
    dotBtn.textContent = '.';
    dotBtn.addEventListener('mousedown', (e) => { e.preventDefault(); oskKeyPress('.'); });
    dotBtn.addEventListener('touchstart', (e) => { e.preventDefault(); oskKeyPress('.'); }, {passive: false});
    
    bottomRow.appendChild(symBtn);
    bottomRow.appendChild(spaceBtn);
    bottomRow.appendChild(dotBtn);
    
    container.appendChild(bottomRow);
}

// --------------------------------------------------------------------------
// 7. TOAST ANIMATIONS
// --------------------------------------------------------------------------
window.showToast = function(msg) {
    const toast = document.getElementById('cyd-toast');
    const msgEl = document.getElementById('cyd-toast-msg');
    const screen = document.getElementById('cyd-screen');
    if (!toast || !msgEl || !screen) return;
    
    msgEl.textContent = msg;
    toast.classList.add('show');
    
    // Screen border flash animation
    screen.classList.remove('flash-screen');
    void screen.offsetWidth; // trigger reflow
    screen.classList.add('flash-screen');
    
    setTimeout(() => {
        toast.classList.remove('show');
    }, 2000);
};
