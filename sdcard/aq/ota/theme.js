const PORTAL_THEMES = new Set(['light', 'dark']);

function normalizePortalTheme(theme) {
    const normalized = String(theme || '').trim().toLowerCase();
    return PORTAL_THEMES.has(normalized) ? normalized : '';
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

function updateThemeToggle(theme, data = null) {
    const toggle = document.getElementById('theme-toggle');
    const label = document.getElementById('theme-toggle-label');
    const icon = document.getElementById('theme-toggle-icon');
    const dark = theme === 'dark';
    const source = data?.ldr_auto ? 'LDR' : (data?.manual_light_theme ? 'ustawienie ręczne CYD' : 'sterownik');

    if (toggle) {
        toggle.setAttribute('aria-pressed', dark ? 'true' : 'false');
        toggle.title = `Motyw pobrany z urządzenia: ${dark ? 'ciemny' : 'jasny'} (${source}).`;
    }
    if (label) {
        label.textContent = dark ? 'Ciemny' : 'Jasny';
    }
    if (icon) {
        icon.className = dark ? 'fa-solid fa-moon' : 'fa-solid fa-sun';
        if (typeof renderLocalIcon === 'function') {
            renderLocalIcon(icon);
        }
    }
}

function applyPortalTheme(theme, data = null) {
    const resolvedTheme = normalizePortalTheme(theme) || 'light';
    document.documentElement.setAttribute('data-theme', resolvedTheme);
    document.documentElement.style.colorScheme = resolvedTheme;
    updateThemeToggle(resolvedTheme, data);
    return resolvedTheme;
}

function applyPortalThemeFromStatus(data) {
    const deviceTheme = resolveDevicePortalTheme(data);
    if (!deviceTheme) {
        updateThemeToggle(document.documentElement.getAttribute('data-theme') || 'light', data);
        return '';
    }
    return applyPortalTheme(deviceTheme, data);
}

function initPortalTheme() {
    applyPortalTheme(document.documentElement.getAttribute('data-theme') || 'light');

    const toggle = document.getElementById('theme-toggle');
    toggle?.addEventListener('click', () => {
        if (typeof fetchStatus === 'function') {
            fetchStatus(true);
        }
    });
}

window.applyPortalTheme = applyPortalTheme;
window.applyPortalThemeFromStatus = applyPortalThemeFromStatus;
document.addEventListener('DOMContentLoaded', initPortalTheme);
