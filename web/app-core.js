const API_STATUS = '/api/status';
const API_ACTION = '/api/action';
const API_LOGS = '/api/logs';
const API_EVENTS = '/api/events';
const API_WEB_SESSION = '/api/web-session';
const API_OTA = '/update';
const API_SETTIME = '/settime';
const LOGS_PAGE_SIZE = 10;
const WEB_SESSION_HEARTBEAT_MS = 5000;
const ENABLE_EVENT_STREAM = false;

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
const OLED_SAVE_ACTIONS = new Set([
    'save_schedule',
    'save_network',
    'set_light',
    'set_light1',
    'set_light2',
    'set_filter',
    'set_plant',
    'set_heater',
    'set_aeration',
    'save_display',
    'save_co2',
    'save_water',
    'save_leak'
]);
const PIN_GUARDED_ACTIONS = new Set([
    'feed_now',
    'clear_critical_logs',
    'restart_device',
    'factory_reset',
    'save_schedule',
    'save_network',
    'wifi_session_start',
    'wifi_session_stop',
    'save_temperature',
    'sync_time_ntp',
    'set_light',
    'set_light1',
    'set_light2',
    'set_filter',
    'set_plant',
    'set_heater',
    'set_aeration',
    'save_display',
    'save_co2',
    'save_water',
    'save_leak'
]);
const OLED_SAVE_TITLES = {
    save_schedule: 'Zapisywanie harmonogramów',
    save_network: 'Zapisywanie ustawień WiFi',
    set_light: 'Zapisywanie stanu światła',
    set_light1: 'Zapisywanie światła 1',
    set_light2: 'Zapisywanie światła 2',
    set_filter: 'Zapisywanie stanu filtra',
    set_plant: 'Zapisywanie światła 2',
    set_heater: 'Zapisywanie grzałki',
    set_aeration: 'Zapisywanie napowietrzania',
    save_display: 'Zapisywanie ustawień ekranu',
    save_co2: 'Zapisywanie automatyki CO2',
    save_water: 'Zapisywanie automatycznej dolewki',
    save_leak: 'Zapisywanie zabezpieczeń'
};
let oledSaveOverlayActiveCount = 0;
let oledSaveOverlayShownAtMs = 0;
let oledSaveOverlayHideTimer = null;
let eventStream = null;
let eventStreamConnected = false;
let statusPollingTimer = null;
let logsPollingTimer = null;
let webSessionId = '';
let webSessionTimer = null;
let webSessionClosing = false;
let webSessionListenersBound = false;
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
    `),
    'fa-sun': makeLocalIcon(`
        <circle cx="12" cy="12" r="4" fill="none"/>
        <path fill="none" d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M4.93 19.07l1.41-1.41M17.66 6.34l1.41-1.41"/>
    `),
    'fa-moon': makeLocalIcon(`
        <path fill="none" d="M20 15.5A8.5 8.5 0 0 1 8.5 4 7 7 0 1 0 20 15.5z"/>
    `),
    'fa-puzzle-piece': makeLocalIcon(`
        <path fill="none" d="M3 6h4a2 2 0 1 1 4 0h4a1 1 0 0 1 1 1v4a2 2 0 1 0 0 4v4a1 1 0 0 1-1 1h-4a2 2 0 1 1-4 0H3V6z"/>
    `),
    'fa-sliders': makeLocalIcon(`
        <path fill="none" d="M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3"/>
        <circle cx="4" cy="12" r="1.5" fill="none"/>
        <circle cx="12" cy="10" r="1.5" fill="none"/>
        <circle cx="20" cy="14" r="1.5" fill="none"/>
    `),
    'fa-seedling': makeLocalIcon(`
        <path fill="none" d="M12 21v-8"/>
        <path fill="none" d="M7 13c0-4 5-4 5-8"/>
        <path fill="none" d="M17 13c0-4-5-4-5-8"/>
        <path fill="none" d="M4 16c2-3 5-3 8-3"/>
        <path fill="none" d="M20 16c-2-3-5-3-8-3"/>
    `),
    'fa-cloud': makeLocalIcon(`
        <path fill="none" d="M7 18a4 4 0 0 1 .9-7.9A5 5 0 0 1 17.5 12H18a3 3 0 0 1 0 6H7z"/>
    `),
    'fa-droplet': makeLocalIcon(`
        <path fill="none" d="M12 3c-3 4-6 7-6 10a6 6 0 0 0 12 0c0-3-3-6-6-10z"/>
    `),
    'fa-shield-halved': makeLocalIcon(`
        <path fill="none" d="M12 3l7 3v5c0 4.5-3 8.5-7 10-4-1.5-7-5.5-7-10V6l7-3z"/>
        <path fill="none" d="M12 3v18"/>
    `),
    'fa-bolt': makeLocalIcon(`
        <path fill="none" d="M13 2L5 14h6l-1 8 8-12h-6l1-8z"/>
    `),
    'fa-vial': makeLocalIcon(`
        <path fill="none" d="M10 2h4M12 2v3M7.5 8.5l9 9a3.5 3.5 0 0 1-5 5l-9-9V8.5h5z"/>
    `),
    'fa-database': makeLocalIcon(`
        <ellipse cx="12" cy="6" rx="8" ry="3" fill="none"/>
        <path fill="none" d="M4 6v12c0 1.7 3.6 3 8 3s8-1.3 8-3V6"/>
        <path fill="none" d="M4 12c0 1.7 3.6 3 8 3s8-1.3 8-3"/>
    `),
    'fa-circle-check': makeLocalIcon(`
        <circle cx="12" cy="12" r="9" fill="none"/>
        <path fill="none" d="m8.5 12 2.3 2.3 4.7-4.7"/>
    `),
    'fa-lock': makeLocalIcon(`
        <rect x="5" y="11" width="14" height="10" rx="2" fill="none"/>
        <path fill="none" d="M8 11V7a4 4 0 1 1 8 0v4"/>
    `),
    'fa-plug': makeLocalIcon(`
        <path fill="none" d="M12 17v5M8 3v4M16 3v4M6 7h12v4a6 6 0 0 1-12 0V7z"/>
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
    if (value === null || value === undefined) {
        return null;
    }
    if (typeof value === 'string' && value.trim() === '') {
        return null;
    }
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

