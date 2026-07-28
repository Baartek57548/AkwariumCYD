const API_BUS_DIAGNOSTICS = '/api/bus-diagnostics';
const I2C_DEVICE_TYPES = Object.freeze({
    mcp23017: {
        name: 'MCP23017',
        role: 'Ekspander GPIO: przekaźniki oraz wejścia bezpieczeństwa.',
        certainty: 'Skonfigurowany w firmware'
    },
    gpio_expander: {
        name: 'Ekspander GPIO',
        role: 'Możliwy MCP23017 albo PCF8574. Adres nie rozstrzyga typu układu.',
        certainty: 'Identyfikacja prawdopodobna'
    },
    ads1115: {
        name: 'ADS1115',
        role: 'Przetwornik ADC dla pH, EC i wejść analogowych.',
        certainty: 'Skonfigurowany w firmware'
    },
    oled: {
        name: 'Wyświetlacz OLED',
        role: 'Możliwy sterownik SSD1306 albo SH1106.',
        certainty: 'Identyfikacja prawdopodobna'
    },
    current_pwm_sensor: {
        name: 'Czujnik lub sterownik',
        role: 'Możliwy INA219, PCA9685 albo czujnik SHT.',
        certainty: 'Adres współdzielony przez wiele układów'
    },
    sht3x: {
        name: 'Czujnik SHT3x',
        role: 'Prawdopodobny czujnik temperatury i wilgotności.',
        certainty: 'Identyfikacja prawdopodobna'
    },
    eeprom: {
        name: 'Pamięć EEPROM',
        role: 'Prawdopodobna pamięć z rodziny AT24Cxx.',
        certainty: 'Identyfikacja prawdopodobna'
    },
    rtc_or_imu: {
        name: 'RTC lub IMU',
        role: 'Możliwy DS3231/DS1307 albo MPU6050.',
        certainty: 'Adres współdzielony przez wiele układów'
    },
    environmental: {
        name: 'Czujnik środowiskowy',
        role: 'Możliwy BMP280, BME280 albo BME680.',
        certainty: 'Identyfikacja prawdopodobna'
    },
    unknown: {
        name: 'Nieznane urządzenie',
        role: 'Adres odpowiada, ale moduł nie występuje w mapie projektu.',
        certainty: 'Typ nierozpoznany'
    }
});

const ONEWIRE_DEVICE_TYPES = Object.freeze({
    ds1990_serial: { name: 'DS1990 / DS2401', role: 'Elektroniczny identyfikator lub numer seryjny.' },
    ds18s20: { name: 'DS18S20', role: 'Czujnik temperatury OneWire.' },
    ds1822: { name: 'DS1822', role: 'Czujnik temperatury OneWire.' },
    ds18b20: { name: 'DS18B20', role: 'Cyfrowy czujnik temperatury akwarium.' },
    ds2423_counter: { name: 'DS2423', role: 'Licznik impulsów z pamięcią.' },
    ds2438_battery: { name: 'DS2438', role: 'Monitor napięcia, temperatury i akumulatora.' },
    ds2431_eeprom: { name: 'DS2431', role: 'Pamięć EEPROM OneWire.' },
    max31850: { name: 'MAX31850', role: 'Przetwornik termopary OneWire.' },
    unknown: { name: 'Nieznane urządzenie', role: 'Rodzina ROM nie występuje w mapie projektu.' }
});

let busDiagnosticsInFlight = false;
let busDiagnosticsLastScanAtMs = 0;
let lastTopbarMarkup = '';
let lastRelayMarkup = null;
let lastScheduleSummaryMarkup = '';
let lastTemperatureChartSignature = '';
const dynamicRelayActionsInFlight = new Set();

function normalizeI2cDevices(payload) {
    if (!Array.isArray(payload?.devices)) return [];
    return payload.devices.slice(0, 16).map((device) => {
        const numericAddress = Number(device?.address);
        const address = Number.isInteger(numericAddress) && numericAddress >= 0x08 && numericAddress <= 0x77
            ? numericAddress
            : null;
        const type = Object.prototype.hasOwnProperty.call(I2C_DEVICE_TYPES, device?.type)
            ? device.type
            : 'unknown';
        return {
            address,
            hex: address === null ? '--' : `0x${address.toString(16).toUpperCase().padStart(2, '0')}`,
            type,
            configured: Boolean(device?.configured)
        };
    }).filter((device) => device.address !== null);
}

function i2cDeviceCountLabel(count) {
    if (count === 1) return '1 urządzenie';
    if (count >= 2 && count <= 4) return `${count} urządzenia`;
    return `${count} urządzeń`;
}

function normalizeOneWireDevices(payload) {
    if (!Array.isArray(payload?.oneWire?.devices)) return [];
    return payload.oneWire.devices.slice(0, 8).map((device) => {
        const type = Object.prototype.hasOwnProperty.call(ONEWIRE_DEVICE_TYPES, device?.type)
            ? device.type
            : 'unknown';
        return {
            rom: String(device?.rom || '').toUpperCase().replace(/[^0-9A-F-]/g, '').slice(0, 23),
            family: Number(device?.family),
            type,
            crcValid: device?.crcValid === true
        };
    }).filter((device) => device.rom.length > 0);
}

function setBusState(id, text, tone) {
    const element = document.getElementById(id);
    if (!element) return;
    element.textContent = text;
    element.dataset.tone = tone;
}

function renderHardwareBusDiagnostics(payload) {
    const devices = normalizeI2cDevices(payload);
    const oneWireDevices = normalizeOneWireDevices(payload);
    const sda = Number(payload?.sda);
    const scl = Number(payload?.scl);
    const frequencyHz = Number(payload?.frequencyHz);
    const scanMs = Number(payload?.scanMs);
    const i2cList = document.getElementById('i2c-device-list');
    const oneWireList = document.getElementById('onewire-device-list');
    const status = document.getElementById('bus-scan-status');
    const uart = Array.isArray(payload?.uart?.ports) ? payload.uart.ports[0] : null;

    setText('i2c-sda-pin', Number.isInteger(sda) ? `GPIO ${sda}` : '--');
    setText('i2c-scl-pin', Number.isInteger(scl) ? `GPIO ${scl}` : '--');
    setText('i2c-frequency', Number.isFinite(frequencyHz) ? `${Math.round(frequencyHz / 1000)} kHz` : '--');
    setText('i2c-device-count', i2cDeviceCountLabel(devices.length));
    setBusState('i2c-bus-state', devices.length ? 'AKTYWNA' : 'BRAK URZĄDZEŃ', devices.length ? 'ok' : 'warn');

    if (i2cList) {
        i2cList.innerHTML = devices.length
            ? devices.map((device) => {
                const info = I2C_DEVICE_TYPES[device.type] || I2C_DEVICE_TYPES.unknown;
                return `<article class="bus-device-card" data-configured="${device.configured ? 'true' : 'false'}">
                    <div class="bus-device-head">
                        <span class="bus-address">${escapeHtml(device.hex)}</span>
                        <span class="bus-confidence">${device.configured ? 'KONFIGURACJA' : 'WYKRYTO'}</span>
                    </div>
                    <strong>${escapeHtml(info.name)}</strong>
                    <p>${escapeHtml(info.role)}</p>
                    <small>${escapeHtml(info.certainty)}</small>
                </article>`;
            }).join('')
            : '<div class="bus-empty-state">Nie wykryto urządzeń odpowiadających na adresach 0x08-0x77.</div>';
    }

    const uartActive = uart?.active === true;
    setText('uart-port', Number.isInteger(Number(uart?.port)) ? `UART${Number(uart.port)}` : '--');
    setText('uart-tx-pin', Number.isInteger(Number(uart?.tx)) ? `GPIO ${Number(uart.tx)}` : '--');
    setText('uart-rx-pin', Number.isInteger(Number(uart?.rx)) ? `GPIO ${Number(uart.rx)}` : '--');
    setText('uart-baud', Number.isFinite(Number(uart?.baud)) ? `${Number(uart.baud).toLocaleString('pl-PL')} bps` : '--');
    setText('uart-format', uart?.format || '--');
    setBusState('uart-bus-state', uartActive ? 'AKTYWNA' : 'NIEAKTYWNA', uartActive ? 'ok' : 'warn');

    const oneWirePin = Number(payload?.oneWire?.dataPin);
    const oneWireScanMs = Number(payload?.oneWire?.scanMs);
    setText('onewire-data-pin', Number.isInteger(oneWirePin) ? `GPIO ${oneWirePin}` : '--');
    setText('onewire-device-count', i2cDeviceCountLabel(oneWireDevices.length));
    setBusState('onewire-bus-state', oneWireDevices.length ? 'AKTYWNA' : 'BRAK URZĄDZEŃ', oneWireDevices.length ? 'ok' : 'warn');
    if (oneWireList) {
        oneWireList.innerHTML = oneWireDevices.length
            ? oneWireDevices.map((device) => {
                const info = ONEWIRE_DEVICE_TYPES[device.type] || ONEWIRE_DEVICE_TYPES.unknown;
                return `<article class="bus-device-card" data-configured="${device.type === 'ds18b20' ? 'true' : 'false'}">
                    <div class="bus-device-head">
                        <span class="bus-address bus-rom">${escapeHtml(device.rom)}</span>
                        <span class="bus-confidence">${device.crcValid ? 'CRC OK' : 'CRC BŁĄD'}</span>
                    </div>
                    <strong>${escapeHtml(info.name)}</strong>
                    <p>${escapeHtml(info.role)}</p>
                    <small>Rodzina 0x${Number.isInteger(device.family) ? device.family.toString(16).toUpperCase().padStart(2, '0') : '--'}</small>
                </article>`;
            }).join('')
            : '<div class="bus-empty-state">Nie wykryto urządzeń OneWire. Sprawdź rezystor podciągający 4,7 kΩ i połączenie GPIO 17.</div>';
    }

    if (status) {
        const i2cDuration = Number.isFinite(scanMs) ? `${Math.max(0, Math.round(scanMs))} ms` : '--';
        const oneWireDuration = Number.isFinite(oneWireScanMs) ? `${Math.max(0, Math.round(oneWireScanMs))} ms` : '--';
        status.textContent = payload?.simulated
            ? `Tryb DEV: symulowane magistrale. I2C ${i2cDuration}, OneWire ${oneWireDuration}.`
            : `Diagnostyka sprzętowa zakończona. I2C ${i2cDuration}, OneWire ${oneWireDuration}.`;
        status.dataset.tone = devices.length || oneWireDevices.length ? 'ok' : 'warn';
    }

    window.lastI2cScanData = { ...payload, devices };
    window.lastBusDiagnosticsData = { ...payload, devices, oneWire: { ...payload.oneWire, devices: oneWireDevices } };
    setCommandStatus(
        'diag-strip-bus',
        `${devices.length} I2C · ${oneWireDevices.length} OneWire`,
        uartActive ? `UART${Number(uart.port)} · ${Number(uart.baud).toLocaleString('pl-PL')} bps` : 'UART nieaktywny',
        devices.length || oneWireDevices.length || uartActive ? 'ok' : 'warn'
    );
}

async function fetchHardwareBusDiagnostics(force = false) {
    if (!isAdminAuthenticated() || busDiagnosticsInFlight) return;
    if (!force && window.lastBusDiagnosticsData && Date.now() - busDiagnosticsLastScanAtMs < 10000) {
        renderHardwareBusDiagnostics(window.lastBusDiagnosticsData);
        return;
    }

    const button = document.getElementById('bus-scan-refresh');
    const status = document.getElementById('bus-scan-status');
    busDiagnosticsInFlight = true;
    if (button) button.disabled = true;
    if (status) {
        status.textContent = 'Skanowanie I2C i OneWire oraz odczyt konfiguracji UART...';
        status.dataset.tone = 'info';
    }
    setCommandStatus('diag-strip-bus', 'Skanowanie...', 'I2C · UART · OneWire', 'info');

    try {
        const token = getAdminTokenForRequest();
        const result = await fetchWithTimeout(
            API_BUS_DIAGNOSTICS,
            {
                cache: 'no-store',
                headers: { 'X-AquaCYD-Session': token }
            },
            API_REQUEST_TIMEOUT_MS,
            (response) => response.json().catch(() => ({}))
        );
        const { response, body: payload } = result;
        if (!response.ok || payload?.ok !== true) {
            if (response.status === 401 || response.status === 403) logoutAdmin();
            throw new Error(payload?.message || `HTTP ${response.status}`);
        }
        busDiagnosticsLastScanAtMs = Date.now();
        renderHardwareBusDiagnostics(payload);
    } catch (error) {
        if (status) {
            status.textContent = `Błąd diagnostyki magistral: ${error.message}`;
            status.dataset.tone = 'danger';
        }
        setCommandStatus('diag-strip-bus', 'Błąd magistral', error.message, 'danger');
    } finally {
        busDiagnosticsInFlight = false;
        if (button) button.disabled = false;
    }
}

window.fetchHardwareBusDiagnostics = fetchHardwareBusDiagnostics;

function normalizeFeedResultCode(code) {
    const raw = String(code || '').trim().toLowerCase();
    if (raw.startsWith('feed_')) {
        return raw.slice(5);
    }
    return raw;
}

