const API_STATUS = '/api/status';
const API_ACTION = '/api/action';
const API_LOGS = '/api/logs';
const API_EVENTS = '/api/events';
const API_OTA = '/update';
const API_SETTIME = '/settime';
const LOGS_PAGE_SIZE = 10;

let backendConnected = false;
let activeLogType = 'normal';
let cachedLogs = {
    normal: [],
    critical: [],
    counts: { normal: 0, critical: 0 }
};
let logsPage = { normal: 0, critical: 0 };
let deviceClockBaseDate = null;
let deviceClockSyncedAtMs = 0;
let lastStatusData = null;
const OLED_SAVE_ACTIONS = new Set(['save_schedule', 'save_network', 'set_light', 'set_filter']);
const OLED_SAVE_TITLES = {
    save_schedule: 'Zapisywanie harmonogramow',
    save_network: 'Zapisywanie ustawien WiFi',
    set_light: 'Zapisywanie stanu swiatla',
    set_filter: 'Zapisywanie stanu filtra'
};
let oledSaveOverlayActiveCount = 0;
let oledSaveOverlayShownAtMs = 0;
let oledSaveOverlayHideTimer = null;
let eventStream = null;
let eventStreamConnected = false;
let statusPollingTimer = null;
let logsPollingTimer = null;
let feedActionState = {
    awaitingResponse: false,
    awaitingCompletion: false,
    sawActive: false,
    startedAtMs: 0,
    baselineLastFeedEpoch: 0,
    baselineLastResult: ''
};
let feedModalHideTimer = null;

function makeLocalIcon(paths) {
    return `<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">${paths}</svg>`;
}

