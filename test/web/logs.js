function syncLogTabButtons() {
    const currentBtn = document.getElementById('logs-current-btn');
    const criticalBtn = document.getElementById('logs-critical-btn');
    const currentActive = activeLogType === 'normal';
    const criticalActive = activeLogType === 'critical';

    currentBtn?.classList.toggle('active', currentActive);
    criticalBtn?.classList.toggle('active', criticalActive);

    if (currentBtn) {
        currentBtn.style.background = currentActive ? 'rgba(6, 182, 212, 0.1)' : 'transparent';
        currentBtn.style.borderColor = currentActive ? 'rgba(6, 182, 212, 0.4)' : 'transparent';
        currentBtn.style.color = currentActive ? 'var(--accent-cyan)' : 'var(--text-main)';
    }

    if (criticalBtn) {
        criticalBtn.style.background = criticalActive ? 'rgba(239, 68, 68, 0.16)' : 'transparent';
        criticalBtn.style.borderColor = criticalActive ? 'rgba(239, 68, 68, 0.34)' : 'transparent';
        criticalBtn.style.color = criticalActive ? '#fca5a5' : 'var(--text-main)';
    }
}

function normalizeLogEntry(entry, fallbackLevel = 'info') {
    if (typeof entry === 'string') {
        return {
            ts: null,
            level: fallbackLevel,
            code: fallbackLevel,
            message: entry
        };
    }

    return {
        ts: toFiniteNumber(entry?.ts),
        level: String(entry?.level || fallbackLevel).toLowerCase(),
        code: String(entry?.code || fallbackLevel),
        message: String(entry?.message || '')
    };
}

function logLevelLabel(level) {
    if (level === 'error') {
        return 'ERROR';
    }
    if (level === 'warning') {
        return 'WARN';
    }
    return 'INFO';
}

function formatLogDisplayTime(ts) {
    if (ts === null || ts < 946684800) {
        return '--:--';
    }

    const date = new Date(ts * 1000);
    if (Number.isNaN(date.getTime())) {
        return '--:--';
    }

    return date.toLocaleTimeString('pl-PL', {
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit'
    });
}

function createLogRow(entry) {
    const levelLabel = logLevelLabel(entry.level);
    const levelClass = entry.level === 'error' ? 'logs-level-critical' : 'logs-level-info';
    const message = entry.code && entry.code !== entry.level
        ? `${entry.code}: ${entry.message}`
        : entry.message;

    return `
        <div class="logs-list-row">
            <span class="logs-level ${levelClass}">${escapeHtml(levelLabel)}</span>
            <span class="logs-time">${escapeHtml(formatLogDisplayTime(entry.ts))}</span>
            <span class="logs-message">${escapeHtml(message)}</span>
        </div>`;
}

function renderLogs() {
    const list = document.getElementById('logs-list');
    const infoCount = document.getElementById('info-count');
    const criticalCount = document.getElementById('critical-count');
    const searchInput = document.getElementById('logs-search');
    if (!list) return;

    syncLogTabButtons();

    const query = (searchInput?.value || '').trim().toLowerCase();
    const sourceRaw = activeLogType === 'critical' ? cachedLogs.critical : cachedLogs.normal;
    const source = sourceRaw.map((item) => normalizeLogEntry(item, activeLogType === 'critical' ? 'error' : 'info'));
    const filtered = source.filter((item) => {
        const haystack = `${item.message} ${item.code} ${item.level}`.toLowerCase();
        return haystack.includes(query);
    });

    const normalCount = Math.max(cachedLogs.normal.length, Math.trunc(Number(cachedLogs.counts?.normal) || 0));
    const criticalCountValue = Math.max(cachedLogs.critical.length, Math.trunc(Number(cachedLogs.counts?.critical) || 0));
    if (infoCount) infoCount.textContent = String(normalCount);
    if (criticalCount) criticalCount.textContent = String(criticalCountValue);

    const totalPages = Math.max(1, Math.ceil(filtered.length / LOGS_PAGE_SIZE));
    const currentPage = clamp(logsPage[activeLogType] || 0, 0, totalPages - 1);
    logsPage[activeLogType] = currentPage;

    setText('logs-page-info', `Strona ${currentPage + 1} / ${totalPages}`);
    setDisabled('logs-prev-btn', currentPage === 0);
    setDisabled('logs-next-btn', currentPage >= totalPages - 1);

    if (filtered.length === 0) {
        list.innerHTML = createLogRow({
            ts: null,
            level: activeLogType === 'critical' ? 'error' : 'info',
            code: 'empty',
            message: 'Brak logow dla wybranego filtra.'
        });
        return;
    }

    const pageStart = currentPage * LOGS_PAGE_SIZE;
    const pageItems = filtered.slice(pageStart, pageStart + LOGS_PAGE_SIZE);
    list.innerHTML = pageItems.map(createLogRow).join('');
}

function applyLogsPayload(logs) {
    cachedLogs.normal = Array.isArray(logs?.normal) ? logs.normal : [];
    cachedLogs.critical = Array.isArray(logs?.critical) ? logs.critical : [];
    cachedLogs.counts = {
        normal: Math.trunc(Number(logs?.counts?.normal) || cachedLogs.normal.length),
        critical: Math.trunc(Number(logs?.counts?.critical) || cachedLogs.critical.length)
    };
    renderLogs();
}