function describeFeedResult(code, fallbackMessage = '') {
    switch (normalizeFeedResultCode(code)) {
        case 'ok':
            return {
                kind: 'success',
                title: 'Sukces',
                message: 'Karmienie zakonczylo sie pomyslnie.'
            };
        case 'busy':
            return {
                kind: 'error',
                title: 'Karmnik zajety',
                message: 'Poprzednie karmienie nadal trwa. Sprobuj ponownie za chwile.'
            };
        case 'sensor_not_ok':
            return {
                kind: 'error',
                title: 'Blad sensora',
                message: 'Sensor polozenia nie potwierdzil poprawnego cyklu karmienia.'
            };
        case 'timeout':
            return {
                kind: 'error',
                title: 'Timeout karmienia',
                message: 'Sterownik nie otrzymal potwierdzenia z sensora w oczekiwanym czasie.'
            };
        default:
            return {
                kind: 'error',
                title: 'Blad karmienia',
                message: fallbackMessage || 'Nie udalo sie uruchomic karmnika.'
            };
    }
}

function hideFeedModal() {
    const modal = document.getElementById('feed-modal');
    if (feedModalHideTimer) {
        clearTimeout(feedModalHideTimer);
        feedModalHideTimer = null;
    }
    if (modal) {
        modal.style.display = 'none';
        modal.setAttribute('aria-hidden', 'true');
    }
}

function showFeedModalState(kind, title, message, autoHideMs = 0) {
    const modal = document.getElementById('feed-modal');
    const icon = document.getElementById('modal-icon');
    const text = document.getElementById('modal-text');
    const subtext = document.getElementById('modal-subtext');

    if (!modal || !icon || !text || !subtext) {
        return;
    }

    if (feedModalHideTimer) {
        clearTimeout(feedModalHideTimer);
        feedModalHideTimer = null;
    }

    modal.style.display = 'flex';
    modal.setAttribute('aria-hidden', 'false');
    modal.setAttribute('aria-live', kind === 'error' ? 'assertive' : 'polite');
    text.textContent = title;
    subtext.textContent = message;

    if (kind === 'success') {
        icon.className = 'fa-solid fa-check-circle fa-2xl';
        icon.style.color = 'var(--success-color)';
    } else if (kind === 'error') {
        icon.className = 'fa-solid fa-triangle-exclamation fa-2xl';
        icon.style.color = '#ef4444';
    } else {
        icon.className = 'fa-solid fa-spinner fa-spin fa-2xl';
        icon.style.color = 'var(--accent-cyan)';
    }
    renderLocalIcon(icon);

    if (autoHideMs > 0) {
        feedModalHideTimer = setTimeout(() => {
            hideFeedModal();
        }, autoHideMs);
    }
}

function resetFeedActionState() {
    feedActionState.awaitingResponse = false;
    feedActionState.awaitingCompletion = false;
    feedActionState.sawActive = false;
    feedActionState.startedAtMs = 0;
    feedActionState.baselineLastFeedEpoch = 0;
    feedActionState.baselineLastResult = '';
}

function createSvgEl(tag, attrs = {}) {
    const el = document.createElementNS('http://www.w3.org/2000/svg', tag);
    Object.entries(attrs).forEach(([key, value]) => {
        if (value !== undefined && value !== null) {
            el.setAttribute(key, String(value));
        }
    });
    return el;
}

function buildActiveBadge(iconClass, label, tone, tier = '') {
    const tierClass = tier ? ` tier-${tier}` : '';
    const tierAttr = tier ? ` data-tier="${escapeHtml(tier)}"` : '';
    return `
        <div class="status-badge status-badge-${tone}${tierClass}"${tierAttr}>
            ${getLocalIconMarkup(iconClass)}
            <span>${escapeHtml(label)}</span>
        </div>`;
}

function buildBackendConnectionBadge() {
    const connected = Boolean(window.backendConnected);
    const state = connected ? 'online' : 'offline';
    const tone = connected ? 'success' : 'danger';
    const label = connected ? 'Sterownik online' : 'Sterownik offline';
    const ariaLabel = connected
        ? 'Stan połączenia: sterownik odpowiada'
        : 'Stan połączenia: brak odpowiedzi sterownika, trwa ponawianie';

    return `
        <div id="backend-connection-status" class="status-badge status-badge-${tone} backend-connection-status" data-state="${state}" aria-label="${ariaLabel}">
            <span id="backend-connection-label">${label}</span>
        </div>`;
}

function buildModuleBadge(iconClass, label, active, activeTone) {
    return buildActiveBadge(iconClass, label, active ? activeTone : 'muted');
}

function normalizeDigitalSensorState(value) {
    if (typeof value === 'boolean') {
        return { valid: true, active: value };
    }

    if (typeof value === 'number' && Number.isFinite(value)) {
        return { valid: true, active: value !== 0 };
    }

    if (typeof value === 'string') {
        const normalized = value.trim().toLowerCase();
        if (['1', 'true', 'on', 'high', 'active', 'alarm', 'detected'].includes(normalized)) {
            return { valid: true, active: true };
        }
        if (['0', 'false', 'off', 'low', 'inactive', 'ok', 'dry', 'none'].includes(normalized)) {
            return { valid: true, active: false };
        }
    }

    return { valid: false, active: false };
}

function buildLeakSafetyBadge(data) {
    const modules = data.modules || {};
    const sensors = data.sensors || {};
    const leakEnabledState = normalizeDigitalSensorState(modules.leak_enabled);
    const leakEnabled = leakEnabledState.valid ? leakEnabledState.active : !!modules.leak_enabled;

    if (!leakEnabled) {
        return buildActiveBadge('fa-shield-halved', 'Wyciek: OFF', 'danger', 'safety');
    }

    const leakState = normalizeDigitalSensorState(sensors.leak_detected);
    if (!leakState.valid) {
        return buildActiveBadge('fa-triangle-exclamation', 'Wyciek: brak MCP', 'danger', 'safety');
    }

    return buildActiveBadge(
        leakState.active ? 'fa-triangle-exclamation' : 'fa-shield-halved',
        leakState.active ? 'Wyciek: ALARM' : 'Wyciek: OK',
        'danger',
        'safety'
    );
}

function getSystemSafetyAndMode(data) {
    const sensors = data.sensors || {};
    const temperature = data.temperature || {};
    const schedule = data.schedule || {};
    const eco = data.eco || {};
    
    const isConnected = window.backendConnected;
    if (!isConnected) {
        return {
            safety: 'offline',
            safetyLabel: 'BRAK POŁĄCZENIA',
            safetyDesc: 'Nie można połączyć się ze sterownikiem ESP32.',
            safetyIcon: 'fa-triangle-exclamation',
            mode: 'OFFLINE',
            modeLabel: 'OFFLINE'
        };
    }
    
    const criticalCount = Math.max(
        cachedLogs?.critical?.length || 0,
        Math.trunc(Number(cachedLogs?.counts?.critical) || 0)
    );
    const leakState = normalizeDigitalSensorState(sensors.leak_detected);
    const isLeak = leakState.valid && leakState.active;
    
    const tempValue = toFiniteNumber(temperature.current ?? sensors.temp_c);
    const tempValid = isSensorValid(data, 'temp', tempValue) && isValidTemperature(tempValue);
    
    const mcpOk = sensors.mcp_ok ?? data.system?.mcpConnected ?? data.system?.i2cConnected ?? true;
    
    let isAlarm = criticalCount > 0 || isLeak || !tempValid || mcpOk === false;
    let alarmMsg = '';
    if (isLeak) alarmMsg = 'WYKRYTO WYCIEK WODY! Zawory CO2 i dolewki zablokowane.';
    else if (!tempValid) alarmMsg = 'Błąd czujnika temperatury DS18B20! Grzałka wyłączona.';
    else if (mcpOk === false) alarmMsg = 'Błąd ekspandera I2C (MCP23017/PCF8574)!';
    else if (criticalCount > 0) alarmMsg = 'Aktywne alarmy krytyczne w logach.';

    if (isAlarm) {
        return {
            safety: 'alarm',
            safetyLabel: 'ALARM KRYTYCZNY',
            safetyDesc: alarmMsg,
            safetyIcon: 'fa-circle-exclamation',
            mode: 'EMERGENCY',
            modeLabel: 'AWARIA'
        };
    }
    
    const isService = data.config?.dev_mode;
    if (isService) {
        return {
            safety: 'service',
            safetyLabel: 'TRYB SERWISOWY',
            safetyDesc: 'Urządzenie jest w trybie serwisowym (DEV). Automatyka może być ograniczona.',
            safetyIcon: 'fa-screwdriver-wrench',
            mode: 'SERVICE',
            modeLabel: 'SERWIS'
        };
    }
    
    const isFeeding = data.feeding?.active;
    if (isFeeding) {
        return {
            safety: 'warn',
            safetyLabel: 'KARMIENIE W TOKU',
            safetyDesc: 'Trwa dozowanie pokarmu. Filtry i napowietrzanie wyciszone.',
            safetyIcon: 'fa-fish',
            mode: 'FEEDING',
            modeLabel: 'KARMIENIE'
        };
    }
    
    const batteryPercent = toFiniteNumber(data.battery?.percent);
    const isLowBattery = batteryPercent !== null && batteryPercent <= 15;
    
    const targetTemp = toFiniteNumber(temperature.target);
    const hyst = toFiniteNumber(temperature.hysteresis);
    let isTempWarn = false;
    if (tempValid && targetTemp !== null && hyst !== null) {
        if (Math.abs(tempValue - targetTemp) > 2.0) {
            isTempWarn = true;
        }
    }
    
    const lightMode = modeValue(schedule.lightMode);
    const filterMode = modeValue(schedule.filterMode);
    const airMode = modeValue(schedule.airMode);
    const heaterMode = modeValue(schedule.heaterMode);
    const isManualMode = lightMode !== 0 || filterMode !== 0 || airMode !== 0 || heaterMode !== 0;

    if (isLowBattery || isTempWarn) {
        let warnMsg = isLowBattery ? 'Niski poziom baterii RTC.' : 'Temperatura wody poza bezpiecznym zakresem.';
        return {
            safety: 'warn',
            safetyLabel: 'OSTRZEŻENIE',
            safetyDesc: warnMsg,
            safetyIcon: 'fa-triangle-exclamation',
            mode: isManualMode ? 'MANUAL' : (eco.enabled ? 'ECO' : 'AUTO'),
            modeLabel: isManualMode ? 'RĘCZNY' : (eco.enabled ? 'ECO' : 'AUTO')
        };
    }
    
    const isNight = schedule.lightMode === 0 && schedule.dayStartHour !== undefined && schedule.dayEndHour !== undefined && (() => {
        const now = getCurrentClockDate();
        const hr = now.getHours();
        const min = now.getMinutes();
        const start = schedule.dayStartHour * 60 + schedule.dayStartMin;
        const end = schedule.dayEndHour * 60 + schedule.dayEndMin;
        const current = hr * 60 + min;
        if (start < end) {
            return current < start || current > end;
        } else {
            return current < start && current > end;
        }
    })();
    
    const activeMode = isManualMode ? 'MANUAL' : (isNight ? 'NIGHT' : (eco.enabled ? 'ECO' : 'AUTO'));
    const activeModeLabel = isManualMode ? 'RĘCZNY' : (isNight ? 'NOCNY' : (eco.enabled ? 'ECO' : 'AUTO'));
    
    return {
        safety: 'ok',
        safetyLabel: 'SYSTEM BEZPIECZNY',
        safetyDesc: 'Wszystkie parametry w normie. Automatyka działa poprawnie.',
        safetyIcon: 'fa-circle-check',
        mode: activeMode,
        modeLabel: activeModeLabel
    };
}

function renderTopbarActiveModules(data) {
    const container = document.getElementById('topbar-active-list');
    if (!container) return;

    const network = data.network || {};
    const safetyInfo = getSystemSafetyAndMode(data);

    let safetyTone = 'success';
    if (safetyInfo.safety === 'warn') safetyTone = 'warn';
    else if (safetyInfo.safety === 'alarm') safetyTone = 'danger';
    else if (safetyInfo.safety === 'service') safetyTone = 'info';
    else if (safetyInfo.safety === 'offline') safetyTone = 'muted';

    const safetyBadge = buildActiveBadge(safetyInfo.safetyIcon, safetyInfo.safetyLabel, safetyTone);
    
    let modeTone = 'info';
    if (safetyInfo.mode === 'MANUAL') modeTone = 'warn';
    else if (safetyInfo.mode === 'FEEDING') modeTone = 'success';
    else if (safetyInfo.mode === 'EMERGENCY') modeTone = 'danger';
    else if (safetyInfo.mode === 'NIGHT') modeTone = 'muted';
    else if (safetyInfo.mode === 'ECO') modeTone = 'success';
    
    const modeIcon = safetyInfo.mode === 'AUTO' ? 'fa-robot' :
                     safetyInfo.mode === 'MANUAL' ? 'fa-sliders' :
                     safetyInfo.mode === 'SERVICE' ? 'fa-screwdriver-wrench' :
                     safetyInfo.mode === 'FEEDING' ? 'fa-fish' :
                     safetyInfo.mode === 'EMERGENCY' ? 'fa-triangle-exclamation' :
                     safetyInfo.mode === 'NIGHT' ? 'fa-moon' : 'fa-leaf';

    const modeBadge = buildActiveBadge(modeIcon, `Tryb: ${safetyInfo.modeLabel}`, modeTone);

    const wifiBadge = network.staConnected
        ? buildModuleBadge('fa-wifi', 'STA', true, 'success')
        : (network.apMode ? buildModuleBadge('fa-satellite-dish', 'AP', true, 'success') : buildActiveBadge('fa-wifi', 'Offline', 'muted'));

    const markup = [
        buildBackendConnectionBadge(),
        safetyBadge,
        modeBadge,
        wifiBadge
    ].join('');
    if (markup !== lastTopbarMarkup) {
        container.innerHTML = markup;
        lastTopbarMarkup = markup;
    }
}

