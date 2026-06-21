function renderSettingsClockPanel(clock) {
    const input = document.getElementById('settings-device-datetime');
    const status = document.getElementById('settings-clock-status');
    if (!input || !status) return;

    const formatted = formatControllerClock(clock);
    if (formatted) {
        input.value = formatted;
        status.textContent = 'Czas urzÄ…dzenia pobrany automatycznie ze sterownika.';
        status.style.color = 'var(--accent-cyan)';
        setCommandStatus('settings-strip-clock', 'Czas OK', formatted, 'ok');
    } else {
        input.value = 'Brak poprawnego czasu z urzÄ…dzenia.';
        status.textContent = 'Sterownik nie udostÄ™pniĹ‚ jeszcze poprawnego czasu.';
        status.style.color = 'var(--text-muted)';
        setCommandStatus('settings-strip-clock', 'Brak czasu', 'RTC/NTP bez poprawnej daty', 'warn');
    }
}

function formatSleepBlockerLabel(code) {
    switch (code) {
        case 'idle_window':
            return 'okno bezczynnosci';
        case 'outputs_active':
            return 'wyjscia aktywne';
        case 'not_night':
            return 'poza noca';
        case 'ota':
            return 'OTA';
        case 'ap_mode':
            return 'tryb AP';
        case 'service_mode':
            return 'tryb serwisowy';
        case 'time_sync':
            return 'sync czasu';
        case 'sta_active':
            return 'STA aktywne';
        case 'feeding':
            return 'karmienie';
        case 'time_invalid':
            return 'czas';
        case 'window_closed':
            return 'poza oknem ECO';
        case 'wifi_active':
            return 'WiFi aktywne';
        case 'ota_active':
            return 'OTA';
        case 'heater_active':
            return 'grzalka';
        case 'temp_unsafe':
            return 'temperatura niepewna';
        case 'feed_soon':
            return 'karmienie wkrotce';
        default:
            return code || 'nieznane';
    }
}

function renderTemperatureSettingsPanel(data) {
    const schedule = data.schedule || {};
    const temperature = data.temperature || {};
    const enabled = Number(schedule.heaterMode) !== 1;

    setCheckboxIfClean('settings-temp-enabled', enabled);
    setNumericValueIfClean('settings-temp-target', temperature.target, 0);
    setNumericValueIfClean('settings-temp-hyst', temperature.hysteresis, 1);
    setCommandStatus(
        'settings-strip-temperature',
        enabled ? 'Auto progowe' : 'OFF',
        `Cel ${formatTemperature(temperature.target)} / histereza ${formatTemperature(temperature.hysteresis)}`,
        enabled ? 'ok' : 'neutral'
    );
}

function buildModuleHealthItem(name, state, tone, detail) {
    const safeTone = ['ok', 'warn', 'danger', 'idle'].includes(tone) ? tone : 'idle';
    return `
        <div class="module-health-item module-health-${safeTone}">
            <div class="module-health-top">
                <span class="module-health-name">${escapeHtml(name)}</span>
                <span class="module-health-state">${escapeHtml(state)}</span>
            </div>
            <span class="module-health-detail">${escapeHtml(detail)}</span>
        </div>`;
}

