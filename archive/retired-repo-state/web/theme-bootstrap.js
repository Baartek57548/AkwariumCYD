'use strict';

(function bootstrapPortalTheme() {
    const preferenceKey = 'aqPortalThemePreference';
    const supportedThemes = new Set(['light', 'dark']);
    let storedPreference = '';

    try {
        storedPreference = window.localStorage?.getItem(preferenceKey) || '';
    } catch (_) {
        storedPreference = '';
    }

    const hasMediaQueries = typeof window.matchMedia === 'function';
    const mobile = hasMediaQueries && window.matchMedia('(max-width: 760px)').matches;
    const systemTheme = hasMediaQueries && window.matchMedia('(prefers-color-scheme: light)').matches
        ? 'light'
        : 'dark';
    const theme = supportedThemes.has(storedPreference)
        ? storedPreference
        : (mobile ? systemTheme : 'dark');
    const source = supportedThemes.has(storedPreference)
        ? 'manual'
        : (mobile ? 'system' : 'bootstrap');

    document.documentElement.setAttribute('data-theme', theme);
    document.documentElement.setAttribute('data-theme-source', source);
    document.documentElement.style.colorScheme = theme;
}());