function renderTemperatureCard(temperature) {
    const currentValue = isValidTemperature(temperature?.current)
        ? Number(temperature.current).toFixed(1)
        : '--.-';

    setText('dashboard-temp-current', currentValue);
    setText('dashboard-temp-target', formatTemperature(temperature?.target));
    setText('dashboard-temp-hysteresis', formatTemperature(temperature?.hysteresis));
}

function renderBatteryWidgets(battery) {
    const voltage = toFiniteNumber(battery?.voltage);
    const percentRaw = toFiniteNumber(battery?.percent);
    const percent = percentRaw === null ? null : clamp(Math.round(percentRaw), 0, 100);
    const voltageText = voltage === null ? '--.--V' : `${voltage.toFixed(2)}V`;

    setText('rtc-battery', voltageText);
    setText('dashboard-battery-voltage', voltageText);
    setText('dashboard-battery-percent', percent === null ? '--' : String(percent));
    setText('power-battery-voltage', voltageText);
    setText('power-battery-percent', percent === null ? '--' : String(percent));

    const fill = document.getElementById('dashboard-battery-fill');
    if (fill) {
        fill.style.width = `${percent === null ? 0 : percent}%`;
        if (percent !== null && percent <= 15) {
            fill.style.background = 'linear-gradient(90deg, #dc2626, #f87171)';
        } else if (percent !== null && percent <= 35) {
            fill.style.background = 'linear-gradient(90deg, #f59e0b, #fbbf24)';
        } else {
            fill.style.background = 'linear-gradient(90deg, #10b981, #34d399)';
        }
    }

    const powerFill = document.getElementById('power-battery-fill');
    if (powerFill) {
        powerFill.style.width = `${percent === null ? 0 : percent}%`;
        if (percent !== null && percent <= 15) {
            powerFill.style.background = 'linear-gradient(90deg, #dc2626, #f87171)';
        } else if (percent !== null && percent <= 35) {
            powerFill.style.background = 'linear-gradient(90deg, #f59e0b, #fbbf24)';
        } else {
            powerFill.style.background = 'linear-gradient(90deg, #10b981, #34d399)';
        }
    }

    let stateLabel = 'Brak pomiaru';
    if (percent !== null) {
        if (percent <= 15) {
            stateLabel = 'Niski poziom';
        } else if (percent <= 35) {
            stateLabel = 'Warto obserwowac';
        } else {
            stateLabel = 'Poziom stabilny';
        }
    }
    setText('dashboard-battery-state', stateLabel);
    setText('power-battery-state', stateLabel);

    // Dynamiczne przelaczanie widoku Bateria vs Zasilanie sieciowe
    const pill = document.querySelector('.battery-pill');
    const dashBattery = document.getElementById('dashboard-battery-card');
    const dashPower = document.getElementById('dashboard-power-card');
    const powerBattery = document.getElementById('power-battery-card');
    const powerMains = document.getElementById('power-mains-card');

    if (voltage === null && percentRaw === null) {
        if (pill) pill.style.display = 'none';
        if (dashBattery) dashBattery.style.display = 'none';
        if (dashPower) {
            dashPower.style.display = 'flex';
            const system = lastStatusData?.system || {};
            const uptime = toFiniteNumber(system.uptime);
            if (uptime !== null) {
                document.getElementById('dashboard-power-uptime').textContent = formatDuration(uptime);
            }
            const powerMode = system.powerMode === 'modem_sleep' ? 'Modem Sleep (ECO)' : 'Ciągły (Normalny)';
            document.getElementById('dashboard-power-mode').textContent = powerMode;
            const network = lastStatusData?.network || {};
            const wifiStatus = network.staConnected ? 'STA Połączono' : (network.apMode ? 'AP Aktywne' : 'Wyłączone');
            document.getElementById('dashboard-power-wifi-status').textContent = wifiStatus;
        }
        if (powerBattery) powerBattery.style.display = 'none';
        if (powerMains) {
            powerMains.style.display = 'flex';
            const system = lastStatusData?.system || {};
            const uptime = toFiniteNumber(system.uptime);
            if (uptime !== null) {
                document.getElementById('power-mains-uptime').textContent = formatDuration(uptime);
            }
            const powerMode = system.powerMode === 'modem_sleep' ? 'Modem Sleep (ECO)' : 'Ciągły (Normalny)';
            document.getElementById('power-mains-mode').textContent = powerMode;
            const network = lastStatusData?.network || {};
            const wifiStatus = network.staConnected ? 'STA Połączono' : (network.apMode ? 'AP Aktywne' : 'Wyłączone');
            document.getElementById('power-mains-wifi-status').textContent = wifiStatus;
        }
    } else {
        if (pill) pill.style.display = 'flex';
        if (dashBattery) dashBattery.style.display = 'flex';
        if (dashPower) dashPower.style.display = 'none';
        if (powerBattery) powerBattery.style.display = 'flex';
        if (powerMains) powerMains.style.display = 'none';
    }
}

function renderNetworkCard(network) {
    const card = document.getElementById('network-card');
    if (!card) return;

    const staConnected = !!network?.staConnected;
    const apMode = !!network?.apMode;
    const staConnecting = !!network?.staConnecting;
    const serviceMode = !!network?.serviceMode;
    const serviceModePending = !!network?.serviceModePending;
    const retryCooldownMs = toFiniteNumber(network?.staRetryCooldownMs);
    const statusText = serviceModePending
        ? 'START WIFI...'
        : (retryCooldownMs !== null && retryCooldownMs > 0
        ? 'ROUTER PELNY'
        : (staConnecting
        ? 'LACZENIE...'
        : (staConnected && apMode
            ? 'AP + STA'
            : (staConnected
                ? 'STA ONLINE'
                : (apMode ? 'TRYB AP' : (serviceMode ? 'SESJA WIFI' : 'RADIO OFF'))))));
    const ssidText = staConnected || staConnecting
        ? (network?.staSsid || network?.configuredStaSsid || '-')
        : (apMode ? (network?.configuredApSsid || network?.ssid || '-') : '-');

    setText('network-status', statusText);
    setText('network-ssid', ssidText);
    setText(
        'network-last-seen',
        retryCooldownMs !== null && retryCooldownMs > 0
            ? `Ponowienie za ${formatCountdown(retryCooldownMs)}`
            : formatEpoch(network?.staLastConnectedEpoch, { fallback: 'Brak historii' })
    );

    card.classList.remove('network-online', 'network-aponly', 'network-offline', 'network-connecting');
    const transitional = serviceModePending || staConnecting ||
        (serviceMode && !staConnected && !apMode);
    card.classList.add(retryCooldownMs !== null && retryCooldownMs > 0
        ? 'network-offline'
        : (transitional
        ? 'network-connecting'
        : (staConnected ? 'network-online' : (apMode ? 'network-aponly' : 'network-offline'))));
}

function isHeaterAutomationEnabled(data) {
    const mode = Number(data?.schedule?.heaterMode ?? data?.temperature?.heaterMode);
    if (Number.isFinite(mode)) {
        return mode !== 1;
    }
    return data?.modules?.heater_enabled !== false;
}

function relayQuickButtonLabel(active) {
    return active ? 'Wyłącz' : 'Włącz';
}

const RELAY_QUICK_ACTIONS = {
    light: {
        buttonId: 'relay-light-toggle',
        actionName: 'set_light1',
        isActive: (data) => !!(data?.relays?.light1 ?? data?.relays?.light),
        buildPayload: (active) => ({ state: active ? '0' : '1' })
    },
    filter: {
        buttonId: 'relay-filter-toggle',
        actionName: 'set_filter',
        isActive: (data) => !!data?.relays?.pump,
        buildPayload: (active) => ({ state: active ? '0' : '1' })
    },
    plant: {
        buttonId: 'relay-plant-toggle',
        actionName: 'set_light2',
        isActive: (data) => !!(data?.relays?.light2 ?? data?.relays?.plantLight),
        buildPayload: (active) => ({ state: active ? '0' : '1' })
    },
    heater: {
        buttonId: 'relay-heater-toggle',
        actionName: 'set_heater',
        isActive: isHeaterAutomationEnabled,
        buildPayload: (active) => ({ state: active ? '0' : '1' })
    },
    aeration: {
        buttonId: 'relay-aeration-toggle',
        actionName: 'set_aeration',
        isActive: (data) => !!data?.relays?.aeration,
        buildPayload: (active) => ({ state: active ? '0' : '1' })
    }
};

async function toggleRelayQuickAction(kind) {
    const config = RELAY_QUICK_ACTIONS[kind];
    if (!config) return;

    const relayValue = config.isActive(lastStatusData || {});
    const payload = config.buildPayload(relayValue);
    const idleLabel = relayQuickButtonLabel(relayValue);
    const button = document.getElementById(config.buttonId);
    if (!button || button.dataset.busy === '1') return;

    button.dataset.busy = '1';
    button.disabled = true;
    button.textContent = 'Zapisywanie...';

    try {
        await sendAction(config.actionName, payload);
        await fetchStatus(true);
        await fetchLogs(true);
    } catch (error) {
        button.textContent = 'Błąd';
        button.title = describeRequestError(error);
        setTimeout(() => {
            button.textContent = idleLabel;
            button.title = '';
        }, 1600);
    } finally {
        button.dataset.busy = '0';
        button.disabled = false;
        if (!button.title) {
            button.textContent = relayQuickButtonLabel(config.isActive(lastStatusData || {}));
        }
    }
}

function modeValue(value) {
    const num = Number(value);
    return Number.isFinite(num) ? num : 0;
}

function dashboardModeToUi(value) {
    if (typeof scheduleModeToUi === 'function') {
        return scheduleModeToUi(value);
    }
    if (typeof value === 'string') {
        const normalized = value.trim().toLowerCase();
        if (normalized === 'always_on') return 'zawsze_wlaczone';
        if (normalized === 'always_off') return 'zawsze_wylaczone';
        return 'harmonogram';
    }
    return modeValue(value) === 1
        ? 'zawsze_wlaczone'
        : (modeValue(value) === 2 ? 'zawsze_wylaczone' : 'harmonogram');
}

function dashboardParseClock(value, fallbackHour, fallbackMinute) {
    const normalizer = typeof normalizeTimeInputValue === 'function'
        ? normalizeTimeInputValue
        : (raw) => {
            const match = String(raw || '').match(/^(\d{1,2}):(\d{1,2})$/);
            if (!match) return `${formatTwoDigits(fallbackHour)}:${formatTwoDigits(fallbackMinute)}`;
            return `${formatTwoDigits(clamp(Number(match[1]), 0, 23))}:${formatTwoDigits(clamp(Number(match[2]), 0, 59))}`;
        };
    const text = normalizer(value || formatTime(fallbackHour, fallbackMinute));
    const [hour, minute] = text.split(':').map(Number);
    return { hour, minute, text };
}

function dashboardScheduleRange(data, key, legacy, fallback) {
    const flat = data.schedule || {};
    const nested = data.schedules?.[key];
    const legacyStartOk = toFiniteNumber(flat[legacy.startHour]) !== null && toFiniteNumber(flat[legacy.startMin]) !== null;
    const legacyEndOk = toFiniteNumber(flat[legacy.endHour]) !== null && toFiniteNumber(flat[legacy.endMin]) !== null;
    const startSource = nested?.start || (legacyStartOk ? formatTime(flat[legacy.startHour], flat[legacy.startMin]) : null);
    const endSource = nested?.end || (legacyEndOk ? formatTime(flat[legacy.endHour], flat[legacy.endMin]) : null);
    const start = dashboardParseClock(startSource, fallback.startHour, fallback.startMin);
    const end = dashboardParseClock(endSource, fallback.endHour, fallback.endMin);
    const mode = dashboardModeToUi(nested?.mode !== undefined ? nested.mode : flat[legacy.mode]);

    return {
        mode,
        start,
        end,
        label: `${start.text} - ${end.text}`
    };
}

function describeDashboardRange(range, labels = {}) {
    if (range.mode === 'zawsze_wlaczone') {
        return labels.on || 'Zawsze włączone';
    }
    if (range.mode === 'zawsze_wylaczone') {
        return labels.off || 'Wyłączone';
    }
    return range.label;
}

function getDashboardWorkRange(data) {
    return dashboardScheduleRange(
        data,
        'light',
        {
            mode: 'lightMode',
            startHour: 'dayStartHour',
            startMin: 'dayStartMin',
            endHour: 'dayEndHour',
            endMin: 'dayEndMin'
        },
        { startHour: 10, startMin: 0, endHour: 21, endMin: 0 }
    );
}