function renderModuleEdgeCases(data) {
    const container = document.getElementById('module-edge-list');
    if (!container) return;

    if (!data || typeof data !== 'object') {
        container.innerHTML = buildModuleHealthItem('Sterownik', 'OCZEKUJE', 'idle', 'Brak danych statusu');
        return;
    }

    const temperature = data.temperature || {};
    const schedule = data.schedule || {};
    const relays = data.relays || {};
    const feeding = data.feeding || {};
    const network = data.network || {};
    const system = data.system || {};
    const battery = data.battery || {};
    const criticalLogs = Math.max(0, Math.trunc(Number(cachedLogs?.counts?.critical) || cachedLogs?.critical?.length || 0));
    const items = [];

    const currentTempOk = isValidTemperature(temperature.current);
    const targetTemp = toFiniteNumber(temperature.target);
    const hysteresis = toFiniteNumber(temperature.hysteresis);
    if (!currentTempOk) {
        items.push(buildModuleHealthItem('Temperatura', 'BĹÄ„D', 'danger', 'Brak poprawnego odczytu DS18B20'));
    } else if (targetTemp === null || hysteresis === null || hysteresis <= 0) {
        items.push(buildModuleHealthItem('Temperatura', 'UWAGA', 'warn', 'Odczyt jest, ale cel lub histereza sÄ… niepoprawne'));
    } else {
        items.push(buildModuleHealthItem('Temperatura', 'OK', 'ok', `${Number(temperature.current).toFixed(1)}Â°C, cel ${targetTemp.toFixed(1)}Â°C`));
    }

    const heaterMode = Number(schedule.heaterMode);
    if (!!relays.heater && !currentTempOk) {
        items.push(buildModuleHealthItem('GrzaĹ‚ka', 'BĹÄ„D', 'danger', 'PrzekaĹşnik aktywny bez poprawnego pomiaru temperatury'));
    } else if (heaterMode === 1) {
        items.push(buildModuleHealthItem('GrzaĹ‚ka', 'OFF', 'idle', 'Automatyka temperatury wyĹ‚Ä…czona'));
    } else {
        items.push(buildModuleHealthItem('GrzaĹ‚ka', relays.heater ? 'AKTYWNA' : 'OK', relays.heater ? 'warn' : 'ok', relays.heater ? 'Dogrzewanie trwa' : 'Czeka na prĂłg temperatury'));
    }

    const lightMode = Number(schedule.lightMode);
    if (lightMode === 2) {
        items.push(buildModuleHealthItem('ĹšwiatĹ‚o gĹ‚Ăłwne', 'OFF', 'idle', 'Tryb rÄ™cznie wyĹ‚Ä…czony'));
    } else {
        items.push(buildModuleHealthItem('ĹšwiatĹ‚o gĹ‚Ăłwne', relays.light ? 'AKTYWNE' : 'OK', relays.light ? 'ok' : 'idle', lightMode === 1 ? 'Tryb zawsze wĹ‚Ä…czony' : 'Sterowane oknem pracy akwarium'));
    }

    const plantScheduleMode = data.schedules?.plant_light?.mode || 'schedule';
    if (plantScheduleMode === 'always_off') {
        items.push(buildModuleHealthItem('ĹšwiatĹ‚o roĹ›linne', 'OFF', 'idle', 'Tryb rÄ™cznie wyĹ‚Ä…czony'));
    } else {
        items.push(buildModuleHealthItem('ĹšwiatĹ‚o roĹ›linne', relays.plantLight ? 'AKTYWNE' : 'OK', relays.plantLight ? 'ok' : 'idle', plantScheduleMode === 'always_on' ? 'Tryb zawsze wĹ‚Ä…czony' : 'Osobny harmonogram roĹ›linny'));
    }

    const pumpMode = Number(schedule.filterMode);
    if (pumpMode === 2) {
        items.push(buildModuleHealthItem('Filtr', 'OFF', 'idle', 'Tryb rÄ™cznie wyĹ‚Ä…czony'));
    } else {
        items.push(buildModuleHealthItem('Filtr', relays.pump ? 'AKTYWNY' : 'OK', 'ok', pumpMode === 1 ? 'Tryb zawsze wĹ‚Ä…czony' : 'Sterowany harmonogramem'));
    }

    const airMode = Number(schedule.airMode);
    const airPercent = clamp(Number(relays.aerationPercent || 0), 0, 100);
    if (airMode === 2) {
        items.push(buildModuleHealthItem('Napowietrzanie', 'OFF', 'idle', 'Tryb rÄ™cznie wyĹ‚Ä…czony'));
    } else {
        items.push(buildModuleHealthItem('Napowietrzanie', airPercent > 0 ? 'AKTYWNE' : 'OK', airPercent > 0 ? 'ok' : 'idle', airPercent > 0 ? `Otwarcie ${airPercent}%` : 'Czeka na harmonogram'));
    }

    const feedResult = normalizeFeedResultCode(feeding.lastResult);
    if (feeding.active) {
        items.push(buildModuleHealthItem('Karmnik', 'AKTYWNY', 'warn', 'Cykl karmienia trwa'));
    } else if (feedResult && feedResult !== 'ok') {
        items.push(buildModuleHealthItem('Karmnik', 'BĹÄ„D', 'danger', describeFeedResult(feedResult).message));
    } else if (Number(feeding.freq) <= 0) {
        items.push(buildModuleHealthItem('Karmnik', 'OFF', 'idle', 'Automatyczne karmienie wyĹ‚Ä…czone'));
    } else {
        items.push(buildModuleHealthItem('Karmnik', 'OK', 'ok', describeFeedSchedule(feeding)));
    }

    if (network.serviceModePending || network.staConnecting || network.timeSyncInProgress) {
        items.push(buildModuleHealthItem('SieÄ‡', 'OCZEKUJE', 'warn', 'Trwa start WiFi lub synchronizacja czasu'));
    } else if (network.staConnected) {
        const rssi = toFiniteNumber(network.rssi);
        items.push(buildModuleHealthItem('SieÄ‡', 'OK', 'ok', `STA ${rssi === null ? '--' : `${Math.round(rssi)} dBm`}`));
    } else if (network.apMode) {
        items.push(buildModuleHealthItem('SieÄ‡', 'AP', 'warn', 'DziaĹ‚a tryb awaryjny AP'));
    } else {
        items.push(buildModuleHealthItem('SieÄ‡', 'OFF', 'idle', 'Radio wyĹ‚Ä…czone lub poza sesjÄ… WiFi'));
    }

    const clockOk = !!formatControllerClock(data.clock || {});
    if (clockOk) {
        items.push(buildModuleHealthItem('RTC / czas', 'OK', 'ok', network.lastTimeSyncStatus || 'Czas dostÄ™pny'));
    } else if (network.timeSyncInProgress) {
        items.push(buildModuleHealthItem('RTC / czas', 'OCZEKUJE', 'warn', 'Synchronizacja czasu trwa'));
    } else {
        items.push(buildModuleHealthItem('RTC / czas', 'UWAGA', 'warn', 'Brak poprawnego czasu z RTC/sterownika'));
    }

    const batteryPercent = toFiniteNumber(battery.percent);
    if (batteryPercent === null) {
        items.push(buildModuleHealthItem('Zasilanie', 'OCZEKUJE', 'idle', 'Brak pomiaru baterii'));
    } else if (batteryPercent <= 15) {
        items.push(buildModuleHealthItem('Zasilanie', 'BĹÄ„D', 'danger', `Bateria ${Math.round(batteryPercent)}%`));
    } else if (batteryPercent <= 35) {
        items.push(buildModuleHealthItem('Zasilanie', 'UWAGA', 'warn', `Bateria ${Math.round(batteryPercent)}%`));
    } else {
        const blockers = Array.isArray(system.sleepBlockers) ? system.sleepBlockers : [];
        items.push(buildModuleHealthItem('Zasilanie', 'OK', 'ok', blockers.length ? `Sleep blokuje: ${blockers.map(formatSleepBlockerLabel).join(', ')}` : 'Gotowe do light sleep'));
    }

    if (criticalLogs > 0) {
        items.push(buildModuleHealthItem('Logi', 'UWAGA', 'warn', `Krytyczne wpisy: ${criticalLogs}`));
    } else {
        items.push(buildModuleHealthItem('Logi', 'OK', 'ok', 'Brak krytycznych wpisĂłw w panelu'));
    }

    items.push(buildModuleHealthItem('Portal SD', 'OK', 'ok', 'UI 20260618modules2, pliki statyczne z karty'));
    container.innerHTML = items.join('');
}

