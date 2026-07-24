const PORTAL_THEMES = new Set(['light', 'dark']);
const PORTAL_THEME_PREFERENCE_KEY = 'aqPortalThemePreference';
const MOBILE_THEME_MEDIA = typeof window.matchMedia === 'function'
    ? window.matchMedia('(max-width: 760px)')
    : null;
const DARK_THEME_MEDIA = typeof window.matchMedia === 'function'
    ? window.matchMedia('(prefers-color-scheme: dark)')
    : null;

let lastPortalThemeStatus = null;
let portalThemePreference = readPortalThemePreference();

function normalizePortalTheme(theme) {
    const normalized = String(theme || '').trim().toLowerCase();
    return PORTAL_THEMES.has(normalized) ? normalized : '';
}

function readPortalThemePreference() {
    try {
        const stored = String(window.localStorage?.getItem(PORTAL_THEME_PREFERENCE_KEY) || '');
        return PORTAL_THEMES.has(stored) ? stored : 'auto';
    } catch (_) {
        return 'auto';
    }
}

function savePortalThemePreference(preference) {
    portalThemePreference = PORTAL_THEMES.has(preference) ? preference : 'auto';
    try {
        if (portalThemePreference === 'auto') {
            window.localStorage?.removeItem(PORTAL_THEME_PREFERENCE_KEY);
        } else {
            window.localStorage?.setItem(PORTAL_THEME_PREFERENCE_KEY, portalThemePreference);
        }
    } catch (_) {}
}

function usesMobileSystemTheme() {
    return Boolean(MOBILE_THEME_MEDIA?.matches);
}

function resolveSystemPortalTheme() {
    return DARK_THEME_MEDIA?.matches ? 'dark' : 'light';
}

function resolveDevicePortalTheme(data) {
    if (!data || typeof data !== 'object') {
        return '';
    }

    if (typeof data.theme_light === 'boolean') {
        return data.theme_light ? 'light' : 'dark';
    }

    const rootTheme = normalizePortalTheme(data.theme);
    if (rootTheme) {
        return rootTheme;
    }

    if (typeof data.config?.theme_light === 'boolean') {
        return data.config.theme_light ? 'light' : 'dark';
    }

    return normalizePortalTheme(data.config?.theme);
}

function updateThemeToggle(theme, data = null, source = 'device') {
    const toggle = document.getElementById('theme-toggle');
    const label = document.getElementById('theme-toggle-label');
    const icon = document.getElementById('theme-toggle-icon');
    const dark = theme === 'dark';
    const systemControlled = source === 'system';
    const manuallyControlled = source === 'manual';
    const deviceSource = data?.ldr_auto
        ? 'LDR'
        : (data?.manual_light_theme ? 'ustawienie ręczne CYD' : 'sterownik');

    if (toggle) {
        toggle.setAttribute('aria-pressed', dark ? 'true' : 'false');
        toggle.setAttribute('data-theme-source', source);
        toggle.setAttribute('aria-label', `Motyw ${dark ? 'ciemny' : 'jasny'}. Kliknij, aby zmienić tryb motywu.`);
        toggle.title = manuallyControlled
            ? `Motyw ręczny: ${dark ? 'ciemny' : 'jasny'}. Kliknij, aby przejść do kolejnego trybu.`
            : (systemControlled
                ? `Motyw automatyczny telefonu: ${dark ? 'ciemny' : 'jasny'}. Kliknij, aby ustawić ręcznie.`
                : `Motyw pobrany z urządzenia: ${dark ? 'ciemny' : 'jasny'} (${deviceSource}). Kliknij, aby ustawić ręcznie.`);
    }
    if (label) {
        label.textContent = manuallyControlled
            ? `Własny: ${dark ? 'ciemny' : 'jasny'}`
            : (systemControlled
                ? `Auto: ${dark ? 'ciemny' : 'jasny'}`
                : (dark ? 'Ciemny' : 'Jasny'));
    }
    if (icon) {
        icon.className = dark ? 'fa-solid fa-moon' : 'fa-solid fa-sun';
        if (typeof renderLocalIcon === 'function') {
            renderLocalIcon(icon);
        }
    }
}

function applyPortalTheme(theme, data = null, source = 'device') {
    const resolvedTheme = normalizePortalTheme(theme) || 'dark';
    document.documentElement.setAttribute('data-theme', resolvedTheme);
    document.documentElement.setAttribute('data-theme-source', source);
    document.documentElement.style.colorScheme = resolvedTheme;
    updateThemeToggle(resolvedTheme, data, source);
    return resolvedTheme;
}

function applyPortalThemeFromStatus(data) {
    lastPortalThemeStatus = data && typeof data === 'object' ? data : null;

    if (PORTAL_THEMES.has(portalThemePreference)) {
        return applyPortalTheme(portalThemePreference, lastPortalThemeStatus, 'manual');
    }
    if (usesMobileSystemTheme()) {
        return applyPortalTheme(resolveSystemPortalTheme(), lastPortalThemeStatus, 'system');
    }

    const deviceTheme = resolveDevicePortalTheme(lastPortalThemeStatus);
    if (!deviceTheme) {
        const currentTheme = normalizePortalTheme(document.documentElement.getAttribute('data-theme')) || 'dark';
        updateThemeToggle(currentTheme, lastPortalThemeStatus, 'device');
        return '';
    }
    return applyPortalTheme(deviceTheme, lastPortalThemeStatus, 'device');
}

function synchronizePortalTheme() {
    if (PORTAL_THEMES.has(portalThemePreference)) {
        applyPortalTheme(portalThemePreference, lastPortalThemeStatus, 'manual');
        return;
    }
    if (usesMobileSystemTheme()) {
        applyPortalTheme(resolveSystemPortalTheme(), lastPortalThemeStatus, 'system');
        return;
    }

    const deviceTheme = resolveDevicePortalTheme(lastPortalThemeStatus);
    applyPortalTheme(deviceTheme || 'dark', lastPortalThemeStatus, 'device');
}

function addMediaChangeListener(mediaQuery, listener) {
    if (!mediaQuery) return;
    if (typeof mediaQuery.addEventListener === 'function') {
        mediaQuery.addEventListener('change', listener);
        return;
    }
    if (typeof mediaQuery.addListener === 'function') {
        mediaQuery.addListener(listener);
    }
}

function initPortalTheme() {
    synchronizePortalTheme();
    addMediaChangeListener(MOBILE_THEME_MEDIA, synchronizePortalTheme);
    addMediaChangeListener(DARK_THEME_MEDIA, synchronizePortalTheme);

    const toggle = document.getElementById('theme-toggle');
    toggle?.addEventListener('click', () => {
        const nextPreference = portalThemePreference === 'auto'
            ? 'dark'
            : (portalThemePreference === 'dark' ? 'light' : 'auto');
        savePortalThemePreference(nextPreference);
        synchronizePortalTheme();
    });
}

window.applyPortalTheme = applyPortalTheme;
window.applyPortalThemeFromStatus = applyPortalThemeFromStatus;
document.addEventListener('DOMContentLoaded', initPortalTheme);