function dashboardAquaelProfileCode(value) {
    const normalized = String(value ?? '').trim().toLowerCase();
    if (normalized === '1' || normalized === 'daybreak' || normalized === 'dawn' || normalized === 'sunrise') {
        return 'daybreak';
    }
    if (normalized === '2' || normalized === 'night' || normalized === 'moon') {
        return 'night';
    }
    return 'day';
}

function dashboardAquaelProfileLabel(data, key, legacyProfileKey) {
    const flat = data.schedule || {};
    const nested = data.schedules?.[key];
    const code = dashboardAquaelProfileCode(
        nested?.profile ?? nested?.profileLabel ?? flat[`${legacyProfileKey}Name`] ?? flat[legacyProfileKey]
    );
    if (code === 'daybreak') return 'DAYBREAK';
    if (code === 'night') return 'NIGHT';
    return 'DAY';
}

function renderWaterQualityCard(data) {
    const sensors = data.sensors || {};
    const modules = data.modules || {};
    const ph = toFiniteNumber(sensors.ph);
    let ec = toFiniteNumber(sensors.ec);
    const ldr = toFiniteNumber(sensors.ldr);

    // Jesli EC jest null, ale modul EC jest wlaczony i jest to tryb DEV, symulujemy wartosc
    if (ec === null && modules.ec_enabled && data.config?.dev_mode) {
        ec = 420 + Math.sin(Date.now() / 60000) * 15;
    }

    setText('dashboard-ph-current', ph === null ? '--' : ph.toFixed(2));
    setText('dashboard-ec-current', ec === null ? '--' : String(Math.round(ec)));
    setText('dashboard-ldr-current', ldr === null ? '--' : String(Math.round(ldr)));

    const enabled = [
        modules.ph_sensor_enabled ? 'pH' : '',
        modules.ec_enabled ? 'EC' : '',
        modules.water_level_enabled ? 'poziom' : '',
        modules.leak_enabled ? 'wyciek' : '',
        modules.flow_enabled ? 'przepływ' : ''
    ].filter(Boolean);
    setText(
        'dashboard-sensor-state',
        enabled.length ? `Aktywne: ${enabled.join(', ')}` : 'Czujniki dodatkowe są wyłączone lub bez statusu.'
    );
}

function isSensorValid(data, name, value) {
    const sensors = data?.sensors || {};
    const flagName = `${name}_valid`;
    if (Object.prototype.hasOwnProperty.call(sensors, flagName)) {
        return !!sensors[flagName];
    }
    return toFiniteNumber(value) !== null;
}

function countActiveRelays(relays) {
    return [
        relays?.light1 ?? relays?.light,
        relays?.light2 ?? relays?.plantLight,
        relays?.pump,
        relays?.heater,
        relays?.aeration
    ].filter(Boolean).length;
}

function commandCountLabel(count, singular, plural) {
    const value = Math.max(0, Math.trunc(Number(count) || 0));
    return `${value} ${value === 1 ? singular : plural}`;
}

function getEcoFlag(eco, camelName, snakeName) {
    if (eco && Object.prototype.hasOwnProperty.call(eco, camelName)) {
        return !!eco[camelName];
    }
    return !!eco?.[snakeName];
}

function getEcoNumber(eco, camelName, snakeName) {
    const direct = toFiniteNumber(eco?.[camelName]);
    if (direct !== null) return direct;
    return toFiniteNumber(eco?.[snakeName]);
}

function describeSleepBlockers(data) {
    const ecoBlockers = Array.isArray(data?.eco?.blockers) ? data.eco.blockers : [];
    const systemBlockers = Array.isArray(data?.system?.sleepBlockers) ? data.system.sleepBlockers : [];
    const source = ecoBlockers.length ? ecoBlockers : systemBlockers;
    return source
        .map((item) => (typeof formatSleepBlockerLabel === 'function' ? formatSleepBlockerLabel(item) : item))
        .filter(Boolean);
}

function renderStatusCommandStrips(data) {
    const safetyInfo = getSystemSafetyAndMode(data);
    const safetyBar = document.getElementById('global-safety-bar');
    const safetyIcon = document.getElementById('global-safety-icon');
    const safetyTitle = document.getElementById('global-safety-title');
    const safetyDesc = document.getElementById('global-safety-desc');
    
    if (safetyBar) {
        safetyBar.className = `global-safety-bar status-${safetyInfo.safety}`;
        if (safetyIcon && safetyIcon.dataset.icon !== safetyInfo.safetyIcon) {
            const iconMarkup = getLocalIconMarkup ? getLocalIconMarkup(safetyInfo.safetyIcon) : `<i class="fa-solid ${safetyInfo.safetyIcon}"></i>`;
            safetyIcon.innerHTML = iconMarkup;
            safetyIcon.dataset.icon = safetyInfo.safetyIcon;
        }
        if (safetyTitle) {
            safetyTitle.textContent = `Stan systemu: ${safetyInfo.safetyLabel}`;
        }
        if (safetyDesc) {
            safetyDesc.textContent = safetyInfo.safetyDesc;
        }
    }

    const sensors = data.sensors || {};
    const temperature = data.temperature || {};
    const relays = data.relays || {};
    const network = data.network || {};
    const system = data.system || {};
    const firmware = data.firmware || {};
    const eco = data.eco || {};
    const workRange = getDashboardWorkRange(data);
    const activeRelays = countActiveRelays(relays);
    const criticalCount = Math.max(
        cachedLogs?.critical?.length || 0,
        Math.trunc(Number(cachedLogs?.counts?.critical) || 0)
    );

    const tempValue = toFiniteNumber(temperature.current ?? sensors.temp_c);
    const tempValid = isSensorValid(data, 'temp', tempValue) && isValidTemperature(tempValue);
    const phValue = toFiniteNumber(sensors.ph);
    const phValid = isSensorValid(data, 'ph', phValue);
    const ldrValue = toFiniteNumber(sensors.ldr);
    const ldrValid = isSensorValid(data, 'ldr', ldrValue);

    const dashboardTone = criticalCount > 0 ? 'warn' : (tempValid || data.config?.dev_mode ? 'ok' : 'warn');
    setCommandStatus(
        'dashboard-strip-state',
        data.config?.dev_mode ? 'Tryb DEV' : (tempValid ? 'Stabilny' : 'Bez temperatury'),
        tempValid ? `${formatTemperature(tempValue)} / ${activeRelays} wyjsc aktywnych` : 'Brak realnego czujnika temperatury',
        dashboardTone
    );
    setCommandStatus(
        'dashboard-strip-window',
        describeDashboardRange(workRange, { on: 'Cala doba', off: 'OFF' }),
        'Cykl dla swiatla glownego i grzalki',
        'info'
    );
    setCommandStatus(
        'dashboard-strip-risk',
        criticalCount > 0 ? commandCountLabel(criticalCount, 'alarm', 'alarmow') : 'Brak alarmow',
        `${phValid ? 'pH OK' : 'pH brak'} / ${ldrValid ? 'LDR OK' : 'LDR brak'}`,
        criticalCount > 0 ? 'warn' : 'ok'
    );

    const history = Array.isArray(temperature.history) ? temperature.history : [];
    const capacity = Math.max(0, Math.trunc(Number(temperature.historyCapacity) || 0));
    setCommandStatus(
        'measure-strip-live',
        tempValid ? formatTemperature(tempValue) : '--.-C',
        tempValid ? 'Odczyt pochodzi ze sterownika' : 'Brak realnej probki temperatury',
        tempValid ? 'ok' : 'warn'
    );
    setCommandStatus(
        'measure-strip-history',
        commandCountLabel(history.length, 'probka', 'probek'),
        capacity > 0 ? `Pojemnosc bufora: ${capacity}` : 'Historia zalezy od RAM sterownika',
        history.length > 0 ? 'ok' : 'neutral'
    );
    setCommandStatus(
        'measure-strip-export',
        history.length > 0 ? 'CSV + AQBIN' : 'CSV pusty',
        'CSV teraz, archiwa miesieczne na SD',
        history.length > 0 ? 'ok' : 'neutral'
    );

    const versionText = normalizeFirmwareVersion(firmware.version);
    setCommandStatus(
        'diag-strip-firmware',
        versionText,
        firmware.buildDate && firmware.buildTime ? `${firmware.buildDate} ${firmware.buildTime}` : 'Build dev',
        'info'
    );
    const freeHeap = toFiniteNumber(system.freeHeap ?? data.heap_free);
    const largestHeap = toFiniteNumber(system.largestHeap ?? data.heap_largest);
    const freeHeapKb = freeHeap === null ? '--' : `${(freeHeap / 1024).toFixed(1)} KB`;
    const largestHeapKb = largestHeap === null ? '--' : `${(largestHeap / 1024).toFixed(1)} KB`;
    setCommandStatus(
        'diag-strip-heap',
        freeHeapKb,
        `Najwiekszy blok: ${largestHeapKb}`,
        freeHeap !== null && freeHeap < 32000 ? 'warn' : 'ok'
    );
    const busDiagnostics = window.lastBusDiagnosticsData;
    const i2cDevices = Array.isArray(busDiagnostics?.devices) ? busDiagnostics.devices : null;
    const oneWireDevices = Array.isArray(busDiagnostics?.oneWire?.devices) ? busDiagnostics.oneWire.devices : null;
    const uartPort = Array.isArray(busDiagnostics?.uart?.ports) ? busDiagnostics.uart.ports[0] : null;
    const mcpResponding = sensors.mcp_ok === true || system.mcpConnected === true;
    setCommandStatus(
        'diag-strip-bus',
        i2cDevices && oneWireDevices
            ? `${i2cDevices.length} I2C · ${oneWireDevices.length} OneWire`
            : (mcpResponding ? 'MCP 0x20' : 'Nie skanowano'),
        uartPort?.active
            ? `UART${Number(uartPort.port)} · ${Number(uartPort.baud).toLocaleString('pl-PL')} bps`
            : (mcpResponding ? 'Ekspander odpowiada; pełna diagnostyka po otwarciu zakładki' : 'I2C · UART · OneWire'),
        i2cDevices && oneWireDevices ? 'ok' : (mcpResponding ? 'ok' : 'neutral')
    );

    const batteryPercent = toFiniteNumber(data.battery?.percent);
    const batteryVoltage = toFiniteNumber(data.battery?.voltage);
    setCommandStatus(
        'power-strip-battery',
        batteryPercent === null ? '--%' : `${Math.round(batteryPercent)}%`,
        batteryVoltage === null ? 'Brak pomiaru napiecia' : `${batteryVoltage.toFixed(2)}V`,
        batteryPercent === null ? 'neutral' : (batteryPercent <= 15 ? 'danger' : (batteryPercent <= 35 ? 'warn' : 'ok'))
    );
    const deepReady = getEcoFlag(eco, 'deepReady', 'deep_ready');
    const rtcReady = getEcoFlag(eco, 'rtcReady', 'rtc_ready');
    const wakeAfterSec = getEcoNumber(eco, 'wakeAfterSec', 'wake_after_sec');
    setCommandStatus(
        'power-strip-eco',
        deepReady ? 'Gotowe' : (rtcReady ? 'Zablokowane' : 'RTC brak'),
        wakeAfterSec && wakeAfterSec > 0 ? `Plan pobudki za ${formatDuration(wakeAfterSec)}` : 'Deep sleep wymaga poprawnego czasu i braku blokad',
        deepReady ? 'ok' : (rtcReady ? 'warn' : 'danger')
    );
    const blockerLabels = describeSleepBlockers(data);
    setCommandStatus(
        'power-strip-blockers',
        blockerLabels.length ? commandCountLabel(blockerLabels.length, 'blokada', 'blokad') : 'Brak',
        blockerLabels.length ? blockerLabels.join(', ') : 'Uklad moze przejsc w sleep po spelnieniu okna ECO',
        blockerLabels.length ? 'warn' : 'ok'
    );

    setCommandStatus(
        'ota-strip-link',
        network.staConnected ? 'STA online' : (network.apMode ? 'AP online' : 'Offline'),
        data.portal_domain || data.portal_url || 'Brak adresu portalu',
        network.staConnected || network.apMode ? 'ok' : 'warn'
    );
}

function setRelayCard(relayId, state, meta) {
    const card = document.getElementById(`relay-${relayId}`);
    const stateEl = document.getElementById(`relay-${relayId}-state`);
    const metaEl = document.getElementById(`relay-${relayId}-meta`);
    if (!card || !metaEl) return;

    card.classList.remove('relay-on', 'relay-off', 'relay-standby');
    card.classList.add(`relay-${state}`);
    if (stateEl) {
        stateEl.textContent = state.toUpperCase();
    }
    metaEl.textContent = meta;
}

const RELAY_FUNCTIONS_INFO = {
    none: { label: "Brak / nieużywany", icon: "fa-xmark" },
    filter: { label: "Filtr", icon: "fa-filter" },
    heater: { label: "Grzałka", icon: "fa-temperature-half" },
    main_light: { label: "Świetlówka przednia", icon: "fa-lightbulb" },
    plant_light: { label: "Świetlówka tylna", icon: "fa-lightbulb" },
    aeration: { label: "Napowietrzanie", icon: "fa-wind" },
    co2: { label: "CO2", icon: "fa-cloud" },
    water_dosing: { label: "Dolewka wody (ATO)", icon: "fa-droplet" },
    feeder: { label: "Karmnik", icon: "fa-fish" },
    circulation_pump: { label: "Pompa obiegowa", icon: "fa-rotate-right" },
    uv_lamp: { label: "Lampa UV", icon: "fa-sun" },
    reserve: { label: "Rezerwa", icon: "fa-sliders" },
    custom: { label: "Własna nazwa", icon: "fa-sliders" }
};