function renderDiagnosticsPanel(data) {
    const system = data.system || {};
    const network = data.network || {};

    // Renderowanie przestrzeni karty SD
    const sdMounted = data.sd_mounted ?? false;
    const sdStatusEl = document.getElementById('sd-status');
    const sdFreeEl = document.getElementById('sd-free-space');
    const sdTotalEl = document.getElementById('sd-total-space');

    if (sdStatusEl) {
        sdStatusEl.textContent = sdMounted ? 'Zamontowana' : 'Nie podĹ‚Ä…czona';
        sdStatusEl.style.color = sdMounted ? 'var(--success-color)' : '#ef4444';
    }
    if (sdFreeEl) {
        const freeBytes = toFiniteNumber(data.sd_free_bytes);
        sdFreeEl.textContent = freeBytes === null ? '--' : `${(freeBytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
    }
    if (sdTotalEl) {
        const totalBytes = toFiniteNumber(data.sd_total_bytes);
        sdTotalEl.textContent = totalBytes === null ? '--' : `${(totalBytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
    }

    setText('system-uptime', formatDuration(system.uptimeSeconds));
    setText('system-reset-reason', system.resetReason || 'unknown');
    setText('system-reset-count', String(Math.max(0, Math.trunc(Number(system.resetCount) || 0))));
    const powerMode = system.powerMode || 'unknown';
    setText('power-mode-state', powerMode);

    const freeHeap = toFiniteNumber(system.freeHeap ?? data.heap_free);
    const largestHeap = toFiniteNumber(system.largestHeap ?? data.heap_largest);
    const freeHeapKb = freeHeap === null ? '-- KB' : `${(freeHeap / 1024).toFixed(1)} KB`;
    const largestHeapKb = largestHeap === null ? '-- KB' : `${(largestHeap / 1024).toFixed(1)} KB`;
    setText('system-free-heap', freeHeapKb);
    setText('system-largest-block', largestHeapKb);

    const blockers = Array.isArray(system.sleepBlockers) ? system.sleepBlockers : [];
    const blockersText = blockers.length === 0
        ? 'gotowy do light sleep'
        : blockers.map(formatSleepBlockerLabel).join(', ');
    setText(
        'power-sleep-blockers',
        blockersText
    );

    const lastSyncLabel = network.lastTimeSyncOk
        ? formatEpoch(network.lastTimeSyncEpoch, {
            fallback: '',
            includeDate: true,
            includeSeconds: false
        })
        : '';
    const ntpSummary = network.timeSyncInProgress
        ? 'Synchronizacja trwa'
        : (network.lastTimeSyncStatus || 'Brak danych');
    setText('system-ntp-status', ntpSummary);
    if (lastSyncLabel) {
        setText('system-ntp-status', `${ntpSummary} / ${lastSyncLabel}`);
    }

    const rssi = toFiniteNumber(network.rssi);
    const clients = Math.max(0, Math.trunc(Number(network.clients) || 0));
    const networkSummary = network.staConnected
        ? `STA ${rssi === null ? '--' : `${Math.round(rssi)} dBm`} / AP clients ${clients}`
        : `STA offline / AP clients ${clients}`;
    setText('system-network-quality', networkSummary);
    setText('power-network-quality', networkSummary);

    let apIdleText = 'AP inactive';
    if (network.apMode) {
        apIdleText = network.apIdleCountdownActive
            ? `Auto-stop za ${formatCountdownMs(network.apIdleRemainingMs)}`
            : 'AP aktywne, klient polaczony';
    }
    setText('system-ap-idle', apIdleText);
    setText('power-ap-idle', apIdleText);
    renderModuleEdgeCases(data);
}

function normalizeFirmwareVersion(version) {
    const raw = (version || '').trim();
    if (!raw) return 'v--';
    return raw.startsWith('v') || raw.startsWith('V') ? raw : `v${raw}`;
}

function renderFirmwareInfo(firmware) {
    const versionText = normalizeFirmwareVersion(firmware?.version);
    const tooltipParts = [
        firmware?.buildRef ? `Build: ${firmware.buildRef}` : '',
        firmware?.buildDate && firmware?.buildTime ? `Kompilacja: ${firmware.buildDate} ${firmware.buildTime}` : '',
        firmware?.idfVersion ? `IDF: ${firmware.idfVersion}` : ''
    ].filter(Boolean);
    const tooltip = tooltipParts.join(' | ');

    ['sidebar-firmware-version', 'system-firmware-version'].forEach((id) => {
        const el = document.getElementById(id);
        if (!el) return;
        el.textContent = versionText;
        el.title = tooltip;
    });

    const buildDateText = firmware?.buildDate && firmware?.buildTime
        ? `${firmware.buildDate} ${firmware.buildTime}`
        : 'Build dev';
    setText('system-build-date', buildDateText);
}

function setSettingsNetworkStatus(message, tone = 'muted') {
    setInlineStatus('settings-network-status', message, tone);
    const current = document.getElementById('settings-strip-network')?.textContent || 'Siec';
    setCommandStatus('settings-strip-network', current, message, mapInlineToneToCommandTone(tone));
}

function setScheduleStatus(message, tone = 'muted') {
    setInlineStatus('schedule-save-status', message, tone);
    const normalized = mapInlineToneToCommandTone(tone);
    const title = tone === 'success'
        ? 'Zsynchronizowane'
        : (tone === 'error'
            ? 'Blad zapisu'
            : (tone === 'warning' ? 'Czesciowo' : 'Zmiany'));
    setCommandStatus('schedule-strip-save', title, message, normalized);
}

function setTemperatureStatus(message, tone = 'muted') {
    setInlineStatus('settings-temp-status', message, tone);
    const current = document.getElementById('settings-strip-temperature')?.textContent || 'Temperatura';
    setCommandStatus('settings-strip-temperature', current, message, mapInlineToneToCommandTone(tone));
}

function setDeviceActionStatus(message, tone = 'muted') {
    setInlineStatus('device-actions-status', message, tone);
}

function describeWifiSession(network) {
    const clients = Math.max(0, Math.trunc(Number(network?.clients) || 0));
    const apCountdown = network?.apIdleCountdownActive
        ? ` Auto-stop AP za ${formatCountdownMs(network?.apIdleRemainingMs)}.`
        : '';

    if (network?.serviceModePending) {
        return {
            active: true,
            startDisabled: true,
            label: 'Start WiFi',
            message: 'Polecenie przyjete. Zadanie sieciowe za chwile rozpocznie probe STA/AP.',
            startLabel: 'Start',
            stopLabel: 'Anuluj start'
        };
    }

    if (network?.staConnecting) {
        return {
            active: true,
            startDisabled: true,
            label: 'Laczenie ze STA',
            message: 'Sterownik laczy sie z zapisanym SSID. Przy niepowodzeniu przejdzie do AP awaryjnego.',
            startLabel: 'Laczenie',
            stopLabel: 'Przerwij WiFi'
        };
    }

    if (network?.staConnected) {
        const ssid = network?.staSsid || network?.configuredStaSsid || 'router';
        return {
            active: true,
            label: network?.apMode ? 'STA + AP' : 'STA online',
            message: `Polaczono z ${ssid}.`,
            startLabel: 'Ponow WiFi',
            stopLabel: 'Wylacz WiFi'
        };
    }

    if (network?.apMode) {
        const ssid = network?.configuredApSsid || network?.ssid || 'AP';
        return {
            active: true,
            label: 'AP awaryjne',
            message: `AP ${ssid} aktywne, klientow: ${clients}.${apCountdown}`,
            startLabel: 'Sprobuj STA',
            stopLabel: 'Wylacz AP'
        };
    }

    if (network?.serviceMode) {
        return {
            active: true,
            label: 'Sesja WiFi',
            message: 'Sesja WiFi jest aktywna. Sterownik oczekuje na stan STA albo AP.',
            startLabel: 'Restart WiFi',
            stopLabel: 'Wylacz WiFi'
        };
    }

    return {
        active: false,
        label: 'Radio OFF',
        message: 'Wlacz WiFi, aby sprobowac polaczenia STA i awaryjnie uruchomic AP.',
        startLabel: 'Wlacz WiFi',
        stopLabel: 'WiFi wylaczone'
    };
}

function renderSettingsNetworkPanel(network) {
    setInputValueIfClean('settings-active-sta-ssid', network?.staSsid || '-');
    setInputValueIfClean('settings-network-ip', network?.ip || '-');
    setInputValueIfClean('settings-sta-ssid', network?.configuredStaSsid || '');
    setInputValueIfClean('settings-ap-ssid', network?.configuredApSsid || '');

    const state = describeWifiSession(network || {});
    const startBtn = document.getElementById('start-wifi-session-btn');
    const stopBtn = document.getElementById('stop-wifi-session-btn');

    setText('settings-network-session-label', state.label);
    setText('settings-network-session-status', state.message);
    setCommandStatus(
        'settings-strip-network',
        state.label,
        network?.ip ? `${network.ip} / ${network?.configuredStaSsid || network?.staSsid || 'STA'}` : state.message,
        network?.staConnected ? 'ok' : (network?.apMode || network?.staConnecting || network?.serviceModePending ? 'warn' : 'neutral')
    );

    if (startBtn && startBtn.dataset.busy !== '1') {
        startBtn.disabled = !!state.startDisabled;
        startBtn.textContent = state.startLabel;
    }

    if (stopBtn && stopBtn.dataset.busy !== '1') {
        stopBtn.disabled = !state.active;
        stopBtn.textContent = state.stopLabel;
    }
}

function validateSsidField(value, label, required, maxLength = 32) {
    const normalized = String(value || '').trim();
    if (required && normalized.length === 0) {
        return `${label}: pole nie moze byc puste.`;
    }
    if (normalized.length > maxLength) {
        return `${label}: maksymalnie ${maxLength} znakow.`;
    }
    return '';
}

function validateWifiPasswordField(value, label) {
    const password = String(value || '');
    if (!password) {
        return '';
    }
    if (password.length < 8 || password.length > 63) {
        return `${label}: hasĹ‚o WPA musi mieÄ‡ od 8 do 63 znakĂłw.`;
    }
    return '';
}

const DISPLAY_PROFILE_VALUES = ['always_on', 'timeout_60s', 'always_off'];
const DISPLAY_PROFILE_LABELS = {
    always_on: 'Zawsze ON',
    timeout_60s: 'Timeout 60s',
    always_off: 'Always OFF'
};
const LEAK_ACTION_VALUES = ['alarm', 'disable_valves', 'disable_all'];

function normalizeSelectValue(value, allowedValues, fallback) {
    const normalized = String(value || '').trim();
    return allowedValues.includes(normalized) ? normalized : fallback;
}

function normalizeBrightnessPercent(value) {
    const numeric = toFiniteNumber(value);
    if (numeric === null) {
        return 100;
    }
    return clamp(Math.round(clamp(numeric, 10, 100) / 5) * 5, 10, 100);
}

function readBoundedNumberInput(input, min, max, label, integer = false) {
    if (!input) {
        return {
            ok: false,
            value: null,
            input: null,
            message: `${label}: brak pola formularza.`
        };
    }

    const numeric = toFiniteNumber(input.value);
    if (numeric === null) {
        return {
            ok: false,
            value: null,
            input,
            message: `${label}: wpisz poprawna liczbe.`
        };
    }

    const normalized = integer ? Math.trunc(numeric) : numeric;
    if (normalized < min || normalized > max) {
        return {
            ok: false,
            value: normalized,
            input,
            message: `${label}: dozwolony zakres to ${min}-${max}.`
        };
    }

    return {
        ok: true,
        value: normalized,
        input,
        message: ''
    };
}

function readSelectInput(input, allowedValues, label) {
    if (!input) {
        return {
            ok: false,
            value: '',
            input: null,
            message: `${label}: brak pola formularza.`
        };
    }

    const normalized = String(input.value || '').trim();
    if (!allowedValues.includes(normalized)) {
        return {
            ok: false,
            value: normalized,
            input,
            message: `${label}: nieprawidlowa wartosc.`
        };
    }

    return {
        ok: true,
        value: normalized,
        input,
        message: ''
    };
}

function focusInvalidInput(input) {
    if (input && typeof input.focus === 'function') {
        input.focus();
    }
}

function markFieldsClean(ids) {
    ids.forEach((id) => {
        const el = document.getElementById(id);
        if (el) {
            el.dataset.dirty = '0';
        }
    });
}

async function runWifiSessionAction(mode) {
    const startBtn = document.getElementById('start-wifi-session-btn');
    const stopBtn = document.getElementById('stop-wifi-session-btn');
    const isStart = mode === 'start';
    const action = isStart ? 'wifi_session_start' : 'wifi_session_stop';
    const busyLabel = isStart ? 'Uruchamianie' : 'Wylaczanie';

    if ((isStart && startBtn?.dataset.busy === '1') || (!isStart && stopBtn?.dataset.busy === '1')) {
        return;
    }

    [startBtn, stopBtn].forEach((button) => {
        if (!button) return;
        button.dataset.busy = '1';
        button.disabled = true;
    });
    if (isStart && startBtn) {
        startBtn.textContent = busyLabel;
    }
    if (!isStart && stopBtn) {
        stopBtn.textContent = busyLabel;
    }

    setSettingsNetworkStatus(
        isStart
            ? 'Wysylam polecenie wlaczenia WiFi. Sterownik sprobuje STA, potem AP.'
            : 'Wysylam polecenie wylaczenia aktywnej sesji WiFi.',
        'muted'
    );

    try {
        const response = await sendAction(action, {}, { showSaveAnimation: false });
        setSettingsNetworkStatus(
            response?.message || (isStart
                ? 'Polecenie wlaczenia WiFi wyslane.'
                : 'Polecenie wylaczenia WiFi wyslane.'),
            isStart ? 'success' : 'warning'
        );
        setTimeout(() => fetchStatus(true), isStart ? 1400 : 700);
    } catch (error) {
        setSettingsNetworkStatus(`Blad sterowania WiFi: ${describeRequestError(error)}`, 'error');
    } finally {
        setTimeout(() => {
            [startBtn, stopBtn].forEach((button) => {
                if (button) button.dataset.busy = '0';
            });
            renderSettingsNetworkPanel(lastStatusData?.network || {});
        }, 1800);
    }
}

async function saveNetworkSettings() {
    const staSsidInput = document.getElementById('settings-sta-ssid');
    const staPasswordInput = document.getElementById('settings-sta-password');
    const button = document.getElementById('save-network-btn');
    if (!staSsidInput || !staPasswordInput || !button) return;

    const payload = {
        staSsid: (staSsidInput.value || '').trim()
    };

    const staPassword = (staPasswordInput.value || '').trim();
    if (staPassword) payload.staPassword = staPassword;

    const validationError =
        validateSsidField(payload.staSsid, 'SSID STA', true, 32) ||
        validateWifiPasswordField(staPassword, 'Haslo STA');

    if (validationError) {
        setSettingsNetworkStatus(validationError, 'error');
        return;
    }

    button.disabled = true;
    setSettingsNetworkStatus('Zapisywanie ustawien WiFi trwa.', 'muted');

    try {
        await sendAction('save_network', payload);
        ['settings-sta-ssid', 'settings-sta-password'].forEach((id) => {
            const el = document.getElementById(id);
            if (el) el.dataset.dirty = '0';
        });
        staPasswordInput.value = '';
        setSettingsNetworkStatus('Zapisano profil STA na SD. Zostanie uzyty przy kolejnej sesji WiFi.', 'success');
        await fetchStatus(true);
    } catch (error) {
        setSettingsNetworkStatus(`Blad zapisu WiFi: ${error?.message || 'nieznany blad'}`, 'error');
    } finally {
        button.disabled = false;
    }
}

async function saveTemperatureSettings() {
    const enabledInput = document.getElementById('settings-temp-enabled');
    const targetInput = document.getElementById('settings-temp-target');
    const hystInput = document.getElementById('settings-temp-hyst');
    const button = document.getElementById('save-temperature-btn');
    if (!enabledInput || !targetInput || !hystInput || !button) return;

    const payload = {
        heaterMode: enabledInput.checked ? '0' : '1',
        target: String(targetInput.value || ''),
        hysteresis: String(hystInput.value || ''),
        targetTemp: String(targetInput.value || ''),
        tempHyst: String(hystInput.value || '')
    };

    const target = toFiniteNumber(payload.target);
    const hysteresis = toFiniteNumber(payload.hysteresis);
    if (target === null || target < 18 || target > 30) {
        setTemperatureStatus('Temperatura docelowa musi byc w zakresie 18-30Â°C.', 'error');
        targetInput.focus();
        return;
    }
    if (hysteresis === null || hysteresis < 0.1 || hysteresis > 5) {
        setTemperatureStatus('Histereza musi byc w zakresie 0.1-5.0Â°C.', 'error');
        hystInput.focus();
        return;
    }

    button.disabled = true;
    setTemperatureStatus('Zapisywanie ustawien temperatury trwa.', 'muted');

    try {
        const response = await sendAction('save_temperature', payload, { showSaveAnimation: false });
        ['settings-temp-enabled', 'settings-temp-target', 'settings-temp-hyst'].forEach((id) => {
            const el = document.getElementById(id);
            if (el) el.dataset.dirty = '0';
        });
        setTemperatureStatus(
            response?.code === 'settings_partial'
                ? 'Zapisano po korekcie wartosci spoza profilu walidacji.'
                : 'Zapisano ustawienia temperatury.',
            response?.code === 'settings_partial' ? 'warning' : 'success'
        );
        await fetchStatus(true);
    } catch (error) {
        setTemperatureStatus(`Blad ustawien temperatury: ${describeRequestError(error)}`, 'error');
    } finally {
        button.disabled = false;
    }
}

async function syncBrowserTime() {
    const ntpBtn = document.getElementById('sync-ntp-btn');
    const browserBtn = document.getElementById('sync-browser-time-btn');
    const epoch = Math.trunc(Date.now() / 1000);

    ntpBtn && (ntpBtn.disabled = true);
    browserBtn && (browserBtn.disabled = true);
    setInlineStatus('settings-clock-status', 'Wysylam czas z przegladarki do sterownika.', 'muted');

    try {
        if (!isAdminAuthenticated()) {
            await loginAsAdmin();
        }
        const pin = getAdminPinForRequest();
        if (!pin) {
            const error = new Error('Wymagane logowanie admina.');
            error.code = 'admin_required';
            throw error;
        }

        const response = await fetch(API_SETTIME, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: new URLSearchParams({ epoch: String(epoch), pin }).toString()
        });
        const message = await response.text();
        if (!response.ok) {
            if (response.status === 403) {
                logoutAdmin();
            }
            const error = new Error(message || 'request_failed');
            error.code = message || 'request_failed';
            throw error;
        }
        await fetchStatus(true);
        setInlineStatus('settings-clock-status', 'Czas sterownika zsynchronizowany z przegladarka.', 'success');
    } catch (error) {
        setInlineStatus('settings-clock-status', `Blad synchronizacji czasu: ${describeRequestError(error)}`, 'error');
    } finally {
        ntpBtn && (ntpBtn.disabled = false);
        browserBtn && (browserBtn.disabled = false);
    }
}

async function syncTimeWithNtp() {
    const ntpBtn = document.getElementById('sync-ntp-btn');
    const browserBtn = document.getElementById('sync-browser-time-btn');

    ntpBtn && (ntpBtn.disabled = true);
    browserBtn && (browserBtn.disabled = true);
    setInlineStatus('settings-clock-status', 'Uruchamiam reczna synchronizacje NTP.', 'muted');

    try {
        const response = await sendAction('sync_time_ntp', {}, { showSaveAnimation: false });
        await fetchStatus(true);
        setInlineStatus(
            'settings-clock-status',
            response?.message || 'Synchronizacja NTP zakoĹ„czona powodzeniem.',
            'success'
        );
    } catch (error) {
        await fetchStatus(true);
        setInlineStatus(
            'settings-clock-status',
            error?.message || `Blad synchronizacji NTP (${error?.code || 'request_failed'}).`,
            error?.code === 'busy' ? 'warning' : 'error'
        );
    } finally {
        ntpBtn && (ntpBtn.disabled = false);
        browserBtn && (browserBtn.disabled = false);
    }
}

async function runDeviceAction(action, confirmationMessage, successMessage) {
    if (!window.confirm(confirmationMessage)) {
        return;
    }

    const restartBtn = document.getElementById('restart-device-btn');
    const resetBtn = document.getElementById('factory-reset-btn');
    let unlockButtons = true;
    restartBtn && (restartBtn.disabled = true);
    resetBtn && (resetBtn.disabled = true);
    setDeviceActionStatus('Wysylam polecenie do sterownika.', 'muted');

    try {
        await sendAction(action, {}, { showSaveAnimation: false });
        setDeviceActionStatus(successMessage, action === 'factory_reset' ? 'warning' : 'success');
        setBackendState(false);
        unlockButtons = false;
    } catch (error) {
        setDeviceActionStatus(`BĹ‚Ä…d akcji urzÄ…dzenia: ${describeRequestError(error)}`, 'error');
    } finally {
        if (unlockButtons) {
            restartBtn && (restartBtn.disabled = false);
            resetBtn && (resetBtn.disabled = false);
        }
    }
}


function updateDisplaySettingsUIState() {
    const autoCheckbox = document.getElementById('settings-display-auto');
    const brightnessLabel = document.getElementById('settings-display-brightness-label');
    const statusNote = document.getElementById('settings-display-status-note');
    const slider = document.getElementById('settings-display-brightness');
    if (!autoCheckbox) return;

    const isAuto = autoCheckbox.checked;
    if (brightnessLabel) {
        brightnessLabel.textContent = isAuto ? 'Maksymalna jasnoĹ›Ä‡ (Auto):' : 'StaĹ‚a jasnoĹ›Ä‡ (Manual):';
    }
    if (statusNote) {
        statusNote.textContent = isAuto
            ? 'JasnoĹ›Ä‡ automatyczna dostosuje podĹ›wietlenie pĹ‚ynnie do otoczenia (od 15% do ustawionej wartoĹ›ci maksymalnej).'
            : 'Ekran bÄ™dzie Ĺ›wieciĹ‚ ze staĹ‚Ä…, wybranÄ… jasnoĹ›ciÄ… podĹ›wietlenia.';
    }
    if (slider) {
        slider.style.opacity = isAuto ? '0.6' : '1.0';
    }
}

// Ustawienia wyswietlacza CYD
function renderDisplaySettingsPanel(data) {
    const display = data.display || {};
    const auto = display.autoBrightness ?? true;
    const profile = normalizeSelectValue(display.profile, DISPLAY_PROFILE_VALUES, 'always_on');
    const brightness = normalizeBrightnessPercent(display.brightness);

    setCheckboxIfClean('settings-display-auto', auto);
    setInputValueIfClean('settings-display-profile', profile);
    setNumericValueIfClean('settings-display-brightness', brightness);

    const brightnessValEl = document.getElementById('settings-display-brightness-val');
    if (brightnessValEl) {
        brightnessValEl.textContent = `${brightness}%`;
    }

    setCommandStatus(
        'settings-strip-display',
        DISPLAY_PROFILE_LABELS[profile],
        `Jasnosc: ${brightness}% | Auto: ${auto ? 'Tak' : 'Nie'}`,
        'info'
    );

    updateDisplaySettingsUIState();
}

async function saveDisplaySettings() {
    const autoInput = document.getElementById('settings-display-auto');
    const profileInput = document.getElementById('settings-display-profile');
    const brightnessInput = document.getElementById('settings-display-brightness');
    const brightnessValEl = document.getElementById('settings-display-brightness-val');
    const button = document.getElementById('save-display-btn');
    if (!autoInput || !profileInput || !brightnessInput || !button) return;

    const profileResult = readSelectInput(profileInput, DISPLAY_PROFILE_VALUES, 'Profil ekranu');
    if (!profileResult.ok) {
        setDisplayStatus(profileResult.message, 'error');
        focusInvalidInput(profileResult.input);
        return;
    }

    const brightnessResult = readBoundedNumberInput(brightnessInput, 10, 100, 'Jasnosc ekranu', true);
    if (!brightnessResult.ok) {
        setDisplayStatus(brightnessResult.message, 'error');
        focusInvalidInput(brightnessResult.input);
        return;
    }

    const brightness = normalizeBrightnessPercent(brightnessResult.value);
    brightnessInput.value = String(brightness);
    if (brightnessValEl) {
        brightnessValEl.textContent = `${brightness}%`;
    }

    const payload = {
        autoBrightness: autoInput.checked ? '1' : '0',
        profile: profileResult.value,
        brightness: String(brightness)
    };

    setDisplayStatus('Zapisywanie ustawieĹ„ ekranu...', 'warning');
    button.disabled = true;

    try {
        await sendAction('save_display', payload);
        setDisplayStatus('Ustawienia ekranu zapisane.', 'success');
        markFieldsClean(['settings-display-auto', 'settings-display-profile', 'settings-display-brightness']);
        await fetchStatus(true);
    } catch (error) {
        setDisplayStatus(`BĹ‚Ä…d: ${describeRequestError(error)}`, 'error');
    } finally {
        button.disabled = false;
    }
}

function setDisplayStatus(message, tone = 'muted') {
    setInlineStatus('settings-display-status', message, tone);
    const current = document.getElementById('settings-strip-display')?.textContent || 'Ekran';
    setCommandStatus('settings-strip-display', current, message, mapInlineToneToCommandTone(tone));
}

function initDisplaySettingsListeners() {
    const slider = document.getElementById('settings-display-brightness');
    const valText = document.getElementById('settings-display-brightness-val');
    slider?.addEventListener('input', () => {
        if (valText) {
            valText.textContent = `${slider.value}%`;
        }
        slider.dataset.dirty = '1';
        setDisplayStatus('Masz niezapisane zmiany ustawieĹ„ ekranu.', 'muted');
    });

    const autoToggle = document.getElementById('settings-display-auto');
    autoToggle?.addEventListener('change', () => {
        autoToggle.dataset.dirty = '1';
        setDisplayStatus('Masz niezapisane zmiany ustawieĹ„ ekranu.', 'muted');
        updateDisplaySettingsUIState();
    });

    const profileSelect = document.getElementById('settings-display-profile');
    profileSelect?.addEventListener('change', () => {
        profileSelect.dataset.dirty = '1';
        setDisplayStatus('Masz niezapisane zmiany ustawieĹ„ ekranu.', 'muted');
    });
}

// ObsĹ‚uga nowej zakĹ‚adki ModuĹ‚Ăłw (GrzaĹ‚ka, CO2, Dolewka, Wyciek)
function renderModulesTab(data) {
    const modules = data.modules || {};
    const config = data.config || {};
    const sensors = data.sensors || {};

    // 1. GrzaĹ‚ka
    const heaterOn = modules.heater_on ?? false;
    const heaterEnabled = modules.heater_enabled ?? true;
    const heaterStatusEl = document.getElementById('module-status-heater');
    if (heaterStatusEl) {
        if (!heaterEnabled) {
            heaterStatusEl.textContent = 'WYĹÄ„CZONA';
            heaterStatusEl.style.color = 'var(--text-muted)';
        } else {
            heaterStatusEl.textContent = heaterOn ? 'GRZANIE' : 'CZUWANIE';
            heaterStatusEl.style.color = heaterOn ? 'var(--accent-orange)' : 'var(--success-color)';
        }
    }

    // 2. CO2
    const co2Enabled = modules.co2_enabled ?? false;
    const currentPh = toFiniteNumber(sensors.ph);
    const targetPh = toFiniteNumber(config.co2TargetPh ?? data.co2?.targetPh ?? modules.co2_target_ph ?? 6.8) ?? 6.8;
    const co2StatusEl = document.getElementById('module-status-co2');
    if (co2StatusEl) {
        if (!co2Enabled) {
            co2StatusEl.textContent = 'WYĹÄ„CZONY';
            co2StatusEl.style.color = 'var(--text-muted)';
        } else {
            const dosing = currentPh !== null && currentPh > targetPh;
            co2StatusEl.textContent = dosing ? 'DOZOWANIE' : 'ZAMKNIÄTY';
            co2StatusEl.style.color = dosing ? 'var(--accent-cyan)' : 'var(--success-color)';
        }
    }
    setCheckboxIfClean('settings-co2-enabled', co2Enabled);
    setNumericValueIfClean('settings-co2-ph-target', targetPh, 2);
    setNumericValueIfClean('settings-co2-limit', config.co2MaxTimeMin ?? data.co2?.limitMinutes ?? 180, 0);

    // 3. ATO (Poziom wody)
    const waterEnabled = modules.water_level_enabled ?? false;
    const levelLabel = document.getElementById('settings-water-level-label');
    const isWaterHigh = sensors.ph_valid ?? true; // mock sensor level high/low
    if (levelLabel) {
        levelLabel.value = isWaterHigh ? 'OK (WYSOKI)' : 'NISKI (DOLEWANIE)';
        levelLabel.style.color = isWaterHigh ? 'var(--success-color)' : 'var(--warning-color)';
    }
    const waterStatusEl = document.getElementById('module-status-water');
    if (waterStatusEl) {
        if (!waterEnabled) {
            waterStatusEl.textContent = 'WYĹÄ„CZONA';
            waterStatusEl.style.color = 'var(--text-muted)';
        } else {
            waterStatusEl.textContent = !isWaterHigh ? 'DOLEWANIE' : 'CZUWANIE';
            waterStatusEl.style.color = !isWaterHigh ? 'var(--accent-blue)' : 'var(--success-color)';
        }
    }
    setCheckboxIfClean('settings-water-enabled', waterEnabled);
    setNumericValueIfClean('settings-water-timeout', config.waterTimeoutSec ?? data.water?.timeoutSec ?? 30, 0);

    // 4. Wyciek (Leak)
    const leakEnabled = modules.leak_enabled ?? false;
    const leakSensorEl = document.getElementById('settings-leak-sensor-label');
    const hasLeak = sensors.ldr_valid === false; // mock leak on invalid ldr
    if (leakSensorEl) {
        leakSensorEl.value = hasLeak ? 'ALARM WYCIEKU!' : 'SUCHY';
        leakSensorEl.style.color = hasLeak ? '#ef4444' : 'var(--success-color)';
    }
    const leakStatusEl = document.getElementById('module-status-leak');
    if (leakStatusEl) {
        if (!leakEnabled) {
            leakStatusEl.textContent = 'NIEAKTYWNE';
            leakStatusEl.style.color = 'var(--text-muted)';
        } else {
            leakStatusEl.textContent = hasLeak ? 'ALARM!' : 'BEZPIECZNY';
            leakStatusEl.style.color = hasLeak ? '#ef4444' : 'var(--success-color)';
        }
    }
    setCheckboxIfClean('settings-leak-enabled', leakEnabled);
    setInputValueIfClean(
        'settings-leak-action',
        normalizeSelectValue(config.leakActionType ?? data.leak?.action ?? 'disable_all', LEAK_ACTION_VALUES, 'disable_all')
    );
}

async function saveCo2Settings() {
    const enabledInput = document.getElementById('settings-co2-enabled');
    const phTargetInput = document.getElementById('settings-co2-ph-target');
    const limitInput = document.getElementById('settings-co2-limit');
    const button = document.getElementById('save-co2-btn');
    if (!enabledInput || !phTargetInput || !limitInput || !button) return;

    const targetResult = readBoundedNumberInput(phTargetInput, 5.0, 8.5, 'Docelowe pH', false);
    if (!targetResult.ok) {
        setCo2Status(targetResult.message, 'error');
        focusInvalidInput(targetResult.input);
        return;
    }

    const limitResult = readBoundedNumberInput(limitInput, 1, 1440, 'Limit dozowania CO2', true);
    if (!limitResult.ok) {
        setCo2Status(limitResult.message, 'error');
        focusInvalidInput(limitResult.input);
        return;
    }

    phTargetInput.value = Number(targetResult.value).toFixed(2);
    limitInput.value = String(limitResult.value);

    const payload = {
        co2Enabled: enabledInput.checked ? '1' : '0',
        targetPh: Number(targetResult.value).toFixed(2),
        co2Limit: String(limitResult.value)
    };

    button.disabled = true;
    setCo2Status('Zapisywanie automatyki CO2...', 'warning');

    try {
        await sendAction('save_co2', payload);
        markFieldsClean(['settings-co2-enabled', 'settings-co2-ph-target', 'settings-co2-limit']);
        setCo2Status('Zapisano automatykÄ™ CO2.', 'success');
        await fetchStatus(true);
    } catch (error) {
        setCo2Status(`BĹ‚Ä…d: ${describeRequestError(error)}`, 'error');
    } finally {
        button.disabled = false;
    }
}

async function saveWaterSettings() {
    const enabledInput = document.getElementById('settings-water-enabled');
    const timeoutInput = document.getElementById('settings-water-timeout');
    const button = document.getElementById('save-water-btn');
    if (!enabledInput || !timeoutInput || !button) return;

    const timeoutResult = readBoundedNumberInput(timeoutInput, 5, 300, 'Limit pompy dolewki', true);
    if (!timeoutResult.ok) {
        setWaterStatus(timeoutResult.message, 'error');
        focusInvalidInput(timeoutResult.input);
        return;
    }
    timeoutInput.value = String(timeoutResult.value);

    const payload = {
        waterEnabled: enabledInput.checked ? '1' : '0',
        waterTimeout: String(timeoutResult.value)
    };

    button.disabled = true;
    setWaterStatus('Zapisywanie automatycznej dolewki...', 'warning');

    try {
        await sendAction('save_water', payload);
        markFieldsClean(['settings-water-enabled', 'settings-water-timeout']);
        setWaterStatus('Zapisano automatycznÄ… dolewkÄ™.', 'success');
        await fetchStatus(true);
    } catch (error) {
        setWaterStatus(`BĹ‚Ä…d: ${describeRequestError(error)}`, 'error');
    } finally {
        button.disabled = false;
    }
}

async function saveLeakSettings() {
    const enabledInput = document.getElementById('settings-leak-enabled');
    const actionSelect = document.getElementById('settings-leak-action');
    const button = document.getElementById('save-leak-btn');
    if (!enabledInput || !actionSelect || !button) return;

    const actionResult = readSelectInput(actionSelect, LEAK_ACTION_VALUES, 'Akcja wycieku');
    if (!actionResult.ok) {
        setLeakStatus(actionResult.message, 'error');
        focusInvalidInput(actionResult.input);
        return;
    }

    const payload = {
        leakEnabled: enabledInput.checked ? '1' : '0',
        leakAction: actionResult.value
    };

    button.disabled = true;
    setLeakStatus('Zapisywanie ustawieĹ„ zabezpieczeĹ„...', 'warning');

    try {
        await sendAction('save_leak', payload);
        markFieldsClean(['settings-leak-enabled', 'settings-leak-action']);
        setLeakStatus('Zapisano ustawienia zabezpieczeĹ„.', 'success');
        await fetchStatus(true);
    } catch (error) {
        setLeakStatus(`BĹ‚Ä…d: ${describeRequestError(error)}`, 'error');
    } finally {
        button.disabled = false;
    }
}

function setCo2Status(message, tone = 'muted') {
    setInlineStatus('settings-co2-status', message, tone);
}

function setWaterStatus(message, tone = 'muted') {
    setInlineStatus('settings-water-status', message, tone);
}

function setLeakStatus(message, tone = 'muted') {
    setInlineStatus('settings-leak-status', message, tone);
}

// Eksport do okna globalnego
window.renderDisplaySettingsPanel = renderDisplaySettingsPanel;
window.saveDisplaySettings = saveDisplaySettings;
window.setDisplayStatus = setDisplayStatus;
window.initDisplaySettingsListeners = initDisplaySettingsListeners;
window.renderModulesTab = renderModulesTab;
window.saveCo2Settings = saveCo2Settings;
window.saveWaterSettings = saveWaterSettings;
window.saveLeakSettings = saveLeakSettings;
