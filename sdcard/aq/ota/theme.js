const PORTAL_THEMES = new Set(['light', 'dark']);
const MOBILE_THEME_MEDIA = typeof window.matchMedia === 'function'
    ? window.matchMedia('(max-width: 760px)')
    : null;
const DARK_THEME_MEDIA = typeof window.matchMedia === 'function'
    ? window.matchMedia('(prefers-color-scheme: dark)')
    : null;

let lastPortalThemeStatus = null;

function normalizePortalTheme(theme) {
    const normalized = String(theme || '').trim().toLowerCase();
    return PORTAL_THEMES.has(normalized) ? normalized : '';
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
    const deviceSource = data?.ldr_auto
        ? 'LDR'
        : (data?.manual_light_theme ? 'ustawienie ręczne CYD' : 'sterownik');

    if (toggle) {
        toggle.setAttribute('aria-pressed', dark ? 'true' : 'false');
        toggle.setAttribute('data-theme-source', source);
        toggle.title = systemControlled
            ? `Motyw automatyczny telefonu: ${dark ? 'ciemny' : 'jasny'}.`
            : `Motyw pobrany z urządzenia: ${dark ? 'ciemny' : 'jasny'} (${deviceSource}).`;
    }
    if (label) {
        label.textContent = systemControlled
            ? `Auto: ${dark ? 'ciemny' : 'jasny'}`
            : (dark ? 'Ciemny' : 'Jasny');
    }
    if (icon) {
        icon.className = dark ? 'fa-solid fa-moon' : 'fa-solid fa-sun';
        if (typeof renderLocalIcon === 'function') {
            renderLocalIcon(icon);
        }
    }
}

function applyPortalTheme(theme, data = null, source = 'device') {
    const resolvedTheme = normalizePortalTheme(theme) || 'light';
    document.documentElement.setAttribute('data-theme', resolvedTheme);
    document.documentElement.setAttribute('data-theme-source', source);
    document.documentElement.style.colorScheme = resolvedTheme;
    updateThemeToggle(resolvedTheme, data, source);
    return resolvedTheme;
}

function applyPortalThemeFromStatus(data) {
    lastPortalThemeStatus = data && typeof data === 'object' ? data : null;

    if (usesMobileSystemTheme()) {
        return applyPortalTheme(resolveSystemPortalTheme(), lastPortalThemeStatus, 'system');
    }

    const deviceTheme = resolveDevicePortalTheme(lastPortalThemeStatus);
    if (!deviceTheme) {
        const currentTheme = normalizePortalTheme(document.documentElement.getAttribute('data-theme')) || 'light';
        updateThemeToggle(currentTheme, lastPortalThemeStatus, 'device');
        return '';
    }
    return applyPortalTheme(deviceTheme, lastPortalThemeStatus, 'device');
}

function synchronizePortalTheme() {
    if (usesMobileSystemTheme()) {
        applyPortalTheme(resolveSystemPortalTheme(), lastPortalThemeStatus, 'system');
        return;
    }

    const deviceTheme = resolveDevicePortalTheme(lastPortalThemeStatus);
    applyPortalTheme(deviceTheme || 'light', lastPortalThemeStatus, 'device');
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
        if (usesMobileSystemTheme()) {
            synchronizePortalTheme();
            return;
        }
        if (typeof fetchStatus === 'function') {
            fetchStatus(true);
        }
    });
}

window.applyPortalTheme = applyPortalTheme;
window.applyPortalThemeFromStatus = applyPortalThemeFromStatus;
document.addEventListener('DOMContentLoaded', initPortalTheme);