function normalizeRelayChannel(value) {
    const channel = Number(value);
    return Number.isInteger(channel) && channel >= 1 && channel <= 8 ? channel : null;
}

function getRelayState(relay, data) {
    const relays = data?.relays || {};
    const channel = normalizeRelayChannel(relay?.channel);
    const func = relay.function;

    // 1. Try channel-based state
    if (channel !== null && relays[`ch${channel}`] !== undefined) {
        return !!relays[`ch${channel}`];
    }
    if (channel !== null && relays[String(channel)] !== undefined) {
        return !!relays[String(channel)];
    }

    // 2. Try function-based state
    if (relays[func] !== undefined) {
        return !!relays[func];
    }

    // 3. Try legacy mapping
    const legacyMap = {
        filter: 'pump',
        heater: 'heater',
        main_light: 'light',
        plant_light: 'plantLight',
        aeration: 'aeration'
    };
    const legacyKey = legacyMap[func];
    if (legacyKey && relays[legacyKey] !== undefined) {
        return !!relays[legacyKey];
    }

    return false;
}

async function toggleDynamicRelayAction(channel) {
    const normalizedChannel = normalizeRelayChannel(channel);
    if (normalizedChannel === null) {
        showToast('Nieprawidłowy kanał', 'Sterownik przesłał numer kanału spoza zakresu 1–8.', 'error', 5000);
        return;
    }
    if (dynamicRelayActionsInFlight.has(normalizedChannel)) {
        showToast('Polecenie w toku', `Kanał ${normalizedChannel} czeka na potwierdzenie sterownika.`, 'warning', 2800);
        return;
    }

    const status = window.lastStatusData || {};
    const config = status.relaysConfig;
    if (!config || !Array.isArray(config.relays)) return;

    const relay = config.relays.find((item) => normalizeRelayChannel(item?.channel) === normalizedChannel);
    if (!relay) return;

    if (relay.manualAllowed === false) {
        showToast(
            'Sterowanie zablokowane',
            'Ręczne przełączenie tego kanału jest zablokowane przez konfigurację bezpieczeństwa.',
            'warning',
            4800
        );
        return;
    }

    const active = getRelayState(relay, status);
    const nextState = active ? 0 : 1;

    // Zabezpieczenie dwufazowe i potwierdzenie
    let confirmMsg = "";
    if (relay.function === 'filter' && active) {
        confirmMsg = "Czy na pewno chcesz wyłączyć filtr? Spowoduje to zatrzymanie obiegu biologicznego w akwarium!";
    } else if (relay.function === 'heater' && !active) {
        confirmMsg = "Czy na pewno chcesz ręcznie włączyć grzałkę? Monitoruj temperaturę wody, aby nie przegrzać zbiornika.";
    } else if (relay.function === 'co2' && !active) {
        confirmMsg = "Czy na pewno chcesz ręcznie włączyć CO2? Zbyt wysokie stężenie może być niebezpieczne dla ryb.";
    } else if (relay.function === 'water_dosing' && !active) {
        confirmMsg = "Czy na pewno chcesz ręcznie włączyć dolewkę wody?";
    }

    if (confirmMsg && !confirm(confirmMsg)) {
        return;
    }

    const btn = document.getElementById(`relay-${normalizedChannel}-toggle`);
    dynamicRelayActionsInFlight.add(normalizedChannel);
    if (btn) {
        setElementBusy(btn, true);
        btn.textContent = "Zapis...";
    }

    try {
        const legacyMap = {
            filter: 'filter',
            main_light: 'light',
            plant_light: 'plant',
            heater: 'heater',
            aeration: 'aeration'
        };

        const legacyKind = legacyMap[relay.function];
        if (legacyKind) {
            await toggleRelayQuickAction(legacyKind);
        } else {
            await sendAction("test_relay", {
                channel: String(normalizedChannel),
                state: String(nextState),
                duration: "0"
            }, { requirePin: true });
            await fetchStatus(true);
            await fetchLogs(true);
        }
    } catch (error) {
        console.warn("Błąd przełączania przekaźnika:", error);
    } finally {
        dynamicRelayActionsInFlight.delete(normalizedChannel);
        const currentButton = document.getElementById(`relay-${normalizedChannel}-toggle`);
        if (currentButton) {
            setElementBusy(currentButton, false);
            // renderRelays will update text/state soon, but set correct text just in case
            currentButton.textContent = active ? 'Włącz' : 'Wyłącz';
        }
    }
}

function buildRelayRowHtml(relay, data, isCore) {
    const channel = normalizeRelayChannel(relay?.channel);
    if (channel === null) {
        return '';
    }
    const active = getRelayState(relay, data);
    const stateClass = active ? 'relay-on' : 'relay-off';
    const stateText = active ? 'ON' : 'OFF';
    
    const info = RELAY_FUNCTIONS_INFO[relay.function] || { label: relay.label, icon: 'fa-sliders' };
    const label = relay.function === 'custom' ? (relay.label || 'Urządzenie własne') : info.label;
    const icon = info.icon;

    let metaText = 'Automatyka';
    const schedule = data.schedule || {};
    
    if (relay.function === 'main_light') {
        const lightMode = modeValue(schedule.lightMode);
        const workRange = getDashboardWorkRange(data);
        metaText = lightMode === 1 ? 'Zawsze włączone' : (lightMode === 2 ? 'Ręcznie wyłączone' : workRange.label);
        metaText = `${metaText} / ${dashboardAquaelProfileLabel(data, 'light', 'lightProfile')}`;
    } else if (relay.function === 'plant_light') {
        const plantRange = dashboardScheduleRange(
            data,
            'plant_light',
            {
                mode: 'plantLightMode',
                startHour: 'plantStartHour',
                startMin: 'plantStartMin',
                endHour: 'plantEndHour',
                endMin: 'plantEndMin'
            },
            { startHour: 12, startMin: 0, endHour: 18, endMin: 0 }
        );
        metaText = describeDashboardRange(plantRange, { on: 'Zawsze włączone', off: 'Wyłączone' });
        metaText = `${metaText} / ${dashboardAquaelProfileLabel(data, 'plant_light', 'plantLightProfile')}`;
    } else if (relay.function === 'filter') {
        const filterMode = modeValue(schedule.filterMode);
        metaText = filterMode === 1 ? 'Zawsze włączony' : (filterMode === 2 ? 'Ręcznie wyłączony' : formatRange(schedule.filterStartHour, schedule.filterStartMin, schedule.filterEndHour, schedule.filterEndMin));
    } else if (relay.function === 'heater') {
        const heaterMode = modeValue(schedule.heaterMode);
        metaText = active ? 'Dogrzewanie aktywne' : (heaterMode === 1 ? 'Wyłączona' : `Cel ${formatTemperature(data.temperature?.target)}`);
    } else if (relay.function === 'aeration') {
        const airMode = modeValue(schedule.airMode);
        const relays = data.relays || {};
        const aerationPercent = clamp(Number(relays.aerationPercent || 0), 0, 100);
        metaText = active ? `Otwarcie ${aerationPercent}%` : (airMode === 1 ? 'Zawsze aktywne' : (airMode === 2 ? 'Ręcznie wyłączone' : formatRange(schedule.airStartHour, schedule.airStartMin, schedule.airEndHour, schedule.airEndMin)));
    } else {
        if (relay.defaultState === 'auto') {
            metaText = 'Tryb automatyczny';
        } else {
            metaText = relay.defaultState === 'on' ? 'Domyślnie ON' : 'Domyślnie OFF';
        }
    }

    const primaryClass = isCore ? ' relay-primary-item' : '';
    const busy = dynamicRelayActionsInFlight.has(channel);

    return `
    <div class="relay-row-item${primaryClass} ${stateClass}" id="relay-${channel}">
        <div class="relay-row-info">
            <span class="relay-row-icon">${getLocalIconMarkup ? getLocalIconMarkup(icon) : `<i class="fa-solid ${icon}"></i>`}</span>
            <div>
                <div class="relay-row-name-line">
                    <div class="relay-row-name">${escapeHtml(label)}</div>
                    <span id="relay-${channel}-state" class="relay-row-state">${stateText}</span>
                </div>
                <div class="relay-row-meta" id="relay-${channel}-meta">${escapeHtml(metaText)}</div>
            </div>
        </div>
        <button id="relay-${channel}-toggle" class="btn btn-secondary relay-row-btn" type="button" data-admin-only="true" data-relay-channel="${channel}" aria-pressed="${active ? 'true' : 'false'}" aria-busy="${busy ? 'true' : 'false'}"${busy ? ' disabled' : ''}>Przełącz</button>
    </div>`;
}

function renderRelaysLegacy(data) {
    const schedule = data.schedule || {};
    const relays = data.relays || {};
    const aerationPercent = clamp(Number(relays.aerationPercent || 0), 0, 100);
    const plantRange = dashboardScheduleRange(
        data,
        'plant_light',
        {
            mode: 'plantLightMode',
            startHour: 'plantStartHour',
            startMin: 'plantStartMin',
            endHour: 'plantEndHour',
            endMin: 'plantEndMin'
        },
        { startHour: 12, startMin: 0, endHour: 18, endMin: 0 }
    );

    const lightMode = modeValue(schedule.lightMode);
    const filterMode = modeValue(schedule.filterMode);
    const airMode = modeValue(schedule.airMode);
    const heaterMode = modeValue(schedule.heaterMode);

    const lightState = (relays.light1 ?? relays.light) ? 'on' : (lightMode === 2 ? 'off' : 'standby');
    const plantState = (relays.light2 ?? relays.plantLight) ? 'on' : (plantRange.mode === 'zawsze_wylaczone' ? 'off' : 'standby');
    const filterState = relays.pump ? 'on' : (filterMode === 2 ? 'off' : 'standby');
    const heaterState = relays.heater ? 'on' : (heaterMode === 1 ? 'off' : 'standby');
    const aerationState = aerationPercent > 0 ? 'on' : (airMode === 2 ? 'off' : 'standby');

    setRelayCard(
        'light',
        lightState,
        lightMode === 1 ? 'Zawsze wlaczone' : (lightMode === 2 ? 'Recznie wylaczone' : formatRange(schedule.dayStartHour, schedule.dayStartMin, schedule.dayEndHour, schedule.dayEndMin))
    );
    setRelayCard(
        'plant',
        plantState,
        describeDashboardRange(plantRange, {
            on: 'Zawsze włączone',
            off: 'Ręcznie wyłączone'
        })
    );
    setRelayCard(
        'filter',
        filterState,
        filterMode === 1 ? 'Zawsze wlaczony' : (filterMode === 2 ? 'Recznie wylaczony' : formatRange(schedule.filterStartHour, schedule.filterStartMin, schedule.filterEndHour, schedule.filterEndMin))
    );
    setRelayCard(
        'heater',
        heaterState,
        heaterState === 'on' ? 'Dogrzewanie aktywne' : (heaterMode === 1 ? 'Tryb OFF' : `Cel ${formatTemperature(data.temperature?.target)}`)
    );
    setRelayCard(
        'aeration',
        aerationState,
        aerationState === 'on' ? `Otwarcie ${aerationPercent}%` : (airMode === 1 ? 'Zawsze aktywne' : (airMode === 2 ? 'Recznie zamkniete' : formatRange(schedule.airStartHour, schedule.airStartMin, schedule.airEndHour, schedule.airEndMin)))
    );

    const activeCount = [lightState, plantState, filterState, heaterState, aerationState].filter((state) => state === 'on').length;
    setText('relay-count', `${activeCount} / 5 aktywne`);

    const toggles = {
        light: relays.light1 ?? relays.light,
        filter: relays.pump,
        plant: relays.light2 ?? relays.plantLight,
        heater: isHeaterAutomationEnabled(data),
        aeration: relays.aeration
    };

    for (const [key, active] of Object.entries(toggles)) {
        const btn = document.getElementById(`relay-${key}-toggle`);
        if (btn && btn.dataset.busy !== '1') {
            btn.textContent = relayQuickButtonLabel(!!active);
        }
    }
}