function setCommandStatus(valueId, value, detail = '', tone = 'neutral') {
    const valueEl = document.getElementById(valueId);
    if (!valueEl) {
        return;
    }

    const safeTone = ['ok', 'warn', 'danger', 'info', 'neutral'].includes(tone) ? tone : 'neutral';
    valueEl.textContent = value;

    const detailEl = document.getElementById(`${valueId}-detail`);
    if (detailEl) {
        detailEl.textContent = detail;
    }

    const card = valueEl.closest('.view-command-card');
    if (card) {
        card.dataset.tone = safeTone;
    }
}

function mapInlineToneToCommandTone(tone) {
    if (tone === 'success') return 'ok';
    if (tone === 'error') return 'danger';
    if (tone === 'warning') return 'warn';
    if (tone === 'info') return 'info';
    return 'neutral';
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

function setCommandStatus(valueId, value, detail = '', tone = 'neutral') {
    const valueEl = document.getElementById(valueId);
    if (!valueEl) {
        return;
    }

    const safeTone = ['ok', 'warn', 'danger', 'info', 'neutral'].includes(tone) ? tone : 'neutral';
    valueEl.textContent = value;

    const detailEl = document.getElementById(`${valueId}-detail`);
    if (detailEl) {
        detailEl.textContent = detail;
    }

    const card = valueEl.closest('.view-command-card');
    if (card) {
        card.dataset.tone = safeTone;
    }
}

function mapInlineToneToCommandTone(tone) {
    if (tone === 'success') return 'ok';
    if (tone === 'error') return 'danger';
    if (tone === 'warning') return 'warn';
    if (tone === 'info') return 'info';
    return 'neutral';
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

function updateBackendConnectionIndicator(isConnected, stateChanged) {
    const indicator = document.getElementById('backend-connection-status');
    const label = document.getElementById('backend-connection-label');
    const announcer = document.getElementById('backend-connection-announcer');
    const connected = Boolean(isConnected);
    const state = connected ? 'online' : 'offline';
    const text = connected ? 'Sterownik online' : 'Sterownik offline';

    if (indicator) {
        indicator.dataset.state = state;
        indicator.classList.remove('status-badge-success', 'status-badge-muted', 'status-badge-danger');
        indicator.classList.add(connected ? 'status-badge-success' : 'status-badge-danger');
        indicator.setAttribute('aria-label', connected
            ? 'Stan połączenia: sterownik odpowiada'
            : 'Stan połączenia: brak odpowiedzi sterownika, trwa ponawianie');
        indicator.title = connected
            ? `Ostatnia odpowiedź: ${new Date().toLocaleTimeString('pl-PL')}`
            : 'Panel zachowuje ostatnie dane i ponawia połączenie automatycznie.';
    }
    if (label) {
        label.textContent = text;
    }
    if (stateChanged && announcer) {
        announcer.textContent = connected
            ? 'Połączenie ze sterownikiem zostało przywrócone.'
            : 'Utracono połączenie ze sterownikiem. Panel ponawia próbę automatycznie.';
    }
}

function setBackendState(isConnected) {
    const connected = Boolean(isConnected);
    const currentIndicatorState = document.getElementById('backend-connection-status')?.dataset.state || '';
    const stateChanged = backendConnected !== connected ||
        (connected && currentIndicatorState !== 'online') ||
        (!connected && currentIndicatorState !== 'offline');

    backendConnected = connected;
    window.backendConnected = connected;
    document.body.dataset.backendState = connected ? 'online' : 'offline';
    updateBackendConnectionIndicator(connected, stateChanged);

    const statusEl = document.getElementById('logs-status');
    if (statusEl) {
        statusEl.textContent = connected ? 'Połączono ze sterownikiem ESP32.' : 'Brak odpowiedzi sterownika.';
    }
    if (!connected) {
        setCommandStatus('dashboard-strip-state', 'Offline', 'Brak odpowiedzi sterownika', 'danger');
        setCommandStatus('ota-strip-link', 'Offline', 'Brak aktywnej odpowiedzi HTTP', 'warn');
        setCommandStatus('diag-strip-bus', 'Offline', 'Status zostanie odswiezony po powrocie ESP32', 'warn');
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

    lastStatusData = mergedData;
    window.lastStatusData = mergedData;

    if (typeof window.applyPortalThemeFromStatus === 'function') {
        window.applyPortalThemeFromStatus(mergedData);
    }
    syncClockFromController(mergedData.clock);
    renderDashboard(mergedData);
    setBackendState(true);

    if (window.ChartsApp && typeof window.ChartsApp.updateData === 'function' && mergedData.temperature) {
        window.ChartsApp.updateData(mergedData.temperature);
    }
}

function createWebSessionId() {
    const bytes = new Uint8Array(8);
    if (window.crypto && typeof window.crypto.getRandomValues === 'function') {
        window.crypto.getRandomValues(bytes);
        return `w${Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('')}`;
    }
    const fallback = `${Date.now().toString(36)}${Math.floor(Math.random() * 0xFFFFFF).toString(36)}`;
    return `w${fallback}`.slice(0, 17);
}

function getWebSessionId() {
    if (/^[A-Za-z0-9_-]{6,24}$/.test(webSessionId)) {
        return webSessionId;
    }

    try {
        const stored = window.sessionStorage?.getItem('aqWebSessionId') || '';
        if (/^[A-Za-z0-9_-]{6,24}$/.test(stored)) {
            webSessionId = stored;
            return webSessionId;
        }
    } catch (_) {}

    webSessionId = createWebSessionId();
    try {
        window.sessionStorage?.setItem('aqWebSessionId', webSessionId);
    } catch (_) {}
    return webSessionId;
}

function sendWebSessionHeartbeat(state = 'active', useBeacon = false) {
    const params = new URLSearchParams({
        sid: getWebSessionId(),
        state
    });

    if (useBeacon && navigator.sendBeacon) {
        try {
            navigator.sendBeacon(API_WEB_SESSION, params);
            return;
        } catch (_) {}
    }

    fetch(`${API_WEB_SESSION}?${params.toString()}`, {
        method: 'GET',
        cache: 'no-store',
        keepalive: state !== 'active'
    }).catch(() => {});
}

function closeWebSession() {
    if (webSessionClosing) {
        return;
    }
    webSessionClosing = true;
    if (webSessionTimer) {
        clearInterval(webSessionTimer);
        webSessionTimer = null;
    }
    sendWebSessionHeartbeat('close', true);
}

function startWebSessionHeartbeat() {
    webSessionClosing = false;
    sendWebSessionHeartbeat('active');
    if (!webSessionTimer) {
        webSessionTimer = setInterval(() => {
            if (!webSessionClosing) {
                sendWebSessionHeartbeat('active');
            }
        }, WEB_SESSION_HEARTBEAT_MS);
    }

    if (!webSessionListenersBound) {
        webSessionListenersBound = true;
        window.addEventListener('pagehide', closeWebSession);
        window.addEventListener('beforeunload', closeWebSession);
        window.addEventListener('pageshow', () => {
            if (webSessionClosing) {
                webSessionClosing = false;
                startWebSessionHeartbeat();
            }
        });
        document.addEventListener('visibilitychange', () => {
            if (document.visibilityState === 'visible' && !webSessionClosing) {
                sendWebSessionHeartbeat('active');
            }
        });
    }
}

async function fetchStatus(force = false, includeHistory = false) {
    if (eventStreamConnected && !force) {
        return;
    }

    try {
        const response = await fetch(includeHistory ? `${API_STATUS}?history=1` : API_STATUS, {
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
    if (!isAdminAuthenticated()) {
        return;
    }

    try {
        const pin = getAdminPinForRequest();
        const response = await fetch(`${API_LOGS}?pin=${encodeURIComponent(pin)}`, { cache: 'no-store' });
        if (!response.ok) {
            if (response.status === 403) {
                logoutAdmin();
            }
            throw new Error('logs http');
        }
        const logs = await response.json();
        applyLogsPayload(logs);
    } catch (_) {
        // keep last admin logs
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
    if (!ENABLE_EVENT_STREAM || eventStream || !('EventSource' in window)) {
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
    subtext.textContent = 'Wysyłam zmianę do ESP32 i czekam na potwierdzenie sterownika.';

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
    const delayMs = Math.max(0, 300 - elapsedMs);
    oledSaveOverlayHideTimer = setTimeout(() => {
        overlay.style.display = 'none';
        overlay.setAttribute('aria-hidden', 'true');
        oledSaveOverlayHideTimer = null;
    }, delayMs);
}

let pinPromiseResolve = null;
let pinPromiseReject = null;
let pinModalReturnFocus = null;
let adminSessionPin = '';
let adminSessionStartedAtMs = 0;
let currentUserRole = 'guest';
const ADMIN_PIN_PATTERN = /^\d{4,8}$/;

function normalizePinValue(value) {
    return String(value || '').replace(/\D/g, '').slice(0, 8);
}

function setPinModalError(message) {
    const errorEl = document.getElementById('pin-modal-error');
    const input = document.getElementById('pin-modal-input');
    if (!errorEl) return;
    const hasError = Boolean(message);
    errorEl.textContent = hasError ? message : '';
    errorEl.hidden = !hasError;
    input?.setAttribute('aria-invalid', hasError ? 'true' : 'false');
}

function setPinModalBusy(isBusy) {
    const input = document.getElementById('pin-modal-input');
    const submit = document.getElementById('pin-modal-submit');
    const cancel = document.getElementById('pin-modal-cancel');
    if (input) input.disabled = isBusy;
    if (submit) {
        submit.disabled = isBusy;
        submit.textContent = isBusy ? 'Sprawdzam...' : 'OK';
    }
    if (cancel) cancel.disabled = isBusy;
}

function setAdminSession(pin) {
    adminSessionPin = pin;
    adminSessionStartedAtMs = Date.now();
    currentUserRole = 'admin';
    applyAuthState();
    fetchStatus(true);
    fetchLogs(true);
}

function clearAdminSession() {
    adminSessionPin = '';
    adminSessionStartedAtMs = 0;
    currentUserRole = 'guest';
    cachedLogs = {
        normal: [],
        critical: [],
        counts: { normal: 0, critical: 0 }
    };
    logsPage = { normal: 0, critical: 0 };
    applyAuthState();
    if (typeof renderLogs === 'function') {
        renderLogs();
    }
}

function isAdminAuthenticated() {
    return currentUserRole === 'admin' && ADMIN_PIN_PATTERN.test(adminSessionPin);
}

function getAdminPinForRequest() {
    return isAdminAuthenticated() ? adminSessionPin : '';
}

function restorePinModalFocus() {
    const target = pinModalReturnFocus;
    pinModalReturnFocus = null;
    if (!(target instanceof HTMLElement) || !target.isConnected || target.disabled || target.offsetParent === null) {
        return;
    }
    requestAnimationFrame(() => target.focus());
}

function hidePinModal() {
    const modal = document.getElementById('pin-modal');
    if (modal) {
        modal.style.display = 'none';
        modal.setAttribute('aria-hidden', 'true');
    }
    restorePinModalFocus();
}

function promptForPin(errorMessage = '') {
    const modal = document.getElementById('pin-modal');
    const input = document.getElementById('pin-modal-input');

    if (!modal || !input) {
        return Promise.reject(new Error('PIN modal HTML elements not found'));
    }

    const wasOpen = modal.style.display === 'flex';
    if (!wasOpen && document.activeElement instanceof HTMLElement) {
        pinModalReturnFocus = document.activeElement;
    }

    if (pinPromiseReject) {
        pinPromiseReject(new Error('pin_replaced'));
        pinPromiseReject = null;
        pinPromiseResolve = null;
    }

    input.value = '';
    setPinModalBusy(false);
    setPinModalError(errorMessage);
    modal.style.display = 'flex';
    modal.setAttribute('aria-hidden', 'false');
    input.focus();
    input.select();

    return new Promise((resolve, reject) => {
        pinPromiseResolve = resolve;
        pinPromiseReject = reject;
    });
}

function handlePinCancel() {
    hidePinModal();
    if (pinPromiseReject) {
        pinPromiseReject(new Error('pin_cancelled'));
        pinPromiseReject = null;
        pinPromiseResolve = null;
    }
}

function handlePinSubmit() {
    const input = document.getElementById('pin-modal-input');
    if (!input) return;
    const val = normalizePinValue(input.value);
    input.value = val;
    if (!ADMIN_PIN_PATTERN.test(val)) {
        setPinModalError('PIN admina musi mieć od 4 do 8 cyfr.');
        return;
    }
    if (pinPromiseResolve) {
        pinPromiseResolve(val);
        pinPromiseResolve = null;
        pinPromiseReject = null;
    }
}

window.promptForPin = promptForPin;
window.handlePinCancel = handlePinCancel;
window.handlePinSubmit = handlePinSubmit;
window.getAdminPinForRequest = getAdminPinForRequest;

function updateAuthStatusWidgets() {
    const isAdmin = isAdminAuthenticated();
    setCommandStatus(
        'ota-strip-pin',
        isAdmin ? 'Admin' : 'Gość',
        isAdmin ? 'Dostęp administracyjny aktywny' : 'Zaloguj admina przed OTA',
        isAdmin ? 'ok' : 'warn'
    );
}

function applyAuthState() {
    const isAdmin = isAdminAuthenticated();
    document.body.classList.toggle('role-admin', isAdmin);
    document.body.classList.toggle('role-guest', !isAdmin);
    updateAuthStatusWidgets();

    const activeSection = document.querySelector('.view-section.active');
    if (activeSection?.matches('[data-admin-only="true"]') && !isAdmin) {
        switchTab('dashboard');
    }
}

window.isAdminAuthenticated = isAdminAuthenticated;
window.applyAuthState = applyAuthState;

async function verifyAdminPin(pin) {
    const params = new URLSearchParams({ action: 'auth_check', pin });
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 5000);
    let response;
    try {
        response = await fetch(API_ACTION, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: params.toString(),
            signal: controller.signal
        });
    } catch (error) {
        if (error?.name === 'AbortError') {
            const timeoutError = new Error('Sterownik nie odpowiedział na logowanie w ciągu 5 sekund.');
            timeoutError.code = 'auth_timeout';
            throw timeoutError;
        }
        throw error;
    } finally {
        clearTimeout(timeout);
    }
    const contentType = response.headers.get('content-type') || '';
    const responsePayload = contentType.includes('application/json')
        ? await response.json()
        : {
            success: response.ok,
            code: response.ok ? 'ok' : 'request_failed',
            message: await response.text()
        };

    if (!response.ok || responsePayload?.success === false) {
        const error = new Error(responsePayload?.message || 'Nieprawidłowy PIN admina.');
        error.code = responsePayload?.code || 'invalid_pin';
        error.status = response.status;
        throw error;
    }

    return responsePayload;
}

async function loginAsAdmin() {
    let message = '';
    for (let attempt = 0; attempt < 3; attempt += 1) {
        let pin = '';
        try {
            pin = await promptForPin(message);
        } catch (error) {
            const cancelled = new Error('Logowanie admina anulowane.');
            cancelled.code = 'admin_login_cancelled';
            throw cancelled;
        }

        try {
            setPinModalBusy(true);
            await verifyAdminPin(pin);
            setAdminSession(pin);
            hidePinModal();
            return true;
        } catch (error) {
            clearAdminSession();
            const connectionError = error?.code === 'auth_timeout' || !error?.status;
            message = connectionError
                ? (error?.message || 'Brak odpowiedzi sterownika. Spróbuj ponownie.')
                : (attempt >= 1
                    ? 'Błędny PIN admina. Pozostała ostatnia próba.'
                    : 'Błędny PIN admina. Spróbuj ponownie.');
            if (attempt === 2) {
                throw error;
            }
        } finally {
            setPinModalBusy(false);
        }
    }
    return false;
}

function logoutAdmin() {
    clearAdminSession();
}

window.loginAsAdmin = loginAsAdmin;
window.logoutAdmin = logoutAdmin;

async function sendAction(action, payload = {}, options = {}) {
    const actionPayload = { ...payload };
    const needsAdmin = options.requirePin ?? PIN_GUARDED_ACTIONS.has(action);

    if (needsAdmin) {
        if (!isAdminAuthenticated()) {
            await loginAsAdmin();
        }
        actionPayload.pin = adminSessionPin;
    }

    const showSaveAnimation = options.showSaveAnimation ?? shouldShowOledSaveAnimation(action);
    let saveAnimationShown = false;
    if (showSaveAnimation) {
        showOledSaveAnimation(action);
        saveAnimationShown = true;
    }

    try {
        const params = new URLSearchParams({ action, ...actionPayload });
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
            error.status = response.status;
            error.payload = responsePayload;
            throw error;
        }

        return responsePayload;
    } catch (error) {
        const isPinErr = error.status === 403 ||
                         error.code === 'invalid_pin' ||
                         error.code === 'pin_required' ||
                         String(error.message || '').toLowerCase().includes('pin');

        if (needsAdmin && isPinErr) {
            clearAdminSession();
            const authError = new Error('Dostęp admina wygasł albo PIN został odrzucony. Zaloguj się ponownie.');
            authError.code = 'admin_required';
            throw authError;
        }

        throw error;
    } finally {
        if (saveAnimationShown) {
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

    const wasOpen = sidebar.classList.contains('mobile-open');
    sidebar.classList.toggle('mobile-open', open);
    backdrop.classList.toggle('visible', open);
    backdrop.setAttribute('aria-hidden', open ? 'false' : 'true');
    toggle.setAttribute('aria-expanded', open ? 'true' : 'false');
    toggle.setAttribute('aria-label', open ? 'Zamknij menu' : 'Otwórz menu');
    document.body.classList.toggle('nav-open', open);

    if (toggleLabel) {
        toggleLabel.textContent = open ? 'Zamknij' : 'Menu';
    }

    if (toggleIcon) {
        toggleIcon.className = open ? 'fa-solid fa-xmark' : 'fa-solid fa-bars';
        renderLocalIcon(toggleIcon);
    }

    if (window.innerWidth <= 960 && open && !wasOpen) {
        requestAnimationFrame(() => {
            const activeLink = sidebar.querySelector('.nav-item.active a');
            const firstVisibleControl = Array.from(sidebar.querySelectorAll('a[href], button:not([disabled])'))
                .find((element) => element.offsetParent !== null);
            (activeLink || firstVisibleControl)?.focus();
        });
    } else if (window.innerWidth <= 960 && !open && wasOpen) {
        toggle.focus();
    }
}

function updateMobileCurrentView(tabId) {
    const currentView = document.getElementById('mobile-current-view');
    if (!currentView) return;

    const activeNav = Array.from(document.querySelectorAll('.nav-item[data-target]'))
        .find((nav) => nav.getAttribute('data-target') === tabId);
    const label = activeNav?.querySelector('a')?.textContent?.trim();
    currentView.textContent = label || 'Panel sterownika';
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
            return;
        }

        if (event.key === 'Tab' && window.innerWidth <= 960 && sidebar.classList.contains('mobile-open')) {
            const focusable = Array.from(sidebar.querySelectorAll(
                'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), [tabindex]:not([tabindex="-1"])'
            )).filter((element) => element.offsetParent !== null);
            if (focusable.length === 0) {
                event.preventDefault();
                toggle.focus();
                return;
            }

            const first = focusable[0];
            const last = focusable[focusable.length - 1];
            if (event.shiftKey && document.activeElement === first) {
                event.preventDefault();
                last.focus();
            } else if (!event.shiftKey && document.activeElement === last) {
                event.preventDefault();
                first.focus();
            }
        }
    });

    setMobileNavOpen(false);
    updateMobileCurrentView(document.querySelector('.view-section.active')?.id || 'dashboard');
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

function isAdminOnlySection(section) {
    return section?.matches('[data-admin-only="true"]') || false;
}

function canAccessSection(section) {
    return !!section && section.classList.contains('view-section') && (!isAdminOnlySection(section) || isAdminAuthenticated());
}

function getInitialTabFromHash() {
    let hash = '';
    try {
        hash = decodeURIComponent(String(window.location.hash || '').replace(/^#/, ''));
    } catch (_) {
        hash = '';
    }
    const section = hash ? document.getElementById(hash) : null;
    return canAccessSection(section) ? hash : 'dashboard';
}

function updateLocationHash(tabId) {
    if (!window.history?.replaceState) return;
    const baseUrl = `${window.location.pathname}${window.location.search}`;
    window.history.replaceState(null, '', tabId === 'dashboard' ? baseUrl : `${baseUrl}#${tabId}`);
}

function switchTab(tabId, updateHash = true) {
    const targetSection = document.getElementById(tabId);
    if (!canAccessSection(targetSection)) {
        if (tabId !== 'dashboard') {
            switchTab('dashboard', updateHash);
        }
        return;
    }

    const previousTabId = document.querySelector('.view-section.active')?.id || '';
    const navItems = document.querySelectorAll('.nav-item');
    navItems.forEach((nav) => {
        nav.classList.remove('active');
        nav.querySelector('a')?.removeAttribute('aria-current');
    });

    const activeNav = Array.from(navItems).find((nav) => nav.getAttribute('data-target') === tabId);
    if (activeNav) {
        activeNav.classList.add('active');
        activeNav.querySelector('a')?.setAttribute('aria-current', 'page');
    }

    const sections = document.querySelectorAll('.view-section');
    sections.forEach((section) => {
        const isActive = section.id === tabId;
        section.classList.toggle('active', isActive);
        section.setAttribute('aria-hidden', isActive ? 'false' : 'true');
    });

    if (targetSection) {
        document.body.dataset.activeView = tabId;
        updateMobileCurrentView(tabId);
        if (updateHash) {
            updateLocationHash(tabId);
        }
        if (previousTabId && previousTabId !== tabId) {
            const label = activeNav?.querySelector('a')?.textContent?.trim() || tabId;
            const announcer = document.getElementById('view-announcer');
            if (announcer) {
                announcer.textContent = `Otwarty widok: ${label}.`;
            }
        }
    }

    if (tabId === 'harmonogramy' && typeof refreshScheduleTimelineLayout === 'function') {
        requestAnimationFrame(refreshScheduleTimelineLayout);
    }

    if (tabId === 'wykresy' && typeof renderAllCharts === 'function') {
        requestAnimationFrame(renderAllCharts);
    }

    if (tabId === 'diag' && typeof fetchHardwareBusDiagnostics === 'function') {
        requestAnimationFrame(() => fetchHardwareBusDiagnostics(false));
    }

    setMobileNavOpen(false);
}