const LOCAL_ICON_SVGS = {
    'fa-water': makeLocalIcon(`
        <path fill="none" d="M3 7c2 2 4 2 6 0s4-2 6 0 4 2 6 0"/>
        <path fill="none" d="M3 12c2 2 4 2 6 0s4-2 6 0 4 2 6 0"/>
        <path fill="none" d="M3 17c2 2 4 2 6 0s4-2 6 0 4 2 6 0"/>
    `),
    'fa-gauge-high': makeLocalIcon(`
        <path fill="none" d="M4 14a8 8 0 1 1 16 0"/>
        <path fill="none" d="M12 14l4-4"/>
        <circle cx="12" cy="14" r="1" fill="currentColor" stroke="none"/>
    `),
    'fa-calendar-days': makeLocalIcon(`
        <rect x="3" y="5" width="18" height="16" rx="2" fill="none"/>
        <path fill="none" d="M8 3v4M16 3v4M3 10h18"/>
        <circle cx="8" cy="14" r="0.9" fill="currentColor" stroke="none"/>
        <circle cx="12" cy="14" r="0.9" fill="currentColor" stroke="none"/>
        <circle cx="16" cy="14" r="0.9" fill="currentColor" stroke="none"/>
        <circle cx="8" cy="18" r="0.9" fill="currentColor" stroke="none"/>
        <circle cx="12" cy="18" r="0.9" fill="currentColor" stroke="none"/>
        <circle cx="16" cy="18" r="0.9" fill="currentColor" stroke="none"/>
    `),
    'fa-terminal': makeLocalIcon(`
        <path fill="none" d="M4 7l4 4-4 4"/>
        <path fill="none" d="M12 15h8"/>
        <path fill="none" d="M3 20h18"/>
    `),
    'fa-cloud-arrow-up': makeLocalIcon(`
        <path fill="none" d="M7 18a4 4 0 0 1 .9-7.9A5 5 0 0 1 17.5 12H18a3 3 0 0 1 0 6H7z"/>
        <path fill="none" d="M12 16V9"/>
        <path fill="none" d="m9.5 11.5 2.5-2.5 2.5 2.5"/>
    `),
    'fa-gear': makeLocalIcon(`
        <circle cx="12" cy="12" r="3.5" fill="none"/>
        <path fill="none" d="M12 2v2.2M12 19.8V22M4.9 4.9l1.5 1.5M17.6 17.6l1.5 1.5M2 12h2.2M19.8 12H22M4.9 19.1l1.5-1.5M17.6 6.4l1.5-1.5"/>
    `),
    'fa-battery-three-quarters': makeLocalIcon(`
        <rect x="2.5" y="7" width="18" height="10" rx="2" fill="none"/>
        <path fill="none" d="M22 10v4"/>
        <rect x="5" y="9.5" width="11" height="5" rx="1" stroke="none" fill="currentColor"/>
    `),
    'fa-satellite-dish': makeLocalIcon(`
        <path fill="none" d="M5 19a7 7 0 0 1 7-7"/>
        <path fill="none" d="M5 15a11 11 0 0 1 11-11"/>
        <path fill="none" d="M12.5 12.5 19 19"/>
        <circle cx="8" cy="16" r="1.2" fill="currentColor" stroke="none"/>
    `),
    'fa-wifi': makeLocalIcon(`
        <path fill="none" d="M3 9a13 13 0 0 1 18 0"/>
        <path fill="none" d="M6.5 12.5a8 8 0 0 1 11 0"/>
        <path fill="none" d="M10 16a3.2 3.2 0 0 1 4 0"/>
        <circle cx="12" cy="19" r="1" fill="currentColor" stroke="none"/>
    `),
    'fa-lightbulb': makeLocalIcon(`
        <path fill="none" d="M9 18h6"/>
        <path fill="none" d="M10 21h4"/>
        <path fill="none" d="M8 10a4 4 0 1 1 8 0c0 1.6-.8 2.7-1.8 3.7-.7.7-1.2 1.5-1.2 2.3h-2c0-.8-.5-1.6-1.2-2.3C8.8 12.7 8 11.6 8 10z"/>
    `),
    'fa-filter': makeLocalIcon(`
        <path fill="none" d="M4 6h16"/>
        <path fill="none" d="M7 12h10"/>
        <path fill="none" d="M10 18h4"/>
    `),
    'fa-temperature-half': makeLocalIcon(`
        <path fill="none" d="M10 14.5V5a2 2 0 1 1 4 0v9.5a4 4 0 1 1-4 0z"/>
        <path fill="none" d="M12 9v7"/>
    `),
    'fa-wind': makeLocalIcon(`
        <path fill="none" d="M4 9h9a2 2 0 1 0-2-2"/>
        <path fill="none" d="M4 13h13a2 2 0 1 1-2 2"/>
        <path fill="none" d="M4 17h7a2 2 0 1 0-2 2"/>
    `),
    'fa-clock': makeLocalIcon(`
        <circle cx="12" cy="12" r="8.5" fill="none"/>
        <path fill="none" d="M12 7v5l3 2"/>
    `),
    'fa-circle-info': makeLocalIcon(`
        <circle cx="12" cy="12" r="8.5" fill="none"/>
        <path fill="none" d="M12 10v5"/>
        <circle cx="12" cy="7" r="0.8" fill="currentColor" stroke="none"/>
    `),
    'fa-microchip': makeLocalIcon(`
        <rect x="7" y="7" width="10" height="10" rx="2" fill="none"/>
        <path fill="none" d="M9 2v3M15 2v3M9 19v3M15 19v3M2 9h3M2 15h3M19 9h3M19 15h3"/>
    `),
    'fa-spinner': makeLocalIcon(`
        <path fill="none" d="M12 3a9 9 0 1 1-9 9"/>
    `),
    'fa-check-circle': makeLocalIcon(`
        <circle cx="12" cy="12" r="9" fill="none"/>
        <path fill="none" d="m8.5 12 2.3 2.3 4.7-4.7"/>
    `),
    'fa-triangle-exclamation': makeLocalIcon(`
        <path fill="none" d="M12 4 3.5 19h17L12 4z"/>
        <path fill="none" d="M12 9v4"/>
        <circle cx="12" cy="16.2" r="0.8" fill="currentColor" stroke="none"/>
    `),
    'fa-bars': makeLocalIcon(`
        <path fill="none" d="M4 7h16M4 12h16M4 17h16"/>
    `),
    'fa-xmark': makeLocalIcon(`
        <path fill="none" d="M6 6l12 12M18 6 6 18"/>
    `),
    'fa-download': makeLocalIcon(`
        <path fill="none" d="M12 4v10"/>
        <path fill="none" d="m8 10 4 4 4-4"/>
        <path fill="none" d="M5 19h14"/>
    `),
    'fa-file-arrow-up': makeLocalIcon(`
        <path fill="none" d="M8 3h6l4 4v14H6V3z"/>
        <path fill="none" d="M14 3v4h4"/>
        <path fill="none" d="M12 17V9"/>
        <path fill="none" d="m9.5 11.5 2.5-2.5 2.5 2.5"/>
    `),
    'fa-arrows-rotate': makeLocalIcon(`
        <path fill="none" d="M5 9a7 7 0 0 1 12-2"/>
        <path fill="none" d="M17 7v4h-4"/>
        <path fill="none" d="M19 15a7 7 0 0 1-12 2"/>
        <path fill="none" d="M7 17v-4h4"/>
    `),
    'fa-laptop': makeLocalIcon(`
        <rect x="5" y="6" width="14" height="9" rx="1.5" fill="none"/>
        <path fill="none" d="M3 18h18"/>
    `),
    'fa-rotate-right': makeLocalIcon(`
        <path fill="none" d="M18 8V4h-4"/>
        <path fill="none" d="M18 8a7 7 0 1 0 1.5 7"/>
    `),
    'fa-floppy-disk': makeLocalIcon(`
        <path fill="none" d="M5 4h11l3 3v13H5V4z"/>
        <path fill="none" d="M8 4v6h8V4"/>
        <rect x="8" y="14" width="8" height="4" rx="1" fill="none"/>
    `),
    'fa-fish': makeLocalIcon(`
        <path fill="none" d="M4 12c3.5-4 8.5-4 12 0-3.5 4-8.5 4-12 0z"/>
        <path fill="none" d="M16 12l4-3v6l-4-3"/>
        <circle cx="9" cy="11" r="0.8" fill="currentColor" stroke="none"/>
    `),
    'fa-chart-line': makeLocalIcon(`
        <path fill="none" d="M3 3v18h18"/>
        <path fill="none" d="m19 9-5 5-4-4-3 3"/>
    `),
    'fa-circle-notch': makeLocalIcon(`
        <path fill="none" d="M12 3a9 9 0 0 1 9 9"/>
    `)
};