function renderRelays(data) {
    const config = data.relaysConfig || window.lastStatusData?.relaysConfig;
    if (!config || !Array.isArray(config.relays)) {
        renderRelaysLegacy(data);
        return;
    }

    const container = document.querySelector('.relays-section');
    if (!container) return;

    const activeRelays = config.relays
        .filter((relay) => normalizeRelayChannel(relay?.channel) !== null && relay.function !== 'none')
        .slice(0, 8);
    const coreFuncs = ['main_light', 'filter', 'plant_light'];
    const coreRelays = activeRelays.filter(r => coreFuncs.includes(r.function));
    const extendedRelays = activeRelays.filter(r => !coreFuncs.includes(r.function));

    const onCount = activeRelays.filter(r => getRelayState(r, data)).length;
    setText('relay-count', `${onCount} / ${activeRelays.length} aktywne`);

    let html = '';

    if (coreRelays.length > 0) {
        html += `
        <div class="relay-core-group tier-group" data-tier="core" aria-label="Sterowanie podstawowe">
            <div class="relay-group-heading">
                <span class="tier-badge" data-tier="core">Rdzeń</span>
                <span class="relay-group-label">Podstawowe</span>
            </div>
            <div class="relay-core-grid">
                ${coreRelays.map(r => buildRelayRowHtml(r, data, true)).join('')}
            </div>
        </div>`;
    }

    if (extendedRelays.length > 0) {
        html += `
        <details class="relay-automation-group tier-group" data-tier="extended" open>
            <summary>
                <span class="relay-automation-summary">
                    <span class="tier-badge" data-tier="extended">Rozszerzone</span>
                    <strong>Automatyka rozszerzona</strong>
                    <small>Harmonogramy, progi i opcjonalne osprzetowanie</small>
                </span>
                <i class="fa-solid fa-chevron-down"></i>
            </summary>
            <div class="relay-automation-list">
                ${extendedRelays.map(r => buildRelayRowHtml(r, data, false)).join('')}
            </div>
        </details>`;
    }

    if (html !== lastRelayMarkup) {
        const focusedId = container.contains(document.activeElement) ? document.activeElement?.id : '';
        container.innerHTML = html;
        lastRelayMarkup = html;

        if (focusedId) {
            document.getElementById(focusedId)?.focus({ preventScroll: true });
        }
        if (typeof applyAuthState === 'function') {
            applyAuthState();
        }
    }

    if (container.dataset.relayActionsBound !== '1') {
        container.dataset.relayActionsBound = '1';
        container.addEventListener('click', (event) => {
            const button = event.target.closest('button[data-relay-channel]');
            if (!button || !container.contains(button)) {
                return;
            }
            toggleDynamicRelayAction(button.dataset.relayChannel);
        });
    }
}

function describeFeedSchedule(feeding) {
    const freqLabel = formatFeedFrequency(feeding?.freq);
    if (freqLabel === 'Wylaczone') {
        return 'Automatyczne karmienie wylaczone';
    }
    return `${freqLabel} ${formatTime(feeding?.hour, feeding?.minute)}`;
}

function renderTodaySchedule(data) {
    const list = document.getElementById('today-schedule-list');
    if (!list) return;

    const schedule = data.schedule || {};
    const feeding = data.feeding || {};
    const workRange = getDashboardWorkRange(data);
    const plantRange = dashboardScheduleRange(
        data,
        'plant_light',
        {
            mode: 'plantLightMode',
            startHour: 'plantStartHour',
            startMin: 'plantStartMin',
            endHour: 'plantEndHour',
            endMin: 'plantEndMin'
        },
        { startHour: 12, startMin: 0, endHour: 18, endMin: 0 }
    );
    setText('dashboard-work-window', workRange.label);

    const items = [
        {
            label: 'Okno pracy',
            value: describeDashboardRange(workRange, {
                on: 'Cała doba',
                off: 'Tryb serwisowy OFF'
            })
        },
        {
            label: 'Świetlówka przednia',
            value: modeValue(schedule.lightMode) === 1
                ? 'Zawsze włączone'
                : (modeValue(schedule.lightMode) === 2
                    ? 'Wyłączone'
                    : workRange.label)
        },
        {
            label: 'Świetlówka tylna',
            value: describeDashboardRange(plantRange, {
                on: 'Zawsze włączone',
                off: 'Wyłączone'
            })
        },
        {
            label: 'Filtr',
            value: modeValue(schedule.filterMode) === 1
                ? 'Zawsze włączony'
                : (modeValue(schedule.filterMode) === 2
                    ? 'Wyłączony'
                    : formatRange(schedule.filterStartHour, schedule.filterStartMin, schedule.filterEndHour, schedule.filterEndMin))
        },
        {
            label: 'Napowietrzanie',
            value: modeValue(schedule.airMode) === 1
                ? 'Zawsze aktywne'
                : (modeValue(schedule.airMode) === 2
                    ? 'Wyłączone'
                    : formatRange(schedule.airStartHour, schedule.airStartMin, schedule.airEndHour, schedule.airEndMin))
        },
        {
            label: 'Grzałka',
            value: modeValue(schedule.heaterMode) === 1
                ? 'Wyłączona'
                : 'Auto progowe 24/7'
        },
        {
            label: 'Karmienie',
            value: describeFeedSchedule(feeding)
        }
    ];

    const markup = items.map((item) => `
        <div class="schedule-summary-item">
            <span>${escapeHtml(item.label)}</span>
            <strong>${escapeHtml(item.value)}</strong>
        </div>`).join('');
    if (markup !== lastScheduleSummaryMarkup) {
        list.innerHTML = markup;
        lastScheduleSummaryMarkup = markup;
    }
}

function renderFeederCard(data) {
    const feeding = data.feeding || {};
    const lastResult = normalizeFeedResultCode(feeding.lastResult);
    setText('feed-next-label', describeFeedSchedule(feeding));

    const lastFeedText = formatEpoch(feeding.lastFeedEpoch, {
        fallback: 'brak danych',
        includeDate: true,
        includeSeconds: false
    });
    setText('feed-last-label', `Ostatnie karmienie: ${lastFeedText}`);

    if (feeding.active && (feedActionState.awaitingResponse || feedActionState.awaitingCompletion)) {
        feedActionState.awaitingResponse = false;
        feedActionState.awaitingCompletion = true;
        feedActionState.sawActive = true;
        showFeedModalState('progress', 'Trwa karmienie...', 'Sensor polozenia jest w trakcie odczytu.');
    } else if (feedActionState.awaitingCompletion && !feeding.active) {
        const lastFeedEpoch = Math.max(0, Math.trunc(Number(feeding.lastFeedEpoch) || 0));
        const resultChanged = lastResult && lastResult !== feedActionState.baselineLastResult;
        const feedConfirmed = feedActionState.sawActive ||
            lastFeedEpoch > feedActionState.baselineLastFeedEpoch ||
            resultChanged;
        const waitExpired = (Date.now() - feedActionState.startedAtMs) > 15000;

        if (feedConfirmed || waitExpired) {
            const result = describeFeedResult(waitExpired && !feedConfirmed ? 'timeout' : lastResult, '');
            showFeedModalState(result.kind, result.title, result.message, 2400);
            resetFeedActionState();
        }
    }

    const button = document.getElementById('feed-now-btn');
    if (button) {
        const busy = !!feeding.active || feedActionState.awaitingResponse || feedActionState.awaitingCompletion;
        button.disabled = busy;
        button.textContent = feeding.active
            ? 'Trwa...'
            : (feedActionState.awaitingResponse ? 'Start...' : (feedActionState.awaitingCompletion ? 'Czekam...' : 'Karm teraz'));
    }
}

function showChartTooltip(clientX, clientY, point) {
    const tooltip = document.getElementById('temperature-chart-tooltip');
    const shell = document.querySelector('.temp-chart-shell');
    if (!tooltip || !shell) return;

    tooltip.innerHTML = `<strong>${escapeHtml(point.valueLabel)}</strong><span>${escapeHtml(point.timeLabel)}</span>`;
    tooltip.hidden = false;

    const rect = shell.getBoundingClientRect();
    const width = tooltip.offsetWidth || 148;
    const height = tooltip.offsetHeight || 56;
    let left = clientX - rect.left - width / 2;
    let top = clientY - rect.top - height - 14;

    left = clamp(left, 10, rect.width - width - 10);
    top = clamp(top, 10, rect.height - height - 10);

    tooltip.style.left = `${left}px`;
    tooltip.style.top = `${top}px`;
}

function hideChartTooltip() {
    const tooltip = document.getElementById('temperature-chart-tooltip');
    if (tooltip) {
        tooltip.hidden = true;
    }
}

let temperatureChartResizeObserver = null;
let temperatureChartResizeFrame = 0;
let observedTemperatureChartShell = null;
let observedTemperatureChartWidth = 0;

function formatChartTemperature(value, digits = 1) {
    return `${Number(value).toFixed(digits)}\u00B0C`;
}

function formatChartTemperatureDelta(value) {
    const numeric = Number(value);
    return Number.isFinite(numeric) ? `${Math.abs(numeric).toFixed(1)}\u00B0C` : '--.-\u00B0C';
}

function buildSmoothChartPath(coords) {
    if (!coords.length) return '';
    if (coords.length === 1) {
        return `M ${coords[0].x.toFixed(2)} ${coords[0].y.toFixed(2)}`;
    }

    const tension = 0.18;
    let path = `M ${coords[0].x.toFixed(2)} ${coords[0].y.toFixed(2)}`;
    for (let index = 0; index < coords.length - 1; index += 1) {
        const p0 = coords[index - 1] || coords[index];
        const p1 = coords[index];
        const p2 = coords[index + 1];
        const p3 = coords[index + 2] || p2;
        const cp1x = p1.x + (p2.x - p0.x) * tension;
        const cp1y = p1.y + (p2.y - p0.y) * tension;
        const cp2x = p2.x - (p3.x - p1.x) * tension;
        const cp2y = p2.y - (p3.y - p1.y) * tension;
        path += ` C ${cp1x.toFixed(2)} ${cp1y.toFixed(2)} ${cp2x.toFixed(2)} ${cp2y.toFixed(2)} ${p2.x.toFixed(2)} ${p2.y.toFixed(2)}`;
    }
    return path;
}

function describeTemperatureTrend(points) {
    if (points.length < 2) {
        return {
            label: 'Start serii',
            tone: 'neutral'
        };
    }

    const windowStart = Math.max(0, points.length - Math.min(points.length, 4));
    const reference = points[windowStart].value;
    const latest = points[points.length - 1].value;
    const delta = latest - reference;

    if (Math.abs(delta) < 0.12) {
        return {
            label: 'Stabilnie',
            tone: 'calm'
        };
    }

    return delta > 0
        ? {
            label: `Rosnie ${formatChartTemperatureDelta(delta)}`,
            tone: 'trend-up'
        }
        : {
            label: `Spada ${formatChartTemperatureDelta(delta)}`,
            tone: 'trend-down'
        };
}

function describeTargetRelationship(latestValue, target, hysteresis) {
    if (target === null) {
        return null;
    }

    const delta = latestValue - target;
    const withinBand = hysteresis > 0
        ? Math.abs(delta) <= hysteresis
        : Math.abs(delta) < 0.12;

    if (withinBand) {
        return {
            label: 'W pasmie',
            detailLabel: 'Temperatura jest w pasmie docelowym.',
            tone: 'ok'
        };
    }

    return delta < 0
        ? {
            label: `Do celu ${formatChartTemperature(Math.abs(delta))}`,
            detailLabel: `Ponizej celu o ${formatChartTemperature(Math.abs(delta))}.`,
            tone: 'warning'
        }
        : {
            label: `Nad celem ${formatChartTemperature(Math.abs(delta))}`,
            detailLabel: `Powyzej celu o ${formatChartTemperature(Math.abs(delta))}.`,
            tone: 'warning'
        };
}

function buildTemperatureDomain(points, target, hysteresis, snapshotMode) {
    const sourceValues = points.map((point) => point.value);
    if (target !== null && snapshotMode) {
        sourceValues.push(target);
        if (hysteresis > 0) {
            sourceValues.push(target - hysteresis, target + hysteresis);
        }
    }

    let minValue = Math.min(...sourceValues);
    let maxValue = Math.max(...sourceValues);
    if (!Number.isFinite(minValue) || !Number.isFinite(maxValue)) {
        minValue = 20;
        maxValue = 30;
    }

    if (snapshotMode) {
        const minimumSpan = Math.max(2.2, hysteresis > 0 ? hysteresis * 5.2 : 0);
        let span = maxValue - minValue;
        if (span < minimumSpan) {
            const grow = (minimumSpan - span) / 2;
            minValue -= grow;
            maxValue += grow;
            span = minimumSpan;
        }

        const pad = Math.max(0.26, span * 0.12);
        return {
            minValue: minValue - pad,
            maxValue: maxValue + pad,
            guidesVisible: target !== null
        };
    }

    if (Math.abs(maxValue - minValue) < 0.6) {
        maxValue += 0.3;
        minValue -= 0.3;
    }

    const dataSpan = Math.max(0.6, maxValue - minValue);
    const basePad = Math.max(0.18, dataSpan * 0.18);
    minValue -= basePad;
    maxValue += basePad;

    let guidesVisible = false;
    if (target !== null) {
        const guideMin = hysteresis > 0 ? target - hysteresis : target;
        const guideMax = hysteresis > 0 ? target + hysteresis : target;
        const guideGap = Math.max(0, minValue - guideMax, guideMin - maxValue);
        guidesVisible = guideGap <= Math.max(0.28, dataSpan * 0.42);

        if (guidesVisible) {
            const guidePad = Math.max(0.12, dataSpan * 0.08);
            minValue = Math.min(minValue, guideMin - guidePad);
            maxValue = Math.max(maxValue, guideMax + guidePad);
        }
    }

    return { minValue, maxValue, guidesVisible };
}

