function bindScheduleShortcuts() {
    document.getElementById('dashboard-edit-schedule-btn')?.addEventListener('click', () => {
        switchTab('harmonogramy');
    });
    document.getElementById('feeder-manage-btn')?.addEventListener('click', () => {
        switchTab('harmonogramy');
    });
    document.getElementById('save-schedule-btn')?.addEventListener('click', saveScheduleSettings);
}

function bindLogsControls() {
    const currentBtn = document.getElementById('logs-current-btn');
    const criticalBtn = document.getElementById('logs-critical-btn');
    const clearBtn = document.getElementById('clear-logs-btn');
    const deleteCriticalBtn = document.getElementById('delete-critical-btn');
    const downloadBtn = document.getElementById('download-logs-btn');
    const searchInput = document.getElementById('logs-search');
    const prevLogsBtn = document.getElementById('logs-prev-btn');
    const nextLogsBtn = document.getElementById('logs-next-btn');

    currentBtn?.addEventListener('click', () => {
        activeLogType = 'normal';
        renderLogs();
    });
    criticalBtn?.addEventListener('click', () => {
        activeLogType = 'critical';
        renderLogs();
    });
    clearBtn?.addEventListener('click', () => {
        if (searchInput) {
            searchInput.value = '';
        }
        logsPage[activeLogType] = 0;
        renderLogs();
    });
    deleteCriticalBtn?.addEventListener('click', async () => {
        try {
            if (!window.confirm('Usunac wszystkie logi krytyczne zapisane w sterowniku?')) {
                return;
            }
            await sendAction('clear_critical_logs');
            await fetchLogs(true);
        } catch (_) {}
    });
    downloadBtn?.addEventListener('click', async () => {
        try {
            if (!isAdminAuthenticated()) {
                await loginAsAdmin();
            }
            const pin = getAdminPinForRequest();
            if (!pin) {
                throw new Error('Wymagane logowanie admina.');
            }
            const response = await fetch(`${API_LOGS}?format=text&type=${encodeURIComponent(activeLogType)}&pin=${encodeURIComponent(pin)}`, {
                cache: 'no-store'
            });
            if (!response.ok) {
                if (response.status === 403) {
                    logoutAdmin();
                }
                throw new Error('Nie udalo sie pobrac logow z urzadzenia.');
            }
            const lines = await response.text();
            const blob = new Blob([lines], { type: 'text/plain;charset=utf-8' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `akwarium_logs_${activeLogType}_${Date.now()}.txt`;
            a.click();
            URL.revokeObjectURL(url);
        } catch (error) {
            console.warn('Nie udalo sie pobrac eksportu logow.', error);
        }
    });
    prevLogsBtn?.addEventListener('click', () => {
        logsPage[activeLogType] = Math.max(0, (logsPage[activeLogType] || 0) - 1);
        renderLogs();
    });
    nextLogsBtn?.addEventListener('click', () => {
        logsPage[activeLogType] = (logsPage[activeLogType] || 0) + 1;
        renderLogs();
    });
    searchInput?.addEventListener('input', () => {
        logsPage[activeLogType] = 0;
        renderLogs();
    });
}

function showSettingsPanel(targetId) {
    const targetPanel = document.getElementById(targetId);
    if (!targetPanel || !targetPanel.classList.contains('settings-panel')) {
        return;
    }

    document.querySelectorAll('.settings-panel').forEach((panel) => {
        panel.classList.toggle('active', panel.id === targetId);
    });

    document.querySelectorAll('.settings-nav-item').forEach((button) => {
        const isActive = button.dataset.settingsTarget === targetId;
        button.classList.toggle('active', isActive);
        button.setAttribute('aria-selected', isActive ? 'true' : 'false');
    });
}

function bindSettingsControls() {
    document.querySelectorAll('.settings-nav-item[data-settings-target]').forEach((button) => {
        button.setAttribute('aria-selected', button.classList.contains('active') ? 'true' : 'false');
        button.addEventListener('click', () => showSettingsPanel(button.dataset.settingsTarget));
    });

    document.getElementById('save-network-btn')?.addEventListener('click', saveNetworkSettings);
    document.getElementById('start-wifi-session-btn')?.addEventListener('click', () => runWifiSessionAction('start'));
    document.getElementById('stop-wifi-session-btn')?.addEventListener('click', () => runWifiSessionAction('stop'));
    document.getElementById('save-temperature-btn')?.addEventListener('click', saveTemperatureSettings);
    document.getElementById('save-display-btn')?.addEventListener('click', saveDisplaySettings);
    document.getElementById('save-co2-btn')?.addEventListener('click', saveCo2Settings);
    document.getElementById('save-water-btn')?.addEventListener('click', saveWaterSettings);
    document.getElementById('save-leak-btn')?.addEventListener('click', saveLeakSettings);
    document.getElementById('sync-browser-time-btn')?.addEventListener('click', syncBrowserTime);
    document.getElementById('sync-ntp-btn')?.addEventListener('click', syncTimeWithNtp);
    document.getElementById('restart-device-btn')?.addEventListener('click', () => {
        runDeviceAction(
            'restart_device',
            'Zrestartowac sterownik teraz?',
            'Polecenie restartu wyslane. Sterownik powinien wrocic online za chwile.'
        );
    });
    document.getElementById('factory-reset-btn')?.addEventListener('click', () => {
        runDeviceAction(
            'factory_reset',
            'Przywrocic ustawienia fabryczne? Ta operacja skasuje konfiguracje i zrestartuje urzadzenie.',
            'Factory reset zainicjowany. Urzadzenie uruchomi sie z ustawieniami domyslnymi.'
        );
    });
}

function bindAuthControls() {
    document.getElementById('admin-login-btn')?.addEventListener('click', async () => {
        try {
            await loginAsAdmin();
        } catch (error) {
            if (error?.code !== 'admin_login_cancelled' && error?.code !== 'pin_replaced') {
                alert(error?.message || 'Nie udało się zalogować admina.');
            }
        }
    });

    document.getElementById('admin-logout-btn')?.addEventListener('click', () => {
        logoutAdmin();
    });
}

function bindDashboardControls() {
    document.getElementById('feed-now-btn')?.addEventListener('click', triggerFeed);
    document.getElementById('relay-light-toggle')?.addEventListener('click', () => toggleRelayQuickAction('light'));
    document.getElementById('relay-filter-toggle')?.addEventListener('click', () => toggleRelayQuickAction('filter'));
    document.getElementById('relay-plant-toggle')?.addEventListener('click', () => toggleRelayQuickAction('plant'));
    document.getElementById('relay-heater-toggle')?.addEventListener('click', () => toggleRelayQuickAction('heater'));
    document.getElementById('relay-aeration-toggle')?.addEventListener('click', () => toggleRelayQuickAction('aeration'));
    document.getElementById('bus-scan-refresh')?.addEventListener('click', () => fetchHardwareBusDiagnostics(true));
}

function bindPinModalEvents() {
    const modal = document.getElementById('pin-modal');
    if (!modal) return;

    const form = document.getElementById('pin-modal-form');
    const input = document.getElementById('pin-modal-input');

    input?.addEventListener('input', () => {
        input.value = input.value.replace(/\D/g, '').slice(0, 8);
    });

    const cancelBtn = document.getElementById('pin-modal-cancel');
    cancelBtn?.addEventListener('click', () => {
        window.handlePinCancel();
    });

    form?.addEventListener('submit', (event) => {
        event.preventDefault();
        window.handlePinSubmit();
    });

    document.addEventListener('keydown', (event) => {
        if (modal.style.display !== 'flex') return;
        if (event.key === 'Escape') {
            window.handlePinCancel();
            event.preventDefault();
        }
    });
}

function initDirtyTracking() {
    ['settings-sta-ssid', 'settings-sta-password'].forEach((id) => {
        const el = document.getElementById(id);
        el?.addEventListener('input', () => {
            el.dataset.dirty = '1';
        });
    });
    ['settings-temp-enabled', 'settings-temp-target', 'settings-temp-hyst'].forEach((id) => {
        const el = document.getElementById(id);
        const eventName = el?.type === 'checkbox' ? 'change' : 'input';
        el?.addEventListener(eventName, () => {
            el.dataset.dirty = '1';
            setTemperatureStatus('Masz niezapisane zmiany ustawien temperatury.', 'muted');
        });
    });
    ['settings-co2-enabled', 'settings-co2-ph-target', 'settings-co2-limit'].forEach((id) => {
        const el = document.getElementById(id);
        const eventName = el?.type === 'checkbox' ? 'change' : 'input';
        el?.addEventListener(eventName, () => {
            el.dataset.dirty = '1';
            setCo2Status('Masz niezapisane zmiany automatyki CO2.', 'muted');
        });
    });
    ['settings-water-enabled', 'settings-water-timeout'].forEach((id) => {
        const el = document.getElementById(id);
        const eventName = el?.type === 'checkbox' ? 'change' : 'input';
        el?.addEventListener(eventName, () => {
            el.dataset.dirty = '1';
            setWaterStatus('Masz niezapisane zmiany automatycznej dolewki.', 'muted');
        });
    });
    ['settings-leak-enabled', 'settings-leak-action'].forEach((id) => {
        const el = document.getElementById(id);
        const eventName = el?.type === 'checkbox' ? 'change' : 'change';
        el?.addEventListener(eventName, () => {
            el.dataset.dirty = '1';
            setLeakStatus('Masz niezapisane zmiany zabezpieczeń.', 'muted');
        });
    });
}

document.addEventListener('DOMContentLoaded', () => {
    initLocalIcons();
    updateClock();
    setInterval(updateClock, 1000);

    initMobileNavigation();
    initNavigation();
    switchTab(getInitialTabFromHash(), false);
    initOTA();
    initScheduleTimeline();
    initDirtyTracking();
    bindScheduleShortcuts();
    bindLogsControls();
    bindSettingsControls();
    bindAuthControls();
    bindDashboardControls();
    bindPinModalEvents();
    initDisplaySettingsListeners();
    applyAuthState();
    document.getElementById('upload-btn')?.addEventListener('click', uploadFirmwarePackage);

    setSettingsNetworkStatus('Zmiany SSID i haseł są stosowane przy kolejnej sesji WiFi.', 'muted');
    setTemperatureStatus('Sterowanie grzałką jest synchronizowane ze sterownikiem.', 'muted');
    setDeviceActionStatus('Akcje administracyjne wymagają potwierdzenia.', 'muted');

    startWebSessionHeartbeat();
    startEventStream();
    startPollingFallback();
    fetchStatus(true, true);
    fetchLogs(true);
    renderLogs();
});