function renderLocalIcon(el) {
    const iconClasses = Array.from(el.classList).find((name) =>
        name.startsWith('fa-') &&
        !['fa-solid', 'fa-regular', 'fa-2x', 'fa-2xl', 'fa-spin', 'fa-bounce'].includes(name)
    );
    const markup = iconClasses ? LOCAL_ICON_SVGS[iconClasses] : '';
    if (markup) {
        el.innerHTML = markup;
    }
}

function initLocalIcons() {
    document.querySelectorAll('i.fa-solid, i.fa-regular').forEach(renderLocalIcon);
}

function getLocalIconMarkup(iconClass, extraClass = '') {
    const iconSvg = LOCAL_ICON_SVGS[iconClass] || '';
    const classes = ['status-icon', extraClass].filter(Boolean).join(' ');
    return `<span class="${classes}" aria-hidden="true">${iconSvg}</span>`;
}

function escapeHtml(value) {
    return String(value ?? '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

function toFiniteNumber(value) {
    const number = Number(value);
    return Number.isFinite(number) ? number : null;
}

function isValidTemperature(value) {
    const numeric = toFiniteNumber(value);
    return numeric !== null && numeric >= -20 && numeric <= 120;
}

function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
}

function formatTwoDigits(value) {
    return String(Math.max(0, Math.trunc(Number(value) || 0))).padStart(2, '0');
}

function formatTime(hour, minute) {
    return `${formatTwoDigits(hour)}:${formatTwoDigits(minute)}`;
}

function formatRange(startHour, startMinute, endHour, endMinute) {
    return `${formatTime(startHour, startMinute)} - ${formatTime(endHour, endMinute)}`;
}

function formatTemperature(value, digits = 1, fallback = '--.-°C') {
    const numeric = toFiniteNumber(value);
    if (numeric === null) {
        return fallback;
    }
    return `${numeric.toFixed(digits)}°C`;
}

function formatEpoch(epoch, options = {}) {
    const numeric = Math.trunc(Number(epoch) || 0);
    if (numeric <= 0) {
        return options.fallback || '--';
    }

    const date = new Date(numeric * 1000);
    if (Number.isNaN(date.getTime())) {
        return options.fallback || '--';
    }

    const timeOptions = {
        hour: '2-digit',
        minute: '2-digit'
    };
    if (options.includeSeconds) {
        timeOptions.second = '2-digit';
    }
    const timeText = date.toLocaleTimeString('pl-PL', timeOptions);
    if (!options.includeDate) {
        return timeText;
    }

    const dateText = date.toLocaleDateString('pl-PL', {
        day: '2-digit',
        month: 'short',
        year: 'numeric'
    });
    return `${dateText}, ${timeText}`;
}

function formatFeedFrequency(freq) {
    switch (Number(freq)) {
        case 1:
            return 'Codziennie';
        case 2:
            return 'Co 2 dni';
        case 3:
            return 'Co 3 dni';
        default:
            return 'Wylaczone';
    }
}

function syncClockFromController(clock) {
    const year = Number(clock?.year);
    const month = Number(clock?.month);
    const day = Number(clock?.day);
    const hour = Number(clock?.hour);
    const minute = Number(clock?.minute);
    const second = Number(clock?.second);

    if (
        !Number.isInteger(year) || !Number.isInteger(month) || !Number.isInteger(day) ||
        !Number.isInteger(hour) || !Number.isInteger(minute) || !Number.isInteger(second) ||
        year < 2024 || month < 1 || month > 12 || day < 1 || day > 31
    ) {
        return false;
    }

    const baseDate = new Date(year, month - 1, day, hour, minute, second, 0);
    if (Number.isNaN(baseDate.getTime())) {
        return false;
    }

    deviceClockBaseDate = baseDate;
    deviceClockSyncedAtMs = Date.now();
    return true;
}

function getCurrentClockDate() {
    if (!deviceClockBaseDate || deviceClockSyncedAtMs <= 0) {
        return new Date();
    }
    return new Date(deviceClockBaseDate.getTime() + Math.max(0, Date.now() - deviceClockSyncedAtMs));
}

function formatControllerClock(clock) {
    const year = Number(clock?.year);
    const month = Number(clock?.month);
    const day = Number(clock?.day);
    const hour = Number(clock?.hour);
    const minute = Number(clock?.minute);
    const second = Number(clock?.second);

    if (
        !Number.isInteger(year) || !Number.isInteger(month) || !Number.isInteger(day) ||
        !Number.isInteger(hour) || !Number.isInteger(minute) || !Number.isInteger(second) ||
        year < 2024 || month < 1 || month > 12 || day < 1 || day > 31
    ) {
        return '';
    }

    const date = new Date(year, month - 1, day, hour, minute, second, 0);
    if (Number.isNaN(date.getTime())) {
        return '';
    }

    return `${date.toLocaleDateString('pl-PL', { day: '2-digit', month: 'short', year: 'numeric' })}, ${date.toLocaleTimeString('pl-PL', { hour: '2-digit', minute: '2-digit', second: '2-digit' })}`;
}

function setInlineStatus(id, message, tone = 'muted') {
    const el = document.getElementById(id);
    if (!el) return;

    el.textContent = message;
    if (tone === 'success') {
        el.style.color = 'var(--accent-cyan)';
    } else if (tone === 'error') {
        el.style.color = '#ef4444';
    } else if (tone === 'warning') {
        el.style.color = 'var(--warning-color)';
    } else {
        el.style.color = 'var(--text-muted)';
    }
}

function setText(id, value) {
    const el = document.getElementById(id);
    if (el) {
        el.textContent = value;
    }
}

function setValue(id, value) {
    const el = document.getElementById(id);
    if (el) {
        el.value = value;
    }
}

function setDisabled(id, disabled) {
    const el = document.getElementById(id);
    if (el) {
        el.disabled = disabled;
    }
}

function setCheckboxIfClean(id, checked) {
    const el = document.getElementById(id);
    if (!el) return;
    if (document.activeElement === el || el.dataset.dirty === '1') return;
    el.checked = Boolean(checked);
}

function setNumericValueIfClean(id, value, digits = null) {
    const el = document.getElementById(id);
    if (!el) return;
    if (document.activeElement === el || el.dataset.dirty === '1') return;
    const numeric = toFiniteNumber(value);
    if (numeric === null) return;
    el.value = digits === null ? String(numeric) : Number(numeric).toFixed(digits);
}

function setInputValueIfClean(id, value) {
    const el = document.getElementById(id);
    if (!el) return;
    if (document.activeElement === el || el.dataset.dirty === '1') return;
    el.value = value || '';
}

function formatDuration(totalSeconds) {
    const seconds = Math.max(0, Math.trunc(Number(totalSeconds) || 0));
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;
    if (days > 0) {
        return `${days}d ${formatTwoDigits(hours)}:${formatTwoDigits(minutes)}:${formatTwoDigits(secs)}`;
    }
    return `${formatTwoDigits(hours)}:${formatTwoDigits(minutes)}:${formatTwoDigits(secs)}`;
}

function formatCountdownMs(value) {
    const ms = Math.max(0, Math.trunc(Number(value) || 0));
    const totalSeconds = Math.ceil(ms / 1000);
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    return `${formatTwoDigits(minutes)}:${formatTwoDigits(seconds)}`;
}

function describeRequestError(error, fallback = 'nieznany blad') {
    if (error?.message) {
        return error.message;
    }
    return fallback;
}

function updateClock() {
    const now = getCurrentClockDate();

    const timeEl = document.getElementById('current-time');
    const dateEl = document.getElementById('current-date');

    if (timeEl && dateEl) {
        timeEl.textContent = now.toLocaleTimeString('pl-PL', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
        dateEl.textContent = now.toLocaleDateString('pl-PL', { day: '2-digit', month: 'short', year: 'numeric' });
    }
}

function setBackendState(isConnected) {
    backendConnected = isConnected;
    const statusEl = document.getElementById('logs-status');
    if (statusEl) {
        statusEl.textContent = isConnected ? 'Polaczono z backendem ESP32.' : 'Brak odpowiedzi sterownika.';
    }
}

function mergeTemperaturePayload(previousTemperature, nextTemperature) {
    if (!nextTemperature || typeof nextTemperature !== 'object') {
        return previousTemperature || null;
    }

    const merged = {
        ...(previousTemperature && typeof previousTemperature === 'object' ? previousTemperature : {}),
        ...nextTemperature
    };

    if (!Array.isArray(nextTemperature.history) && Array.isArray(previousTemperature?.history)) {
        merged.history = previousTemperature.history;
    }

    return merged;
}

function applyStatusPayload(data) {
    if (!data || typeof data !== 'object') {
        return;
    }

    const mergedData = {
        ...data,
        temperature: mergeTemperaturePayload(lastStatusData?.temperature, data.temperature)
    };

    setBackendState(true);
    syncClockFromController(mergedData.clock);
    renderDashboard(mergedData);

    if (window.ChartsApp && typeof window.ChartsApp.updateData === 'function' && mergedData.temperature) {
        window.ChartsApp.updateData(mergedData.temperature);
    }
}

async function fetchStatus(force = false) {
    if (eventStreamConnected && !force) {
        return;
    }

    try {
        const response = await fetch(force ? `${API_STATUS}?history=1` : API_STATUS, {
            cache: 'no-store'
        });
        if (!response.ok) throw new Error('status http');
        const data = await response.json();
        applyStatusPayload(data);
    } catch (_) {
        if (!eventStreamConnected) {
            setBackendState(false);
        }
    }
}

async function fetchLogs(force = false) {
    if (eventStreamConnected && !force) {
        return;
    }

    try {
        const response = await fetch(API_LOGS, { cache: 'no-store' });
        if (!response.ok) throw new Error('logs http');
        const logs = await response.json();
        applyLogsPayload(logs);
    } catch (_) {
        // keep last logs
    }
}

function stopPollingFallback() {
    if (statusPollingTimer) {
        clearInterval(statusPollingTimer);
        statusPollingTimer = null;
    }
    if (logsPollingTimer) {
        clearInterval(logsPollingTimer);
        logsPollingTimer = null;
    }
}

function startPollingFallback() {
    if (!statusPollingTimer) {
        statusPollingTimer = setInterval(() => fetchStatus(false), 3000);
    }
    if (!logsPollingTimer) {
        logsPollingTimer = setInterval(() => fetchLogs(false), 5000);
    }
}

function startEventStream() {
    if (eventStream || !('EventSource' in window)) {
        startPollingFallback();
        return;
    }

    eventStream = new EventSource(API_EVENTS);

    eventStream.onopen = () => {
        eventStreamConnected = true;
        setBackendState(true);
        stopPollingFallback();
    };

    eventStream.addEventListener('ready', () => {
        eventStreamConnected = true;
        setBackendState(true);
        stopPollingFallback();
    });

    eventStream.addEventListener('status', (event) => {
        try {
            const payload = JSON.parse(event.data);
            eventStreamConnected = true;
            applyStatusPayload(payload);
            stopPollingFallback();
        } catch (error) {
            console.warn('Nie udalo sie sparsowac zdarzenia status SSE.', error);
        }
    });

    eventStream.addEventListener('logs', (event) => {
        try {
            const payload = JSON.parse(event.data);
            eventStreamConnected = true;
            applyLogsPayload(payload);
            stopPollingFallback();
        } catch (error) {
            console.warn('Nie udalo sie sparsowac zdarzenia logs SSE.', error);
        }
    });

    eventStream.onerror = () => {
        eventStreamConnected = false;
        startPollingFallback();
    };
}

function shouldShowOledSaveAnimation(action) {
    return OLED_SAVE_ACTIONS.has(action);
}

function showOledSaveAnimation(action) {
    const overlay = document.getElementById('oled-save-overlay');
    const title = document.getElementById('oled-save-title');
    const subtext = document.getElementById('oled-save-subtext');
    if (!overlay || !title || !subtext) {
        return false;
    }

    if (oledSaveOverlayHideTimer) {
        clearTimeout(oledSaveOverlayHideTimer);
        oledSaveOverlayHideTimer = null;
    }

    oledSaveOverlayActiveCount += 1;
    title.textContent = OLED_SAVE_TITLES[action] || 'Zapisywanie danych';
    subtext.textContent = 'Sterownik synchronizuje konfiguracje i pokazuje zapis na OLED.';

    if (oledSaveOverlayActiveCount === 1) {
        overlay.style.display = 'flex';
        overlay.setAttribute('aria-hidden', 'false');
        oledSaveOverlayShownAtMs = Date.now();
    }

    return true;
}

function hideOledSaveAnimation() {
    const overlay = document.getElementById('oled-save-overlay');
    if (oledSaveOverlayActiveCount > 0) {
        oledSaveOverlayActiveCount -= 1;
    }

    if (oledSaveOverlayActiveCount > 0 || !overlay) {
        return;
    }

    const elapsedMs = Date.now() - oledSaveOverlayShownAtMs;
    const delayMs = Math.max(0, 900 - elapsedMs);
    oledSaveOverlayHideTimer = setTimeout(() => {
        overlay.style.display = 'none';
        overlay.setAttribute('aria-hidden', 'true');
        oledSaveOverlayHideTimer = null;
    }, delayMs);
}

async function sendAction(action, payload = {}, options = {}) {
    const showSaveAnimation = options.showSaveAnimation ?? shouldShowOledSaveAnimation(action);
    if (showSaveAnimation) {
        showOledSaveAnimation(action);
    }

    try {
        const params = new URLSearchParams({ action, ...payload });
        const response = await fetch(API_ACTION, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: params.toString()
        });

        const contentType = response.headers.get('content-type') || '';
        let responsePayload;

        if (contentType.includes('application/json')) {
            responsePayload = await response.json();
        } else {
            const responseText = await response.text();
            responsePayload = {
                success: response.ok,
                code: responseText || (response.ok ? 'ok' : 'request_failed'),
                message: responseText || ''
            };
        }

        if (!response.ok || responsePayload?.success === false) {
            const error = new Error(responsePayload?.message || responsePayload?.code || 'request_failed');
            error.code = responsePayload?.code || 'request_failed';
            error.payload = responsePayload;
            throw error;
        }

        return responsePayload;
    } finally {
        if (showSaveAnimation) {
            hideOledSaveAnimation();
        }
    }
}

function setMobileNavOpen(isOpen) {
    const sidebar = document.getElementById('app-sidebar');
    const backdrop = document.getElementById('sidebar-backdrop');
    const toggle = document.getElementById('mobile-nav-toggle');
    const toggleLabel = document.getElementById('mobile-nav-toggle-label');
    const toggleIcon = document.getElementById('mobile-nav-toggle-icon');
    const open = Boolean(isOpen);

    if (!sidebar || !backdrop || !toggle) {
        return;
    }

    sidebar.classList.toggle('mobile-open', open);
    backdrop.classList.toggle('visible', open);
    backdrop.setAttribute('aria-hidden', open ? 'false' : 'true');
    toggle.setAttribute('aria-expanded', open ? 'true' : 'false');
    document.body.classList.toggle('nav-open', open);

    if (toggleLabel) {
        toggleLabel.textContent = open ? 'Zamknij' : 'Menu';
    }

    if (toggleIcon) {
        toggleIcon.className = open ? 'fa-solid fa-xmark' : 'fa-solid fa-bars';
        renderLocalIcon(toggleIcon);
    }
}

function initMobileNavigation() {
    const toggle = document.getElementById('mobile-nav-toggle');
    const sidebar = document.getElementById('app-sidebar');
    const backdrop = document.getElementById('sidebar-backdrop');

    if (!toggle || !sidebar) {
        return;
    }

    toggle.addEventListener('click', () => {
        const isOpen = sidebar.classList.contains('mobile-open');
        setMobileNavOpen(!isOpen);
    });

    backdrop?.addEventListener('click', () => {
        setMobileNavOpen(false);
    });

    window.addEventListener('resize', () => {
        if (window.innerWidth > 960) {
            setMobileNavOpen(false);
        }
    });

    document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') {
            setMobileNavOpen(false);
        }
    });

    setMobileNavOpen(false);
}

function initNavigation() {
    const navItems = document.querySelectorAll('.nav-item[data-target]');

    navItems.forEach((item) => {
        item.addEventListener('click', (event) => {
            event.preventDefault();
            const targetId = item.getAttribute('data-target');
            if (targetId) {
                switchTab(targetId);
            }
        });
    });
}

function switchTab(tabId) {
    const navItems = document.querySelectorAll('.nav-item');
    navItems.forEach((nav) => nav.classList.remove('active'));

    const activeNav = document.querySelector(`.nav-item[data-target="${tabId}"]`);
    if (activeNav) {
        activeNav.classList.add('active');
    }

    const sections = document.querySelectorAll('.view-section');
    sections.forEach((section) => section.classList.remove('active'));

    const targetSection = document.getElementById(tabId);
    if (targetSection) {
        targetSection.classList.add('active');
    }

    setMobileNavOpen(false);
}