function appendTemperatureChartDefs(svg) {
    const defs = createSvgEl('defs');

    const areaGradient = createSvgEl('linearGradient', {
        id: 'chart-area-gradient',
        x1: '0%',
        y1: '0%',
        x2: '0%',
        y2: '100%'
    });
    areaGradient.appendChild(createSvgEl('stop', { offset: '0%', 'stop-color': '#22d3ee', 'stop-opacity': '0.26' }));
    areaGradient.appendChild(createSvgEl('stop', { offset: '55%', 'stop-color': '#0ea5e9', 'stop-opacity': '0.12' }));
    areaGradient.appendChild(createSvgEl('stop', { offset: '100%', 'stop-color': '#22d3ee', 'stop-opacity': '0.02' }));

    const lineGradient = createSvgEl('linearGradient', {
        id: 'chart-line-gradient',
        x1: '0%',
        y1: '0%',
        x2: '100%',
        y2: '0%'
    });
    lineGradient.appendChild(createSvgEl('stop', { offset: '0%', 'stop-color': '#67e8f9' }));
    lineGradient.appendChild(createSvgEl('stop', { offset: '100%', 'stop-color': '#38bdf8' }));

    const bandGradient = createSvgEl('linearGradient', {
        id: 'chart-band-gradient',
        x1: '0%',
        y1: '0%',
        x2: '100%',
        y2: '0%'
    });
    bandGradient.appendChild(createSvgEl('stop', { offset: '0%', 'stop-color': '#7c2d12', 'stop-opacity': '0.72' }));
    bandGradient.appendChild(createSvgEl('stop', { offset: '100%', 'stop-color': '#fb923c', 'stop-opacity': '0.94' }));

    defs.appendChild(areaGradient);
    defs.appendChild(lineGradient);
    defs.appendChild(bandGradient);
    svg.appendChild(defs);
}

function appendTemperatureChartFrame(svg, padding, plotWidth, plotHeight) {
    svg.appendChild(createSvgEl('rect', {
        x: padding.left,
        y: padding.top,
        width: plotWidth,
        height: plotHeight,
        rx: 18,
        class: 'chart-plot-backdrop'
    }));
    svg.appendChild(createSvgEl('rect', {
        x: padding.left,
        y: padding.top,
        width: plotWidth,
        height: plotHeight,
        rx: 18,
        class: 'chart-plot-frame'
    }));
}

function getTemperatureChartLayout(shellWidth, snapshotMode) {
    const width = Math.round(Number(shellWidth) || 0);

    if (width > 0 && width < 560) {
        return snapshotMode
            ? {
                name: 'phone',
                width: 420,
                height: 380,
                padding: { top: 18, right: 18, bottom: 28, left: 18 },
                gridSteps: 4,
                compact: true,
                snapshotStacked: true
            }
            : {
                name: 'phone',
                width: 420,
                height: 360,
                padding: { top: 18, right: 20, bottom: 42, left: 42 },
                gridSteps: 3,
                compact: true,
                snapshotStacked: false
            };
    }

    if (width > 0 && width < 920) {
        return snapshotMode
            ? {
                name: 'tablet',
                width: 720,
                height: 340,
                padding: { top: 18, right: 24, bottom: 30, left: 30 },
                gridSteps: 4,
                compact: true,
                snapshotStacked: false
            }
            : {
                name: 'tablet',
                width: 720,
                height: 312,
                padding: { top: 18, right: 28, bottom: 38, left: 50 },
                gridSteps: 4,
                compact: true,
                snapshotStacked: false
            };
    }

    return {
        name: 'desktop',
        width: 960,
        height: 320,
        padding: { top: 20, right: 34, bottom: 34, left: 58 },
        gridSteps: 4,
        compact: false,
        snapshotStacked: false
    };
}

function applyTemperatureChartLayout(svg, shell, layout, snapshotMode) {
    if (!svg || !layout) return;

    svg.setAttribute('viewBox', `0 0 ${layout.width} ${layout.height}`);
    svg.style.aspectRatio = `${layout.width} / ${layout.height}`;

    if (!shell) return;

    shell.dataset.chartLayout = layout.name;
    shell.classList.toggle('temp-chart-shell-snapshot', snapshotMode);
    shell.classList.toggle('temp-chart-shell-snapshot-stacked', snapshotMode && layout.snapshotStacked);
}

function ensureTemperatureChartResizeObserver(shell) {
    if (!shell || typeof ResizeObserver === 'undefined') {
        return;
    }

    if (temperatureChartResizeObserver && observedTemperatureChartShell === shell) {
        return;
    }

    if (temperatureChartResizeObserver && observedTemperatureChartShell) {
        temperatureChartResizeObserver.unobserve(observedTemperatureChartShell);
    }

    observedTemperatureChartShell = shell;
    observedTemperatureChartWidth = Math.round(shell.getBoundingClientRect().width);
    temperatureChartResizeObserver = new ResizeObserver((entries) => {
        const entry = entries[entries.length - 1];
        const nextWidth = Math.round(entry?.contentRect?.width || 0);

        if (!nextWidth || Math.abs(nextWidth - observedTemperatureChartWidth) < 2) {
            return;
        }

        observedTemperatureChartWidth = nextWidth;
        if (temperatureChartResizeFrame) {
            cancelAnimationFrame(temperatureChartResizeFrame);
        }

        temperatureChartResizeFrame = requestAnimationFrame(() => {
            temperatureChartResizeFrame = 0;
            if (lastStatusData?.temperature) {
                renderTemperatureChart(lastStatusData.temperature || {});
            }
        });
    });

    temperatureChartResizeObserver.observe(shell);
}

function renderTemperatureSnapshot(svg, points, target, hysteresis, domain, layout) {
    const width = layout.width;
    const height = layout.height;
    const padding = layout.padding;
    const plotWidth = width - padding.left - padding.right;
    const plotHeight = height - padding.top - padding.bottom;
    const latest = points[points.length - 1];
    const relation = describeTargetRelationship(latest.value, target, hysteresis);
    const compact = !!layout.compact;
    const stacked = !!layout.snapshotStacked;
    const panelInset = compact ? 14 : 18;
    const panelX = padding.left + panelInset;
    const panelY = padding.top + (stacked ? 12 : 18);
    const panelWidth = stacked
        ? plotWidth - panelInset * 2
        : Math.min(232, Math.max(206, plotWidth * 0.28));
    const panelHeight = stacked ? 132 : plotHeight - 36;
    const dividerX = panelX + panelWidth + (compact ? 18 : 22);
    const rail = stacked
        ? {
            startX: padding.left + 30,
            endX: padding.left + plotWidth - 30,
            y: panelY + panelHeight + 86
        }
        : {
            startX: dividerX + (compact ? 42 : 58),
            endX: padding.left + plotWidth - (compact ? 28 : 44),
            y: padding.top + Math.round(plotHeight * 0.48)
        };
    const span = Math.max(0.1, domain.maxValue - domain.minValue);
    const valueToX = (value) => rail.startX + ((value - domain.minValue) / span) * (rail.endX - rail.startX);
    const currentX = valueToX(latest.value);
    appendTemperatureChartDefs(svg);
    appendTemperatureChartFrame(svg, padding, plotWidth, plotHeight);

    svg.appendChild(createSvgEl('rect', {
        x: panelX,
        y: panelY,
        width: panelWidth,
        height: panelHeight,
        rx: compact ? 18 : 22,
        class: 'chart-snapshot-panel'
    }));
    if (!stacked) {
        svg.appendChild(createSvgEl('line', {
            x1: dividerX,
            y1: padding.top + 24,
            x2: dividerX,
            y2: padding.top + plotHeight - 24,
            class: 'chart-snapshot-divider'
        }));
    }

    const eyebrow = createSvgEl('text', {
        x: panelX + 20,
        y: panelY + (stacked ? 24 : 30),
        class: 'chart-snapshot-eyebrow'
    });
    eyebrow.textContent = 'AKTUALNY ODCZYT';
    svg.appendChild(eyebrow);

    const value = createSvgEl('text', {
        x: panelX + 20,
        y: panelY + (stacked ? 66 : 96),
        class: 'chart-snapshot-value'
    });
    value.textContent = latest.value.toFixed(2);
    svg.appendChild(value);

    const unit = createSvgEl('text', {
        x: panelX + (stacked ? Math.min(panelWidth - 54, 164) : 168),
        y: panelY + (stacked ? 66 : 96),
        class: 'chart-snapshot-unit'
    });
    unit.textContent = '\u00B0C';
    svg.appendChild(unit);

    const status = createSvgEl('text', {
        x: panelX + 20,
        y: panelY + (stacked ? 94 : 130),
        class: `chart-snapshot-status chart-snapshot-status-${relation?.tone || 'neutral'}`
    });
    status.textContent = relation ? relation.detailLabel : 'Brak temperatury docelowej.';
    svg.appendChild(status);

    const time = createSvgEl('text', {
        x: panelX + 20,
        y: panelY + (stacked ? 112 : 156),
        class: 'chart-snapshot-time'
    });
    time.textContent = `Pomiar ${latest.timeLabel}`;
    svg.appendChild(time);

    if (points.length > 1) {
        const trend = describeTemperatureTrend(points);
        const trendText = createSvgEl('text', {
            x: panelX + 20,
            y: panelY + (stacked ? 128 : 182),
            class: 'chart-snapshot-time'
        });
        trendText.textContent = trend.label;
        svg.appendChild(trendText);
    }

    const railTitle = createSvgEl('text', {
        x: rail.startX,
        y: stacked ? panelY + panelHeight + 36 : padding.top + 52,
        class: 'chart-snapshot-rail-title'
    });
    railTitle.textContent = target !== null ? 'Stan wzgledem celu' : 'Biezaca skala temperatury';
    svg.appendChild(railTitle);

    svg.appendChild(createSvgEl('line', {
        x1: rail.startX,
        y1: rail.y,
        x2: rail.endX,
        y2: rail.y,
        class: 'chart-snapshot-rail-track'
    }));

    const tickSteps = layout.gridSteps || 4;
    const guideHalf = compact ? 14 : 18;
    for (let step = 0; step <= tickSteps; step += 1) {
        const ratio = step / tickSteps;
        const x = rail.startX + ratio * (rail.endX - rail.startX);
        const sampleValue = domain.minValue + ratio * (domain.maxValue - domain.minValue);
        svg.appendChild(createSvgEl('line', {
            x1: x,
            y1: rail.y - guideHalf,
            x2: x,
            y2: rail.y + guideHalf,
            class: 'chart-snapshot-guide'
        }));

        if (step === 0 || step === Math.floor(tickSteps / 2) || step === tickSteps) {
            const label = createSvgEl('text', {
                x,
                y: rail.y + (stacked ? 40 : 46),
                class: 'chart-snapshot-scale-label'
            });
            label.setAttribute('text-anchor', step === 0 ? 'start' : (step === tickSteps ? 'end' : 'middle'));
            label.textContent = formatChartTemperature(sampleValue);
            svg.appendChild(label);
        }
    }

    if (target !== null && hysteresis > 0) {
        const lowerX = valueToX(target - hysteresis);
        const upperX = valueToX(target + hysteresis);
        svg.appendChild(createSvgEl('line', {
            x1: lowerX,
            y1: rail.y,
            x2: upperX,
            y2: rail.y,
            class: 'chart-snapshot-band'
        }));
        [lowerX, upperX].forEach((x) => {
            svg.appendChild(createSvgEl('circle', {
                cx: x,
                cy: rail.y,
                r: 4,
                class: 'chart-snapshot-band-cap'
            }));
        });
    }

    if (target !== null) {
        const targetX = valueToX(target);
        svg.appendChild(createSvgEl('line', {
            x1: targetX,
            y1: rail.y - (stacked ? 26 : 34),
            x2: targetX,
            y2: rail.y + (stacked ? 24 : 30),
            class: 'chart-snapshot-target'
        }));

        const targetLabel = createSvgEl('text', {
            x: targetX,
            y: rail.y + (stacked ? 64 : 76),
            class: 'chart-snapshot-target-label'
        });
        targetLabel.setAttribute('text-anchor', 'middle');
        targetLabel.textContent = `Cel ${formatChartTemperature(target)}`;
        svg.appendChild(targetLabel);
    }

    svg.appendChild(createSvgEl('line', {
        x1: rail.startX,
        y1: rail.y,
        x2: currentX,
        y2: rail.y,
        class: 'chart-snapshot-fill'
    }));
    svg.appendChild(createSvgEl('line', {
        x1: currentX,
        y1: rail.y - (stacked ? 40 : 48),
        x2: currentX,
        y2: rail.y + (stacked ? 20 : 26),
        class: 'chart-snapshot-stem'
    }));
    svg.appendChild(createSvgEl('circle', {
        cx: currentX,
        cy: rail.y,
        r: compact ? 14 : 17,
        class: 'chart-point-current'
    }));
    svg.appendChild(createSvgEl('circle', {
        cx: currentX,
        cy: rail.y,
        r: compact ? 6 : 7,
        class: 'chart-point-core'
    }));

    const currentHit = createSvgEl('circle', {
        cx: currentX,
        cy: rail.y,
        r: compact ? 14 : 16,
        tabindex: 0,
        class: 'chart-point-hit'
    });
    currentHit.addEventListener('mouseenter', (event) => showChartTooltip(event.clientX, event.clientY, latest));
    currentHit.addEventListener('mousemove', (event) => showChartTooltip(event.clientX, event.clientY, latest));
    currentHit.addEventListener('mouseleave', hideChartTooltip);
    currentHit.addEventListener('focus', () => {
        const rect = svg.getBoundingClientRect();
        showChartTooltip(rect.left + currentX, rect.top + rail.y, latest);
    });
    currentHit.addEventListener('blur', hideChartTooltip);
    svg.appendChild(currentHit);
}

