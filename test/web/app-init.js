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
            const response = await fetch(`${API_LOGS}?format=text&type=${encodeURIComponent(activeLogType)}`, {
                cache: 'no-store'
            });
            if (!response.ok) {
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

function bindDashboardControls() {
    document.getElementById('feed-now-btn')?.addEventListener('click', triggerFeed);
    document.getElementById('relay-light-toggle')?.addEventListener('click', () => toggleRelayQuickAction('light'));
    document.getElementById('relay-filter-toggle')?.addEventListener('click', () => toggleRelayQuickAction('filter'));
}

function initDirtyTracking() {
    ['settings-sta-ssid', 'settings-sta-password', 'settings-ap-ssid', 'settings-ap-password'].forEach((id) => {
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
}

document.addEventListener('DOMContentLoaded', () => {
    initLocalIcons();
    updateClock();
    setInterval(updateClock, 1000);

    initMobileNavigation();
    initNavigation();
    initOTA();
    initScheduleTimeline();
    initDirtyTracking();
    bindScheduleShortcuts();
    bindLogsControls();
    bindSettingsControls();
    bindDashboardControls();
    document.getElementById('upload-btn')?.addEventListener('click', simulateOTA);

    setSettingsNetworkStatus('Zmiany SSID i hasel sa stosowane przy kolejnej sesji WiFi.', 'muted');
    setTemperatureStatus('Sterowanie grzalka jest synchronizowane ze sterownikiem.', 'muted');
    setDeviceActionStatus('Akcje administracyjne wymagaja potwierdzenia.', 'muted');

    startEventStream();
    startPollingFallback();
    fetchStatus(true);
    fetchLogs(true);
    renderLogs();
});
