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

function bindSettingsControls() {
    document.getElementById('save-network-btn')?.addEventListener('click', saveNetworkSettings);
    document.getElementById('start-wifi-session-btn')?.addEventListener('click', () => runWifiSessionAction('start'));
    document.getElementById('stop-wifi-session-btn')?.addEventListener('click', () => runWifiSessionAction('stop'));
    document.getElementById('save-temperature-btn')?.addEventListener('click', saveTemperatureSettings);
    document.getElementById('save-display-btn')?.addEventListener('click', saveDisplaySettings);
    document.getElementById('save-co2-btn')?.addEventListener('click', saveCo2Settings);
    document.getElementById('save-water-btn')?.addEventListener('click', saveWaterSettings);
    document.getElementById('save-leak-btn')?.addEventListener('click', saveLeakSettings);
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
                alert(error?.message || 'Nie udaĹ‚o siÄ™ zalogowaÄ‡ admina.');
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
}

function bindPinModalEvents() {
    const modal = document.getElementById('pin-modal');
    if (!modal) return;

    const keypad = modal.querySelector('.pin-keypad');
    keypad?.addEventListener('click', (event) => {
        const keyBtn = event.target.closest('.pin-key');
        if (!keyBtn) return;

        const val = keyBtn.getAttribute('data-val');
        const input = document.getElementById('pin-modal-input');
        if (!input) return;

        if (val === 'C') {
            input.value = '';
        } else if (val === 'OK') {
            window.handlePinSubmit();
        } else if (val !== null && val !== undefined) {
            if (input.value.length < 8) {
                input.value = `${input.value}${val}`.replace(/\D/g, '').slice(0, 8);
            }
        }
    });

    const cancelBtn = document.getElementById('pin-modal-cancel');
    cancelBtn?.addEventListener('click', () => {
        window.handlePinCancel();
    });

    const submitBtn = document.getElementById('pin-modal-submit');
    submitBtn?.addEventListener('click', () => {
        window.handlePinSubmit();
    });

    document.addEventListener('keydown', (event) => {
        if (modal.style.display !== 'flex') return;
        const input = document.getElementById('pin-modal-input');
        if (!input) return;

        if (event.key >= '0' && event.key <= '9') {
            if (input.value.length < 8) {
                input.value += event.key;
            }
            event.preventDefault();
        } else if (event.key === 'Backspace') {
            input.value = input.value.slice(0, -1);
            event.preventDefault();
        } else if (event.key === 'Escape') {
            window.handlePinCancel();
            event.preventDefault();
        } else if (event.key === 'Enter') {
            window.handlePinSubmit();
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
            setLeakStatus('Masz niezapisane zmiany zabezpieczeĹ„.', 'muted');
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

    setSettingsNetworkStatus('Zmiany SSID i haseĹ‚ sÄ… stosowane przy kolejnej sesji WiFi.', 'muted');
    setTemperatureStatus('Sterowanie grzaĹ‚kÄ… jest synchronizowane ze sterownikiem.', 'muted');
    setDeviceActionStatus('Akcje administracyjne wymagajÄ… potwierdzenia.', 'muted');

    startEventStream();
    startPollingFallback();
    fetchStatus(true);
    fetchLogs(true);
    renderLogs();
});