function renderTemperatureTrendChart(svg, points, target, hysteresis, domain, layout) {
    const width = layout.width;
    const height = layout.height;
    const padding = layout.padding;
    const plotWidth = width - padding.left - padding.right;
    const plotHeight = height - padding.top - padding.bottom;
    const minValue = domain.minValue;
    const maxValue = domain.maxValue;
    const guidesVisible = domain.guidesVisible;
    const compact = !!layout.compact;
    const xFor = (index) => padding.left + (points.length === 1 ? plotWidth / 2 : (index / (points.length - 1)) * plotWidth);
    const yFor = (value) => padding.top + ((maxValue - value) / (maxValue - minValue)) * plotHeight;
    const targetVisible = target !== null && target >= minValue && target <= maxValue;

    appendTemperatureChartDefs(svg);
    appendTemperatureChartFrame(svg, padding, plotWidth, plotHeight);

    if (target !== null && guidesVisible && hysteresis > 0) {
        const upperY = yFor(target + hysteresis);
        const lowerY = yFor(target - hysteresis);
        const bandTop = Math.min(upperY, lowerY);
        const bandBottom = Math.max(upperY, lowerY);

        svg.appendChild(createSvgEl('rect', {
            x: padding.left,
            y: bandTop,
            width: plotWidth,
            height: Math.max(2, bandBottom - bandTop),
            rx: 14,
            class: 'chart-band'
        }));
    }

    const gridSteps = layout.gridSteps || 4;
    for (let index = 0; index <= gridSteps; index += 1) {
        const ratio = index / gridSteps;
        const y = padding.top + ratio * plotHeight;
        const value = maxValue - ratio * (maxValue - minValue);
        svg.appendChild(createSvgEl('line', {
            x1: padding.left,
            y1: y,
            x2: padding.left + plotWidth,
            y2: y,
            class: 'chart-grid-line'
        }));

        const label = createSvgEl('text', {
            x: Math.max(6, padding.left - (compact ? 34 : 52)),
            y: y + 4,
            class: 'chart-grid-label'
        });
        label.textContent = formatChartTemperature(value);
        svg.appendChild(label);
    }

    if (targetVisible) {
        const targetY = yFor(target);
        svg.appendChild(createSvgEl('line', {
            x1: padding.left,
            y1: targetY,
            x2: padding.left + plotWidth,
            y2: targetY,
            class: 'chart-target-line'
        }));
    }

    const coords = points.map((point, index) => ({
        x: xFor(index),
        y: yFor(point.value)
    }));
    const pathData = buildSmoothChartPath(coords);
    const firstCoord = coords[0];
    const lastCoord = coords[coords.length - 1];
    const areaData = `${pathData} L ${lastCoord.x.toFixed(2)} ${(padding.top + plotHeight).toFixed(2)} L ${firstCoord.x.toFixed(2)} ${(padding.top + plotHeight).toFixed(2)} Z`;

    svg.appendChild(createSvgEl('path', { d: areaData, class: 'chart-area' }));
    svg.appendChild(createSvgEl('path', { d: pathData, class: 'chart-line-glow' }));
    svg.appendChild(createSvgEl('path', { d: pathData, class: 'chart-line' }));

    if (coords.length > 1) {
        svg.appendChild(createSvgEl('line', {
            x1: lastCoord.x,
            y1: padding.top + 8,
            x2: lastCoord.x,
            y2: padding.top + plotHeight - 8,
            class: 'chart-focus-line'
        }));
    }

    svg.appendChild(createSvgEl('circle', {
        cx: lastCoord.x,
        cy: lastCoord.y,
        r: compact ? 12 : 14,
        class: 'chart-point-current'
    }));
    svg.appendChild(createSvgEl('circle', {
        cx: lastCoord.x,
        cy: lastCoord.y,
        r: compact ? 6 : 7,
        class: 'chart-point-core'
    }));

    points.forEach((point, index) => {
        const { x, y } = coords[index];
        const isLatest = index === points.length - 1;

        const circle = createSvgEl('circle', {
            cx: x,
            cy: y,
            r: isLatest ? (compact ? 4.5 : 5) : (compact ? 3.5 : 4),
            class: isLatest ? 'chart-point chart-point-latest' : 'chart-point'
        });
        const title = createSvgEl('title');
        title.textContent = `${point.valueLabel} - ${point.timeLabel}`;
        circle.appendChild(title);
        svg.appendChild(circle);

        const hit = createSvgEl('circle', {
            cx: x,
            cy: y,
            r: compact ? 11 : 13,
            tabindex: 0,
            class: 'chart-point-hit'
        });
        hit.addEventListener('mouseenter', (event) => showChartTooltip(event.clientX, event.clientY, point));
        hit.addEventListener('mousemove', (event) => showChartTooltip(event.clientX, event.clientY, point));
        hit.addEventListener('mouseleave', hideChartTooltip);
        hit.addEventListener('focus', () => {
            const rect = svg.getBoundingClientRect();
            showChartTooltip(rect.left + x, rect.top + y, point);
        });
        hit.addEventListener('blur', hideChartTooltip);
        svg.appendChild(hit);
    });
}

function renderTemperatureChartSummary(summary, points, target, hysteresis, snapshotMode) {
    if (!summary) return;

    const latest = points[points.length - 1];
    const relation = describeTargetRelationship(latest.value, target, hysteresis);
    const badges = [`<span class="temp-chart-pill temp-chart-pill-live">Ostatni <strong>${escapeHtml(latest.valueLabel)}</strong></span>`];

    if (relation) {
        badges.push(`<span class="temp-chart-pill temp-chart-pill-${relation.tone}">${escapeHtml(relation.label)}</span>`);
    }

    if (target !== null) {
        badges.push(`<span class="temp-chart-pill temp-chart-pill-target">Cel <strong>${escapeHtml(formatChartTemperature(target))}</strong></span>`);
    }

    if (target !== null && hysteresis > 0) {
        const lower = formatChartTemperature(target - hysteresis);
        const upper = formatChartTemperature(target + hysteresis);
        badges.push(`<span class="temp-chart-pill temp-chart-pill-band">Pasmo <strong>${escapeHtml(`${lower} - ${upper}`)}</strong></span>`);
    }

    if (!snapshotMode) {
        const trend = describeTemperatureTrend(points);
        const minPoint = points.reduce((lowest, point) => (point.value < lowest.value ? point : lowest), points[0]);
        const maxPoint = points.reduce((highest, point) => (point.value > highest.value ? point : highest), points[0]);
        badges.push(`<span class="temp-chart-pill temp-chart-pill-${trend.tone}">${escapeHtml(trend.label)}</span>`);
        badges.push(`<span class="temp-chart-pill temp-chart-pill-neutral">Zakres <strong>${escapeHtml(`${minPoint.valueLabel} - ${maxPoint.valueLabel}`)}</strong></span>`);
    }

    summary.innerHTML = badges.join('');
}

function renderTemperatureChart(temperature) {
    const svg = document.getElementById('temperature-chart-svg');
    const empty = document.getElementById('temperature-chart-empty');
    const summary = document.getElementById('temperature-chart-summary');
    const note = document.getElementById('temperature-chart-note');
    const shell = document.querySelector('.temp-chart-shell');
    const axis = shell ? shell.querySelector('.temp-chart-axis') : null;
    if (!svg || !empty) return;

    ensureTemperatureChartResizeObserver(shell);

    const rawHistory = Array.isArray(temperature?.history) ? temperature.history : [];
    const historyCapacity = Math.max(1, Math.round(toFiniteNumber(temperature?.historyCapacity) ?? 20));
    const historyIntervalMinutes = Math.max(1, Math.round(toFiniteNumber(temperature?.historyIntervalMinutes) ?? 10));
    const dashboardHistoryLimit = Math.min(historyCapacity, 36);
    const snapshotThreshold = 4;
    const points = rawHistory
        .map((item, index) => ({
            value: toFiniteNumber(item?.value),
            epoch: toFiniteNumber(item?.epoch),
            index
        }))
        .filter((item) => isValidTemperature(item.value))
        .slice(-dashboardHistoryLimit)
        .map((item, index, arr) => ({
            value: item.value,
            epoch: item.epoch,
            valueLabel: formatChartTemperature(item.value, 2),
            timeLabel: formatEpoch(item.epoch, {
                fallback: `Pomiar ${index + 1} z ${arr.length}`,
                includeDate: false,
                includeSeconds: false
            })
        }));

    const target = toFiniteNumber(temperature?.target);
    const hysteresis = Math.abs(toFiniteNumber(temperature?.hysteresis) ?? 0);
    const shellWidth = Math.round(shell?.clientWidth || 0);
    const chartSignature = [
        shellWidth,
        historyCapacity,
        historyIntervalMinutes,
        target ?? '',
        hysteresis,
        points.map((point) => `${point.epoch ?? ''}:${point.value}`).join('|')
    ].join(';');
    if (chartSignature === lastTemperatureChartSignature) {
        return;
    }
    lastTemperatureChartSignature = chartSignature;

    svg.innerHTML = '';
    if (summary) summary.innerHTML = '';
    if (note) {
        note.hidden = true;
        note.textContent = '';
    }
    hideChartTooltip();
    const visibleHours = Math.max(
        1,
        Math.round(((Math.max(points.length, dashboardHistoryLimit) - 1) * historyIntervalMinutes) / 60)
    );
    setText(
        'temperature-chart-meta',
        `${points.length || 0} z ${historyCapacity} pomiarow | ostatnie ~${visibleHours} h`
    );

    const emptyLayout = getTemperatureChartLayout(shell ? shell.clientWidth : 0, false);
    applyTemperatureChartLayout(svg, shell, emptyLayout, false);

    if (points.length === 0) {
        empty.hidden = false;
        if (axis) axis.hidden = false;
        setText('temperature-chart-start', 'Najstarszy pomiar');
        setText('temperature-chart-end', 'Teraz');
        return;
    }

    empty.hidden = true;

    const snapshotMode = points.length < snapshotThreshold;
    const layout = getTemperatureChartLayout(shell ? shell.clientWidth : 0, snapshotMode);
    const domain = buildTemperatureDomain(points, target, hysteresis, snapshotMode);

    applyTemperatureChartLayout(svg, shell, layout, snapshotMode);
    if (axis) {
        axis.hidden = snapshotMode;
    }

    renderTemperatureChartSummary(summary, points, target, hysteresis, snapshotMode);

    if (snapshotMode) {
        if (note) {
            note.hidden = false;
            note.textContent = `Tryb podgladu: pelny trend wlaczy sie po ${snapshotThreshold} probkach. Kolejny odczyt za okolo ${historyIntervalMinutes} min.`;
        }
        renderTemperatureSnapshot(svg, points, target, hysteresis, domain, layout);
        return;
    }

    setText('temperature-chart-start', points[0].timeLabel);
    setText('temperature-chart-end', points[points.length - 1].timeLabel);
    renderTemperatureTrendChart(svg, points, target, hysteresis, domain, layout);
}

function renderDashboard(data) {
    lastStatusData = data;
    renderTopbarActiveModules(data);
    renderTemperatureCard(data.temperature || {});
    renderWaterQualityCard(data);
    renderBatteryWidgets(data.battery || {});
    renderFirmwareInfo(data.firmware || {});
    renderNetworkCard(data.network || {});
    renderSettingsNetworkPanel(data.network || {});
    renderSettingsClockPanel(data.clock || {});
    renderTemperatureSettingsPanel(data);
    renderDisplaySettingsPanel(data);
    renderDiagnosticsPanel(data);
    if (typeof renderModulesTab === 'function') {
        renderModulesTab(data);
    }
    if (typeof window.renderRelayModuleOverview === 'function') {
        window.renderRelayModuleOverview(data);
    }
    renderScheduleEditor(data);
    renderRelays(data);
    renderTodaySchedule(data);
    renderFeederCard(data);
    renderTemperatureChart(data.temperature || {});
    renderStatusCommandStrips(data);
}

async function triggerFeed() {
    if (feedActionState.awaitingResponse || feedActionState.awaitingCompletion || lastStatusData?.feeding?.active) {
        return;
    }

    feedActionState.awaitingResponse = true;
    feedActionState.awaitingCompletion = false;
    feedActionState.sawActive = false;
    feedActionState.startedAtMs = Date.now();
    feedActionState.baselineLastFeedEpoch = Math.max(0, Math.trunc(Number(lastStatusData?.feeding?.lastFeedEpoch) || 0));
    feedActionState.baselineLastResult = normalizeFeedResultCode(lastStatusData?.feeding?.lastResult);

    showFeedModalState('progress', 'Uruchamianie karmienia...', 'Wysylam polecenie do sterownika.');

    try {
        await sendAction('feed_now', {}, { showSaveAnimation: false });
        feedActionState.awaitingResponse = false;
        feedActionState.awaitingCompletion = true;
        showFeedModalState('progress', 'Trwa karmienie...', 'Sterownik przyjal polecenie. Czekam na potwierdzenie.');
        await fetchStatus(true);
    } catch (error) {
        const result = describeFeedResult(error?.code, describeRequestError(error));
        showFeedModalState(result.kind, result.title, result.message, 2600);
        resetFeedActionState();
        await fetchStatus(true);
    }
}
