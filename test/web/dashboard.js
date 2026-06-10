function normalizeFeedResultCode(code) {
    const raw = String(code || '').trim().toLowerCase();
    if (raw.startsWith('feed_')) {
        return raw.slice(5);
    }
    return raw;
}

function describeFeedResult(code, fallbackMessage = '') {
    switch (normalizeFeedResultCode(code)) {
        case 'ok':
            return {
                kind: 'success',
                title: 'Sukces',
                message: 'Karmienie zakonczylo sie pomyslnie.'
            };
        case 'busy':
            return {
                kind: 'error',
                title: 'Karmnik zajety',
                message: 'Poprzednie karmienie nadal trwa. Sprobuj ponownie za chwile.'
            };
        case 'sensor_not_ok':
            return {
                kind: 'error',
                title: 'Blad sensora',
                message: 'Sensor polozenia nie potwierdzil poprawnego cyklu karmienia.'
            };
        case 'timeout':
            return {
                kind: 'error',
                title: 'Timeout karmienia',
                message: 'Sterownik nie otrzymal potwierdzenia z sensora w oczekiwanym czasie.'
            };
        default:
            return {
                kind: 'error',
                title: 'Blad karmienia',
                message: fallbackMessage || 'Nie udalo sie uruchomic karmnika.'
            };
    }
}

function hideFeedModal() {
    const modal = document.getElementById('feed-modal');
    if (feedModalHideTimer) {
        clearTimeout(feedModalHideTimer);
        feedModalHideTimer = null;
    }
    if (modal) {
        modal.style.display = 'none';
    }
}

function showFeedModalState(kind, title, message, autoHideMs = 0) {
    const modal = document.getElementById('feed-modal');
    const icon = document.getElementById('modal-icon');
    const text = document.getElementById('modal-text');
    const subtext = document.getElementById('modal-subtext');

    if (!modal || !icon || !text || !subtext) {
        return;
    }

    if (feedModalHideTimer) {
        clearTimeout(feedModalHideTimer);
        feedModalHideTimer = null;
    }

    modal.style.display = 'flex';
    text.textContent = title;
    subtext.textContent = message;

    if (kind === 'success') {
        icon.className = 'fa-solid fa-check-circle fa-2xl';
        icon.style.color = 'var(--success-color)';
    } else if (kind === 'error') {
        icon.className = 'fa-solid fa-triangle-exclamation fa-2xl';
        icon.style.color = '#ef4444';
    } else {
        icon.className = 'fa-solid fa-spinner fa-spin fa-2xl';
        icon.style.color = 'var(--accent-cyan)';
    }
    renderLocalIcon(icon);

    if (autoHideMs > 0) {
        feedModalHideTimer = setTimeout(() => {
            hideFeedModal();
        }, autoHideMs);
    }
}

function resetFeedActionState() {
    feedActionState.awaitingResponse = false;
    feedActionState.awaitingCompletion = false;
    feedActionState.sawActive = false;
    feedActionState.startedAtMs = 0;
    feedActionState.baselineLastFeedEpoch = 0;
    feedActionState.baselineLastResult = '';
}

function createSvgEl(tag, attrs = {}) {
    const el = document.createElementNS('http://www.w3.org/2000/svg', tag);
    Object.entries(attrs).forEach(([key, value]) => {
        if (value !== undefined && value !== null) {
            el.setAttribute(key, String(value));
        }
    });
    return el;
}

function buildActiveBadge(iconClass, label, tone) {
    return `
        <div class="status-badge status-badge-${tone}">
            ${getLocalIconMarkup(iconClass)}
            <span>${escapeHtml(label)}</span>
        </div>`;
}

function buildModuleBadge(iconClass, label, active, activeTone) {
    return buildActiveBadge(iconClass, label, active ? activeTone : 'muted');
}

function renderTopbarActiveModules(data) {
    const container = document.getElementById('topbar-active-list');
    if (!container) return;

    const network = data.network || {};

    container.innerHTML = [
        buildModuleBadge('fa-satellite-dish', 'AP', !!network.apMode, 'success'),
        buildModuleBadge('fa-wifi', 'STA', !!network.staConnected, 'success')
    ].join('');
}

function renderTemperatureCard(temperature) {
    const currentValue = isValidTemperature(temperature?.current)
        ? Number(temperature.current).toFixed(1)
        : '--.-';

    setText('dashboard-temp-current', currentValue);
    setText('dashboard-temp-target', formatTemperature(temperature?.target));
    setText('dashboard-temp-hysteresis', formatTemperature(temperature?.hysteresis));
}

function renderBatteryWidgets(battery) {
    const voltage = toFiniteNumber(battery?.voltage);
    const percentRaw = toFiniteNumber(battery?.percent);
    const percent = percentRaw === null ? null : clamp(Math.round(percentRaw), 0, 100);
    const voltageText = voltage === null ? '--.--V' : `${voltage.toFixed(2)}V`;

    setText('rtc-battery', voltageText);
    setText('dashboard-battery-voltage', voltageText);
    setText('dashboard-battery-percent', percent === null ? '--' : String(percent));

    const fill = document.getElementById('dashboard-battery-fill');
    if (fill) {
        fill.style.width = `${percent === null ? 0 : percent}%`;
        if (percent !== null && percent <= 15) {
            fill.style.background = 'linear-gradient(90deg, #dc2626, #f87171)';
        } else if (percent !== null && percent <= 35) {
            fill.style.background = 'linear-gradient(90deg, #f59e0b, #fbbf24)';
        } else {
            fill.style.background = 'linear-gradient(90deg, #10b981, #34d399)';
        }
    }

    let stateLabel = 'Brak pomiaru';
    if (percent !== null) {
        if (percent <= 15) {
            stateLabel = 'Niski poziom';
        } else if (percent <= 35) {
            stateLabel = 'Warto obserwowac';
        } else {
            stateLabel = 'Poziom stabilny';
        }
    }
    setText('dashboard-battery-state', stateLabel);
}

function renderNetworkCard(network) {
    const card = document.getElementById('network-card');
    if (!card) return;

    const staConnected = !!network?.staConnected;
    const apMode = !!network?.apMode;
    const staConnecting = !!network?.staConnecting;
    const serviceMode = !!network?.serviceMode;
    const serviceModePending = !!network?.serviceModePending;
    const statusText = serviceModePending
        ? 'START WIFI...'
        : (staConnecting
        ? 'LACZENIE...'
        : (staConnected && apMode
            ? 'AP + STA'
            : (staConnected
                ? 'STA ONLINE'
                : (apMode ? 'TRYB AP' : (serviceMode ? 'SESJA WIFI' : 'RADIO OFF')))));
    const ssidText = staConnected || staConnecting
        ? (network?.staSsid || network?.configuredStaSsid || '-')
        : (apMode ? (network?.configuredApSsid || network?.ssid || '-') : '-');

    setText('network-status', statusText);
    setText('network-ssid', ssidText);
    setText('network-last-seen', formatEpoch(network?.staLastConnectedEpoch, { fallback: 'Brak historii' }));

    card.classList.remove('network-online', 'network-aponly', 'network-offline', 'network-connecting');
    const transitional = serviceModePending || staConnecting ||
        (serviceMode && !staConnected && !apMode);
    card.classList.add(transitional
        ? 'network-connecting'
        : (staConnected ? 'network-online' : (apMode ? 'network-aponly' : 'network-offline')));
}

async function toggleRelayQuickAction(kind) {
    const relayMap = {
        light: {
            relayValue: !!lastStatusData?.relays?.light,
            buttonId: 'relay-light-toggle',
            action: 'set_light'
        },
        filter: {
            relayValue: !!lastStatusData?.relays?.pump,
            buttonId: 'relay-filter-toggle',
            action: 'set_filter'
        }
    };

    const config = relayMap[kind];
    if (!config) return;

    const button = document.getElementById(config.buttonId);
    if (!button || button.dataset.busy === '1') return;

    const idleLabel = config.relayValue ? 'Wylacz' : 'Wlacz';
    const desiredState = config.relayValue ? '0' : '1';
    button.dataset.busy = '1';
    button.disabled = true;
    button.textContent = 'Zapisywanie...';

    try {
        await sendAction(config.action, { state: desiredState });
        await fetchStatus(true);
        await fetchLogs(true);
    } catch (error) {
        button.textContent = 'Blad';
        button.title = describeRequestError(error);
        setTimeout(() => {
            button.textContent = idleLabel;
            button.title = '';
        }, 1600);
    } finally {
        button.dataset.busy = '0';
        button.disabled = false;
        if (!button.title) {
            const relayActive = kind === 'light' ? !!lastStatusData?.relays?.light : !!lastStatusData?.relays?.pump;
            button.textContent = relayActive ? 'Wylacz' : 'Wlacz';
        }
    }
}

function modeValue(value) {
    const num = Number(value);
    return Number.isFinite(num) ? num : 0;
}

function setRelayCard(relayId, state, meta) {
    const card = document.getElementById(`relay-${relayId}`);
    const stateEl = document.getElementById(`relay-${relayId}-state`);
    const metaEl = document.getElementById(`relay-${relayId}-meta`);
    if (!card || !stateEl || !metaEl) return;

    card.classList.remove('relay-on', 'relay-off', 'relay-standby');
    card.classList.add(`relay-${state}`);
    stateEl.textContent = state.toUpperCase();
    metaEl.textContent = meta;
}

function renderRelays(data) {
    const schedule = data.schedule || {};
    const relays = data.relays || {};
    const aerationPercent = clamp(Number(relays.aerationPercent || 0), 0, 100);

    const lightMode = modeValue(schedule.lightMode);
    const filterMode = modeValue(schedule.filterMode);
    const airMode = modeValue(schedule.airMode);
    const heaterMode = modeValue(schedule.heaterMode);

    const lightState = relays.light ? 'on' : (lightMode === 2 ? 'off' : 'standby');
    const filterState = relays.pump ? 'on' : (filterMode === 2 ? 'off' : 'standby');
    const heaterState = relays.heater ? 'on' : (heaterMode === 1 ? 'off' : 'standby');
    const aerationState = aerationPercent > 0 ? 'on' : (airMode === 2 ? 'off' : 'standby');

    setRelayCard(
        'light',
        lightState,
        lightMode === 1 ? 'Zawsze wlaczone' : (lightMode === 2 ? 'Recznie wylaczone' : formatRange(schedule.dayStartHour, schedule.dayStartMin, schedule.dayEndHour, schedule.dayEndMin))
    );
    setRelayCard(
        'filter',
        filterState,
        filterMode === 1 ? 'Zawsze wlaczony' : (filterMode === 2 ? 'Recznie wylaczony' : formatRange(schedule.filterStartHour, schedule.filterStartMin, schedule.filterEndHour, schedule.filterEndMin))
    );
    setRelayCard(
        'heater',
        heaterState,
        heaterState === 'on' ? 'Dogrzewanie aktywne' : (heaterMode === 1 ? 'Tryb OFF' : `Cel ${formatTemperature(data.temperature?.target)}`)
    );
    setRelayCard(
        'aeration',
        aerationState,
        aerationState === 'on' ? `Otwarcie ${aerationPercent}%` : (airMode === 1 ? 'Zawsze aktywne' : (airMode === 2 ? 'Recznie zamkniete' : formatRange(schedule.airStartHour, schedule.airStartMin, schedule.airEndHour, schedule.airEndMin)))
    );

    const activeCount = [lightState, filterState, heaterState, aerationState].filter((state) => state === 'on').length;
    setText('relay-count', `${activeCount} / 4 aktywne`);

    const lightToggle = document.getElementById('relay-light-toggle');
    if (lightToggle && lightToggle.dataset.busy !== '1') {
        lightToggle.textContent = relays.light ? 'Wylacz' : 'Wlacz';
    }

    const filterToggle = document.getElementById('relay-filter-toggle');
    if (filterToggle && filterToggle.dataset.busy !== '1') {
        filterToggle.textContent = relays.pump ? 'Wylacz' : 'Wlacz';
    }
}

function describeFeedSchedule(feeding) {
    const freqLabel = formatFeedFrequency(feeding?.freq);
    if (freqLabel === 'Wylaczone') {
        return 'Automatyczne karmienie wylaczone';
    }
    return `${freqLabel} ${formatTime(feeding?.hour, feeding?.minute)}`;
}

function renderTodaySchedule(data) {
    const list = document.getElementById('today-schedule-list');
    if (!list) return;

    const schedule = data.schedule || {};
    const feeding = data.feeding || {};
    const items = [
        {
            label: 'Swiatlo',
            value: modeValue(schedule.lightMode) === 1
                ? 'Zawsze wlaczone'
                : (modeValue(schedule.lightMode) === 2
                    ? 'Wylaczone'
                    : formatRange(schedule.dayStartHour, schedule.dayStartMin, schedule.dayEndHour, schedule.dayEndMin))
        },
        {
            label: 'Filtr',
            value: modeValue(schedule.filterMode) === 1
                ? 'Zawsze wlaczony'
                : (modeValue(schedule.filterMode) === 2
                    ? 'Wylaczony'
                    : formatRange(schedule.filterStartHour, schedule.filterStartMin, schedule.filterEndHour, schedule.filterEndMin))
        },
        {
            label: 'Napowietrzanie',
            value: modeValue(schedule.airMode) === 1
                ? 'Zawsze aktywne'
                : (modeValue(schedule.airMode) === 2
                    ? 'Wylaczone'
                    : formatRange(schedule.airStartHour, schedule.airStartMin, schedule.airEndHour, schedule.airEndMin))
        },
        {
            label: 'Karmienie',
            value: describeFeedSchedule(feeding)
        }
    ];

    list.innerHTML = items.map((item) => `
        <div class="schedule-summary-item">
            <span>${escapeHtml(item.label)}</span>
            <strong>${escapeHtml(item.value)}</strong>
        </div>`).join('');
}

function renderFeederCard(data) {
    const feeding = data.feeding || {};
    const lastResult = normalizeFeedResultCode(feeding.lastResult);
    setText('feed-next-label', describeFeedSchedule(feeding));

    const lastFeedText = formatEpoch(feeding.lastFeedEpoch, {
        fallback: 'brak danych',
        includeDate: true,
        includeSeconds: false
    });
    setText('feed-last-label', `Ostatnie karmienie: ${lastFeedText}`);

    if (feeding.active && (feedActionState.awaitingResponse || feedActionState.awaitingCompletion)) {
        feedActionState.awaitingResponse = false;
        feedActionState.awaitingCompletion = true;
        feedActionState.sawActive = true;
        showFeedModalState('progress', 'Trwa karmienie...', 'Sensor polozenia jest w trakcie odczytu.');
    } else if (feedActionState.awaitingCompletion && !feeding.active) {
        const lastFeedEpoch = Math.max(0, Math.trunc(Number(feeding.lastFeedEpoch) || 0));
        const resultChanged = lastResult && lastResult !== feedActionState.baselineLastResult;
        const feedConfirmed = feedActionState.sawActive ||
            lastFeedEpoch > feedActionState.baselineLastFeedEpoch ||
            resultChanged;
        const waitExpired = (Date.now() - feedActionState.startedAtMs) > 15000;

        if (feedConfirmed || waitExpired) {
            const result = describeFeedResult(waitExpired && !feedConfirmed ? 'timeout' : lastResult, '');
            showFeedModalState(result.kind, result.title, result.message, 2400);
            resetFeedActionState();
        }
    }

    const button = document.getElementById('feed-now-btn');
    if (button) {
        const busy = !!feeding.active || feedActionState.awaitingResponse || feedActionState.awaitingCompletion;
        button.disabled = busy;
        button.textContent = feeding.active
            ? 'Trwa...'
            : (feedActionState.awaitingResponse ? 'Start...' : (feedActionState.awaitingCompletion ? 'Czekam...' : 'Karm teraz'));
    }
}

function showChartTooltip(clientX, clientY, point) {
    const tooltip = document.getElementById('temperature-chart-tooltip');
    const shell = document.querySelector('.temp-chart-shell');
    if (!tooltip || !shell) return;

    tooltip.innerHTML = `<strong>${escapeHtml(point.valueLabel)}</strong><span>${escapeHtml(point.timeLabel)}</span>`;
    tooltip.hidden = false;

    const rect = shell.getBoundingClientRect();
    const width = tooltip.offsetWidth || 148;
    const height = tooltip.offsetHeight || 56;
    let left = clientX - rect.left - width / 2;
    let top = clientY - rect.top - height - 14;

    left = clamp(left, 10, rect.width - width - 10);
    top = clamp(top, 10, rect.height - height - 10);

    tooltip.style.left = `${left}px`;
    tooltip.style.top = `${top}px`;
}

function hideChartTooltip() {
    const tooltip = document.getElementById('temperature-chart-tooltip');
    if (tooltip) {
        tooltip.hidden = true;
    }
}

let temperatureChartResizeObserver = null;
let temperatureChartResizeFrame = 0;
let observedTemperatureChartShell = null;
let observedTemperatureChartWidth = 0;

function formatChartTemperature(value, digits = 1) {
    return `${Number(value).toFixed(digits)}\u00B0C`;
}

function buildSmoothChartPath(coords) {
    if (!coords.length) return '';
    if (coords.length === 1) {
        return `M ${coords[0].x.toFixed(2)} ${coords[0].y.toFixed(2)}`;
    }

    const tension = 0.18;
    let path = `M ${coords[0].x.toFixed(2)} ${coords[0].y.toFixed(2)}`;
    for (let index = 0; index < coords.length - 1; index += 1) {
        const p0 = coords[index - 1] || coords[index];
        const p1 = coords[index];
        const p2 = coords[index + 1];
        const p3 = coords[index + 2] || p2;
        const cp1x = p1.x + (p2.x - p0.x) * tension;
        const cp1y = p1.y + (p2.y - p0.y) * tension;
        const cp2x = p2.x - (p3.x - p1.x) * tension;
        const cp2y = p2.y - (p3.y - p1.y) * tension;
        path += ` C ${cp1x.toFixed(2)} ${cp1y.toFixed(2)} ${cp2x.toFixed(2)} ${cp2y.toFixed(2)} ${p2.x.toFixed(2)} ${p2.y.toFixed(2)}`;
    }
    return path;
}

function renderTemperatureChartSummary(summary, points, target, hysteresis, guidesVisible) {
    if (!summary) return;

    const latest = points[points.length - 1];
    const badges = [
        `<span class="temp-chart-pill temp-chart-pill-live">Ostatni <strong>${escapeHtml(latest.valueLabel)}</strong></span>`
    ];

    if (target !== null) {
        badges.push(`<span class="temp-chart-pill">Cel <strong>${escapeHtml(formatChartTemperature(target))}</strong></span>`);
        if (hysteresis > 0) {
            const lower = formatChartTemperature(target - hysteresis);
            const upper = formatChartTemperature(target + hysteresis);
            badges.push(
                `<span class="temp-chart-pill ${guidesVisible ? '' : 'temp-chart-pill-warning'}">Pasmo <strong>${escapeHtml(`${lower} - ${upper}`)}</strong>${guidesVisible ? '' : ' poza wykresem'}</span>`
            );
        }
    }

    summary.innerHTML = badges.join('');
}

function renderTemperatureChart(temperature) {
    const svg = document.getElementById('temperature-chart-svg');
    const empty = document.getElementById('temperature-chart-empty');
    const summary = document.getElementById('temperature-chart-summary');
    if (!svg || !empty) return;

    const rawHistory = Array.isArray(temperature?.history) ? temperature.history : [];
    const historyCapacity = Math.max(1, Math.round(toFiniteNumber(temperature?.historyCapacity) ?? 20));
    const historyIntervalMinutes = Math.max(1, Math.round(toFiniteNumber(temperature?.historyIntervalMinutes) ?? 10));
    const points = rawHistory
        .map((item, index) => ({
            value: toFiniteNumber(item?.value),
            epoch: toFiniteNumber(item?.epoch),
            index
        }))
        .filter((item) => isValidTemperature(item.value))
        .slice(-historyCapacity)
        .map((item, index, arr) => ({
            value: item.value,
            epoch: item.epoch,
            valueLabel: formatChartTemperature(item.value, 2),
            timeLabel: formatEpoch(item.epoch, {
                fallback: `Pomiar ${index + 1} z ${arr.length}`,
                includeDate: false,
                includeSeconds: false
            })
        }));

    setText('temperature-chart-meta', `${points.length || 0} / ${historyCapacity} pomiarow co ${historyIntervalMinutes} min`);
    setText('temperature-chart-start', points.length ? points[0].timeLabel : 'Najstarszy pomiar');
    setText('temperature-chart-end', points.length ? points[points.length - 1].timeLabel : 'Teraz');
    svg.innerHTML = '';
    if (summary) summary.innerHTML = '';
    hideChartTooltip();

    if (points.length === 0) {
        empty.hidden = false;
        return;
    }

    empty.hidden = true;

    const width = 960;
    const height = 320;
    const padding = { top: 18, right: 34, bottom: 34, left: 58 };
    const plotWidth = width - padding.left - padding.right;
    const plotHeight = height - padding.top - padding.bottom;
    const target = toFiniteNumber(temperature?.target);
    const hysteresis = Math.abs(toFiniteNumber(temperature?.hysteresis) ?? 0);

    let minValue = Math.min(...points.map((point) => point.value));
    let maxValue = Math.max(...points.map((point) => point.value));
    if (!Number.isFinite(minValue) || !Number.isFinite(maxValue)) {
        minValue = 20;
        maxValue = 30;
    }
    if (Math.abs(maxValue - minValue) < 0.6) {
        maxValue += 0.3;
        minValue -= 0.3;
    }

    const dataSpan = Math.max(0.6, maxValue - minValue);
    const basePad = Math.max(0.18, dataSpan * 0.18);
    minValue -= basePad;
    maxValue += basePad;

    let guidesVisible = false;
    if (target !== null) {
        const guideMin = hysteresis > 0 ? target - hysteresis : target;
        const guideMax = hysteresis > 0 ? target + hysteresis : target;
        const guideGap = Math.max(0, minValue - guideMax, guideMin - maxValue);
        guidesVisible = guideGap <= Math.max(0.65, dataSpan * 0.85);

        if (guidesVisible) {
            const guidePad = Math.max(0.12, dataSpan * 0.08);
            minValue = Math.min(minValue, guideMin - guidePad);
            maxValue = Math.max(maxValue, guideMax + guidePad);
        }
    }

    renderTemperatureChartSummary(summary, points, target, hysteresis, guidesVisible);

    const xFor = (index) => padding.left + (points.length === 1 ? plotWidth / 2 : (index / (points.length - 1)) * plotWidth);
    const yFor = (value) => padding.top + ((maxValue - value) / (maxValue - minValue)) * plotHeight;

    const defs = createSvgEl('defs');
    const gradient = createSvgEl('linearGradient', {
        id: 'chart-area-gradient',
        x1: '0%',
        y1: '0%',
        x2: '0%',
        y2: '100%'
    });
    const startStop = createSvgEl('stop', { offset: '0%', 'stop-color': '#22d3ee', 'stop-opacity': '0.2' });
    const endStop = createSvgEl('stop', { offset: '100%', 'stop-color': '#22d3ee', 'stop-opacity': '0.015' });
    gradient.appendChild(startStop);
    gradient.appendChild(endStop);
    defs.appendChild(gradient);
    svg.appendChild(defs);

    svg.appendChild(createSvgEl('rect', {
        x: padding.left,
        y: padding.top,
        width: plotWidth,
        height: plotHeight,
        rx: 18,
        class: 'chart-plot-frame'
    }));

    if (target !== null && guidesVisible && hysteresis > 0) {
        const upperY = yFor(target + hysteresis);
        const lowerY = yFor(target - hysteresis);
        svg.appendChild(createSvgEl('rect', {
            x: padding.left,
            y: Math.min(upperY, lowerY),
            width: plotWidth,
            height: Math.max(2, Math.abs(lowerY - upperY)),
            rx: 14,
            class: 'chart-band'
        }));
    }

    const gridSteps = 4;
    for (let i = 0; i <= gridSteps; i += 1) {
        const ratio = i / gridSteps;
        const y = padding.top + ratio * plotHeight;
        const value = maxValue - ratio * (maxValue - minValue);
        svg.appendChild(createSvgEl('line', {
            x1: padding.left,
            y1: y,
            x2: padding.left + plotWidth,
            y2: y,
            class: 'chart-grid-line'
        }));
        const label = createSvgEl('text', {
            x: 6,
            y: y + 4,
            class: 'chart-grid-label'
        });
        label.textContent = formatChartTemperature(value);
        svg.appendChild(label);
    }

    if (target !== null && guidesVisible) {
        const targetY = yFor(target);
        svg.appendChild(createSvgEl('line', {
            x1: padding.left,
            y1: targetY,
            x2: padding.left + plotWidth,
            y2: targetY,
            class: 'chart-target-line'
        }));
        const targetLabel = createSvgEl('text', {
            x: padding.left + plotWidth + 10,
            y: targetY + 4,
            class: 'chart-guide-label'
        });
        targetLabel.textContent = `Cel ${target.toFixed(1)}°C`;
        svg.appendChild(targetLabel);

        if (hysteresis > 0) {
            const upper = target + hysteresis;
            const lower = target - hysteresis;
            [upper, lower].forEach((value, index) => {
                const lineY = yFor(value);
                svg.appendChild(createSvgEl('line', {
                    x1: padding.left,
                    y1: lineY,
                    x2: padding.left + plotWidth,
                    y2: lineY,
                    class: 'chart-hysteresis-line'
                }));
                const guide = createSvgEl('text', {
                    x: padding.left + plotWidth + 10,
                    y: lineY + 4,
                    class: 'chart-guide-label hysteresis'
                });
                guide.textContent = `${index === 0 ? 'H +' : 'H -'} ${value.toFixed(1)}°C`;
                svg.appendChild(guide);
            });
        }
    }

    const coords = points.map((point, index) => ({
        x: xFor(index),
        y: yFor(point.value)
    }));
    const pathData = buildSmoothChartPath(coords);
    const firstCoord = coords[0];
    const lastCoord = coords[coords.length - 1];
    const areaData = `${pathData} L ${lastCoord.x.toFixed(2)} ${(padding.top + plotHeight).toFixed(2)} L ${firstCoord.x.toFixed(2)} ${(padding.top + plotHeight).toFixed(2)} Z`;
    svg.appendChild(createSvgEl('path', { d: areaData, class: 'chart-area' }));
    svg.appendChild(createSvgEl('path', { d: pathData, class: 'chart-line' }));
    svg.appendChild(createSvgEl('circle', {
        cx: lastCoord.x,
        cy: lastCoord.y,
        r: 10,
        class: 'chart-point-current'
    }));

    points.forEach((point, index) => {
        const { x, y } = coords[index];

        const circle = createSvgEl('circle', {
            cx: x,
            cy: y,
            r: index === points.length - 1 ? 5 : 4,
            class: 'chart-point'
        });
        const title = createSvgEl('title');
        title.textContent = `${point.valueLabel} - ${point.timeLabel}`;
        circle.appendChild(title);
        svg.appendChild(circle);

        const hit = createSvgEl('circle', {
            cx: x,
            cy: y,
            r: 13,
            tabindex: 0,
            class: 'chart-point-hit'
        });
        hit.addEventListener('mouseenter', (event) => showChartTooltip(event.clientX, event.clientY, point));
        hit.addEventListener('mousemove', (event) => showChartTooltip(event.clientX, event.clientY, point));
        hit.addEventListener('mouseleave', hideChartTooltip);
        hit.addEventListener('focus', () => {
            const rect = svg.getBoundingClientRect();
            showChartTooltip(rect.left + x, rect.top + y, point);
        });
        hit.addEventListener('blur', hideChartTooltip);
        svg.appendChild(hit);
    });
}

function formatChartTemperatureDelta(value, digits = 1) {
    const numeric = Number(value);
    return `${numeric > 0 ? '+' : ''}${numeric.toFixed(digits)}\u00B0C`;
}

function estimateChartChipWidth(text) {
    return Math.max(88, Math.round(String(text || '').length * 7.1 + 24));
}

function appendChartChip(svg, options) {
    const {
        x,
        y,
        text,
        tone = 'neutral',
        anchor = 'start',
        bounds = null
    } = options || {};

    if (!svg || !text) return null;

    const height = 24;
    const width = estimateChartChipWidth(text);
    let left = anchor === 'end' ? x - width : x;
    let top = y;

    if (bounds) {
        left = clamp(left, bounds.minX, bounds.maxX - width);
        top = clamp(top, bounds.minY, bounds.maxY - height);
    }

    const group = createSvgEl('g', { class: `chart-chip chart-chip-${tone}` });
    const background = createSvgEl('rect', {
        x: left,
        y: top,
        width,
        height,
        rx: 12,
        class: 'chart-chip-bg'
    });
    const label = createSvgEl('text', {
        x: left + 12,
        y: top + height / 2,
        class: 'chart-chip-text'
    });
    label.setAttribute('dominant-baseline', 'middle');
    label.textContent = text;

    group.appendChild(background);
    group.appendChild(label);
    svg.appendChild(group);
    return group;
}

function describeTemperatureTrend(points) {
    if (points.length < 2) {
        return {
            label: 'Start serii',
            tone: 'neutral'
        };
    }

    const windowStart = Math.max(0, points.length - Math.min(points.length, 4));
    const reference = points[windowStart].value;
    const latest = points[points.length - 1].value;
    const delta = latest - reference;

    if (Math.abs(delta) < 0.12) {
        return {
            label: 'Stabilnie',
            tone: 'calm'
        };
    }

    return delta > 0
        ? {
            label: `Rosnie ${formatChartTemperatureDelta(delta)}`,
            tone: 'trend-up'
        }
        : {
            label: `Spada ${formatChartTemperatureDelta(delta)}`,
            tone: 'trend-down'
        };
}

function describeTargetRelationship(latestValue, target, hysteresis) {
    if (target === null) {
        return null;
    }

    const delta = latestValue - target;
    const withinBand = hysteresis > 0
        ? Math.abs(delta) <= hysteresis
        : Math.abs(delta) < 0.12;

    if (withinBand) {
        return {
            label: 'W pasmie',
            detailLabel: 'Temperatura jest w pasmie docelowym.',
            tone: 'ok'
        };
    }

    return delta < 0
        ? {
            label: `Do celu ${formatChartTemperature(Math.abs(delta))}`,
            detailLabel: `Ponizej celu o ${formatChartTemperature(Math.abs(delta))}.`,
            tone: 'warning'
        }
        : {
            label: `Nad celem ${formatChartTemperature(Math.abs(delta))}`,
            detailLabel: `Powyzej celu o ${formatChartTemperature(Math.abs(delta))}.`,
            tone: 'warning'
        };
}

function buildTemperatureDomain(points, target, hysteresis, snapshotMode) {
    const sourceValues = points.map((point) => point.value);
    if (target !== null && snapshotMode) {
        sourceValues.push(target);
        if (hysteresis > 0) {
            sourceValues.push(target - hysteresis, target + hysteresis);
        }
    }

    let minValue = Math.min(...sourceValues);
    let maxValue = Math.max(...sourceValues);
    if (!Number.isFinite(minValue) || !Number.isFinite(maxValue)) {
        minValue = 20;
        maxValue = 30;
    }

    if (snapshotMode) {
        const minimumSpan = Math.max(2.2, hysteresis > 0 ? hysteresis * 5.2 : 0);
        let span = maxValue - minValue;
        if (span < minimumSpan) {
            const grow = (minimumSpan - span) / 2;
            minValue -= grow;
            maxValue += grow;
            span = minimumSpan;
        }

        const pad = Math.max(0.26, span * 0.12);
        return {
            minValue: minValue - pad,
            maxValue: maxValue + pad,
            guidesVisible: target !== null
        };
    }

    if (Math.abs(maxValue - minValue) < 0.6) {
        maxValue += 0.3;
        minValue -= 0.3;
    }

    const dataSpan = Math.max(0.6, maxValue - minValue);
    const basePad = Math.max(0.18, dataSpan * 0.18);
    minValue -= basePad;
    maxValue += basePad;

    let guidesVisible = false;
    if (target !== null) {
        const guideMin = hysteresis > 0 ? target - hysteresis : target;
        const guideMax = hysteresis > 0 ? target + hysteresis : target;
        const guideGap = Math.max(0, minValue - guideMax, guideMin - maxValue);
        guidesVisible = guideGap <= Math.max(0.28, dataSpan * 0.42);

        if (guidesVisible) {
            const guidePad = Math.max(0.12, dataSpan * 0.08);
            minValue = Math.min(minValue, guideMin - guidePad);
            maxValue = Math.max(maxValue, guideMax + guidePad);
        }
    }

    return { minValue, maxValue, guidesVisible };
}

function appendTemperatureChartDefs(svg) {
    const defs = createSvgEl('defs');

    const areaGradient = createSvgEl('linearGradient', {
        id: 'chart-area-gradient',
        x1: '0%',
        y1: '0%',
        x2: '0%',
        y2: '100%'
    });
    areaGradient.appendChild(createSvgEl('stop', { offset: '0%', 'stop-color': '#22d3ee', 'stop-opacity': '0.26' }));
    areaGradient.appendChild(createSvgEl('stop', { offset: '55%', 'stop-color': '#0ea5e9', 'stop-opacity': '0.12' }));
    areaGradient.appendChild(createSvgEl('stop', { offset: '100%', 'stop-color': '#22d3ee', 'stop-opacity': '0.02' }));

    const lineGradient = createSvgEl('linearGradient', {
        id: 'chart-line-gradient',
        x1: '0%',
        y1: '0%',
        x2: '100%',
        y2: '0%'
    });
    lineGradient.appendChild(createSvgEl('stop', { offset: '0%', 'stop-color': '#67e8f9' }));
    lineGradient.appendChild(createSvgEl('stop', { offset: '100%', 'stop-color': '#38bdf8' }));

    const bandGradient = createSvgEl('linearGradient', {
        id: 'chart-band-gradient',
        x1: '0%',
        y1: '0%',
        x2: '100%',
        y2: '0%'
    });
    bandGradient.appendChild(createSvgEl('stop', { offset: '0%', 'stop-color': '#7c2d12', 'stop-opacity': '0.72' }));
    bandGradient.appendChild(createSvgEl('stop', { offset: '100%', 'stop-color': '#fb923c', 'stop-opacity': '0.94' }));

    defs.appendChild(areaGradient);
    defs.appendChild(lineGradient);
    defs.appendChild(bandGradient);
    svg.appendChild(defs);
}

function appendTemperatureChartFrame(svg, padding, plotWidth, plotHeight) {
    svg.appendChild(createSvgEl('rect', {
        x: padding.left,
        y: padding.top,
        width: plotWidth,
        height: plotHeight,
        rx: 18,
        class: 'chart-plot-backdrop'
    }));
    svg.appendChild(createSvgEl('rect', {
        x: padding.left,
        y: padding.top,
        width: plotWidth,
        height: plotHeight,
        rx: 18,
        class: 'chart-plot-frame'
    }));
}

function getTemperatureChartLayout(shellWidth, snapshotMode) {
    const width = Math.round(Number(shellWidth) || 0);

    if (width > 0 && width < 560) {
        return snapshotMode
            ? {
                name: 'phone',
                width: 420,
                height: 380,
                padding: { top: 18, right: 18, bottom: 28, left: 18 },
                gridSteps: 4,
                compact: true,
                snapshotStacked: true
            }
            : {
                name: 'phone',
                width: 420,
                height: 360,
                padding: { top: 18, right: 20, bottom: 42, left: 42 },
                gridSteps: 3,
                compact: true,
                snapshotStacked: false
            };
    }

    if (width > 0 && width < 920) {
        return snapshotMode
            ? {
                name: 'tablet',
                width: 720,
                height: 340,
                padding: { top: 18, right: 24, bottom: 30, left: 30 },
                gridSteps: 4,
                compact: true,
                snapshotStacked: false
            }
            : {
                name: 'tablet',
                width: 720,
                height: 312,
                padding: { top: 18, right: 28, bottom: 38, left: 50 },
                gridSteps: 4,
                compact: true,
                snapshotStacked: false
            };
    }

    return {
        name: 'desktop',
        width: 960,
        height: 320,
        padding: { top: 20, right: 34, bottom: 34, left: 58 },
        gridSteps: 4,
        compact: false,
        snapshotStacked: false
    };
}

function applyTemperatureChartLayout(svg, shell, layout, snapshotMode) {
    if (!svg || !layout) return;

    svg.setAttribute('viewBox', `0 0 ${layout.width} ${layout.height}`);
    svg.style.aspectRatio = `${layout.width} / ${layout.height}`;

    if (!shell) return;

    shell.dataset.chartLayout = layout.name;
    shell.classList.toggle('temp-chart-shell-snapshot', snapshotMode);
    shell.classList.toggle('temp-chart-shell-snapshot-stacked', snapshotMode && layout.snapshotStacked);
}

function ensureTemperatureChartResizeObserver(shell) {
    if (!shell || typeof ResizeObserver === 'undefined') {
        return;
    }

    if (temperatureChartResizeObserver && observedTemperatureChartShell === shell) {
        return;
    }

    if (temperatureChartResizeObserver && observedTemperatureChartShell) {
        temperatureChartResizeObserver.unobserve(observedTemperatureChartShell);
    }

    observedTemperatureChartShell = shell;
    observedTemperatureChartWidth = Math.round(shell.getBoundingClientRect().width);
    temperatureChartResizeObserver = new ResizeObserver((entries) => {
        const entry = entries[entries.length - 1];
        const nextWidth = Math.round(entry?.contentRect?.width || 0);

        if (!nextWidth || Math.abs(nextWidth - observedTemperatureChartWidth) < 2) {
            return;
        }

        observedTemperatureChartWidth = nextWidth;
        if (temperatureChartResizeFrame) {
            cancelAnimationFrame(temperatureChartResizeFrame);
        }

        temperatureChartResizeFrame = requestAnimationFrame(() => {
            temperatureChartResizeFrame = 0;
            if (lastStatusData?.temperature) {
                renderTemperatureChart(lastStatusData.temperature || {});
            }
        });
    });

    temperatureChartResizeObserver.observe(shell);
}

function renderTemperatureSnapshot(svg, points, target, hysteresis, domain, layout) {
    const width = layout.width;
    const height = layout.height;
    const padding = layout.padding;
    const plotWidth = width - padding.left - padding.right;
    const plotHeight = height - padding.top - padding.bottom;
    const latest = points[points.length - 1];
    const relation = describeTargetRelationship(latest.value, target, hysteresis);
    const compact = !!layout.compact;
    const stacked = !!layout.snapshotStacked;
    const panelInset = compact ? 14 : 18;
    const panelX = padding.left + panelInset;
    const panelY = padding.top + (stacked ? 12 : 18);
    const panelWidth = stacked
        ? plotWidth - panelInset * 2
        : Math.min(232, Math.max(206, plotWidth * 0.28));
    const panelHeight = stacked ? 132 : plotHeight - 36;
    const dividerX = panelX + panelWidth + (compact ? 18 : 22);
    const rail = stacked
        ? {
            startX: padding.left + 30,
            endX: padding.left + plotWidth - 30,
            y: panelY + panelHeight + 86
        }
        : {
            startX: dividerX + (compact ? 42 : 58),
            endX: padding.left + plotWidth - (compact ? 28 : 44),
            y: padding.top + Math.round(plotHeight * 0.48)
        };
    const span = Math.max(0.1, domain.maxValue - domain.minValue);
    const valueToX = (value) => rail.startX + ((value - domain.minValue) / span) * (rail.endX - rail.startX);
    const currentX = valueToX(latest.value);
    const chipBounds = {
        minX: rail.startX - 8,
        maxX: rail.endX + 8,
        minY: padding.top + 8,
        maxY: height - padding.bottom - 8
    };

    appendTemperatureChartDefs(svg);
    appendTemperatureChartFrame(svg, padding, plotWidth, plotHeight);

    svg.appendChild(createSvgEl('rect', {
        x: panelX,
        y: panelY,
        width: panelWidth,
        height: panelHeight,
        rx: compact ? 18 : 22,
        class: 'chart-snapshot-panel'
    }));
    if (!stacked) {
        svg.appendChild(createSvgEl('line', {
            x1: dividerX,
            y1: padding.top + 24,
            x2: dividerX,
            y2: padding.top + plotHeight - 24,
            class: 'chart-snapshot-divider'
        }));
    }

    const eyebrow = createSvgEl('text', {
        x: panelX + 20,
        y: panelY + (stacked ? 24 : 30),
        class: 'chart-snapshot-eyebrow'
    });
    eyebrow.textContent = 'AKTUALNY ODCZYT';
    svg.appendChild(eyebrow);

    const value = createSvgEl('text', {
        x: panelX + 20,
        y: panelY + (stacked ? 66 : 96),
        class: 'chart-snapshot-value'
    });
    value.textContent = latest.value.toFixed(2);
    svg.appendChild(value);

    const unit = createSvgEl('text', {
        x: panelX + (stacked ? Math.min(panelWidth - 54, 164) : 168),
        y: panelY + (stacked ? 66 : 96),
        class: 'chart-snapshot-unit'
    });
    unit.textContent = '\u00B0C';
    svg.appendChild(unit);

    const status = createSvgEl('text', {
        x: panelX + 20,
        y: panelY + (stacked ? 94 : 130),
        class: `chart-snapshot-status chart-snapshot-status-${relation?.tone || 'neutral'}`
    });
    status.textContent = relation ? relation.detailLabel : 'Brak temperatury docelowej.';
    svg.appendChild(status);

    const time = createSvgEl('text', {
        x: panelX + 20,
        y: panelY + (stacked ? 112 : 156),
        class: 'chart-snapshot-time'
    });
    time.textContent = `Pomiar ${latest.timeLabel}`;
    svg.appendChild(time);

    if (points.length > 1) {
        const trend = describeTemperatureTrend(points);
        const trendText = createSvgEl('text', {
            x: panelX + 20,
            y: panelY + (stacked ? 128 : 182),
            class: 'chart-snapshot-time'
        });
        trendText.textContent = trend.label;
        svg.appendChild(trendText);
    }

    const railTitle = createSvgEl('text', {
        x: rail.startX,
        y: stacked ? panelY + panelHeight + 36 : padding.top + 52,
        class: 'chart-snapshot-rail-title'
    });
    railTitle.textContent = target !== null ? 'Stan wzgledem celu' : 'Biezaca skala temperatury';
    svg.appendChild(railTitle);

    svg.appendChild(createSvgEl('line', {
        x1: rail.startX,
        y1: rail.y,
        x2: rail.endX,
        y2: rail.y,
        class: 'chart-snapshot-rail-track'
    }));

    const tickSteps = layout.gridSteps || 4;
    const guideHalf = compact ? 14 : 18;
    for (let step = 0; step <= tickSteps; step += 1) {
        const ratio = step / tickSteps;
        const x = rail.startX + ratio * (rail.endX - rail.startX);
        const sampleValue = domain.minValue + ratio * (domain.maxValue - domain.minValue);
        svg.appendChild(createSvgEl('line', {
            x1: x,
            y1: rail.y - guideHalf,
            x2: x,
            y2: rail.y + guideHalf,
            class: 'chart-snapshot-guide'
        }));

        if (step === 0 || step === Math.floor(tickSteps / 2) || step === tickSteps) {
            const label = createSvgEl('text', {
                x,
                y: rail.y + (stacked ? 40 : 46),
                class: 'chart-snapshot-scale-label'
            });
            label.setAttribute('text-anchor', step === 0 ? 'start' : (step === tickSteps ? 'end' : 'middle'));
            label.textContent = formatChartTemperature(sampleValue);
            svg.appendChild(label);
        }
    }

    if (target !== null && hysteresis > 0) {
        const lowerX = valueToX(target - hysteresis);
        const upperX = valueToX(target + hysteresis);
        svg.appendChild(createSvgEl('line', {
            x1: lowerX,
            y1: rail.y,
            x2: upperX,
            y2: rail.y,
            class: 'chart-snapshot-band'
        }));
        [lowerX, upperX].forEach((x) => {
            svg.appendChild(createSvgEl('circle', {
                cx: x,
                cy: rail.y,
                r: 4,
                class: 'chart-snapshot-band-cap'
            }));
        });
    }

    if (target !== null) {
        const targetX = valueToX(target);
        svg.appendChild(createSvgEl('line', {
            x1: targetX,
            y1: rail.y - (stacked ? 26 : 34),
            x2: targetX,
            y2: rail.y + (stacked ? 24 : 30),
            class: 'chart-snapshot-target'
        }));

        const targetLabel = createSvgEl('text', {
            x: targetX,
            y: rail.y + (stacked ? 64 : 76),
            class: 'chart-snapshot-target-label'
        });
        targetLabel.setAttribute('text-anchor', 'middle');
        targetLabel.textContent = `Cel ${formatChartTemperature(target)}`;
        svg.appendChild(targetLabel);
    }

    svg.appendChild(createSvgEl('line', {
        x1: rail.startX,
        y1: rail.y,
        x2: currentX,
        y2: rail.y,
        class: 'chart-snapshot-fill'
    }));
    svg.appendChild(createSvgEl('line', {
        x1: currentX,
        y1: rail.y - (stacked ? 40 : 48),
        x2: currentX,
        y2: rail.y + (stacked ? 20 : 26),
        class: 'chart-snapshot-stem'
    }));
    svg.appendChild(createSvgEl('circle', {
        cx: currentX,
        cy: rail.y,
        r: compact ? 14 : 17,
        class: 'chart-point-current'
    }));
    svg.appendChild(createSvgEl('circle', {
        cx: currentX,
        cy: rail.y,
        r: compact ? 6 : 7,
        class: 'chart-point-core'
    }));

    appendChartChip(svg, {
        x: currentX - (stacked ? 52 : 58),
        y: rail.y - (stacked ? 66 : 88),
        text: `Teraz ${latest.valueLabel}`,
        tone: 'live',
        bounds: chipBounds
    });

    const currentHit = createSvgEl('circle', {
        cx: currentX,
        cy: rail.y,
        r: compact ? 14 : 16,
        tabindex: 0,
        class: 'chart-point-hit'
    });
    currentHit.addEventListener('mouseenter', (event) => showChartTooltip(event.clientX, event.clientY, latest));
    currentHit.addEventListener('mousemove', (event) => showChartTooltip(event.clientX, event.clientY, latest));
    currentHit.addEventListener('mouseleave', hideChartTooltip);
    currentHit.addEventListener('focus', () => {
        const rect = svg.getBoundingClientRect();
        showChartTooltip(rect.left + currentX, rect.top + rail.y, latest);
    });
    currentHit.addEventListener('blur', hideChartTooltip);
    svg.appendChild(currentHit);
}

function renderTemperatureTrendChart(svg, points, target, hysteresis, domain, layout) {
    const width = layout.width;
    const height = layout.height;
    const padding = layout.padding;
    const plotWidth = width - padding.left - padding.right;
    const plotHeight = height - padding.top - padding.bottom;
    const minValue = domain.minValue;
    const maxValue = domain.maxValue;
    const guidesVisible = domain.guidesVisible;
    const compact = !!layout.compact;
    const xFor = (index) => padding.left + (points.length === 1 ? plotWidth / 2 : (index / (points.length - 1)) * plotWidth);
    const yFor = (value) => padding.top + ((maxValue - value) / (maxValue - minValue)) * plotHeight;
    const chipBounds = {
        minX: padding.left + 8,
        maxX: padding.left + plotWidth - 8,
        minY: padding.top + 8,
        maxY: padding.top + plotHeight - 8
    };
    const targetVisible = target !== null && target >= minValue && target <= maxValue;

    appendTemperatureChartDefs(svg);
    appendTemperatureChartFrame(svg, padding, plotWidth, plotHeight);

    if (target !== null && guidesVisible && hysteresis > 0) {
        const upperY = yFor(target + hysteresis);
        const lowerY = yFor(target - hysteresis);
        const bandTop = Math.min(upperY, lowerY);
        const bandBottom = Math.max(upperY, lowerY);

        svg.appendChild(createSvgEl('rect', {
            x: padding.left,
            y: bandTop,
            width: plotWidth,
            height: Math.max(2, bandBottom - bandTop),
            rx: 14,
            class: 'chart-band'
        }));
    }

    const gridSteps = layout.gridSteps || 4;
    for (let index = 0; index <= gridSteps; index += 1) {
        const ratio = index / gridSteps;
        const y = padding.top + ratio * plotHeight;
        const value = maxValue - ratio * (maxValue - minValue);
        svg.appendChild(createSvgEl('line', {
            x1: padding.left,
            y1: y,
            x2: padding.left + plotWidth,
            y2: y,
            class: 'chart-grid-line'
        }));

        const label = createSvgEl('text', {
            x: Math.max(6, padding.left - (compact ? 34 : 52)),
            y: y + 4,
            class: 'chart-grid-label'
        });
        label.textContent = formatChartTemperature(value);
        svg.appendChild(label);
    }

    if (targetVisible) {
        const targetY = yFor(target);
        svg.appendChild(createSvgEl('line', {
            x1: padding.left,
            y1: targetY,
            x2: padding.left + plotWidth,
            y2: targetY,
            class: 'chart-target-line'
        }));
        appendChartChip(svg, {
            x: padding.left + plotWidth - 12,
            y: targetY - 30,
            text: `Cel ${formatChartTemperature(target)}`,
            tone: 'target',
            anchor: 'end',
            bounds: chipBounds
        });
    }

    const coords = points.map((point, index) => ({
        x: xFor(index),
        y: yFor(point.value)
    }));
    const pathData = buildSmoothChartPath(coords);
    const firstCoord = coords[0];
    const lastCoord = coords[coords.length - 1];
    const latestPoint = points[points.length - 1];
    const areaData = `${pathData} L ${lastCoord.x.toFixed(2)} ${(padding.top + plotHeight).toFixed(2)} L ${firstCoord.x.toFixed(2)} ${(padding.top + plotHeight).toFixed(2)} Z`;

    svg.appendChild(createSvgEl('path', { d: areaData, class: 'chart-area' }));
    svg.appendChild(createSvgEl('path', { d: pathData, class: 'chart-line-glow' }));
    svg.appendChild(createSvgEl('path', { d: pathData, class: 'chart-line' }));

    if (coords.length > 1) {
        svg.appendChild(createSvgEl('line', {
            x1: lastCoord.x,
            y1: padding.top + 8,
            x2: lastCoord.x,
            y2: padding.top + plotHeight - 8,
            class: 'chart-focus-line'
        }));
    }

    const latestChipAnchor = lastCoord.x > padding.left + plotWidth * 0.72 ? 'end' : 'start';
    const latestChipY = lastCoord.y < padding.top + (compact ? 50 : 42)
        ? lastCoord.y + (compact ? 16 : 12)
        : lastCoord.y - (compact ? 30 : 34);
    appendChartChip(svg, {
        x: latestChipAnchor === 'end' ? lastCoord.x - 14 : lastCoord.x + 14,
        y: latestChipY,
        text: `Teraz ${latestPoint.valueLabel}`,
        tone: 'live',
        anchor: latestChipAnchor,
        bounds: chipBounds
    });

    svg.appendChild(createSvgEl('circle', {
        cx: lastCoord.x,
        cy: lastCoord.y,
        r: compact ? 12 : 14,
        class: 'chart-point-current'
    }));
    svg.appendChild(createSvgEl('circle', {
        cx: lastCoord.x,
        cy: lastCoord.y,
        r: compact ? 6 : 7,
        class: 'chart-point-core'
    }));

    points.forEach((point, index) => {
        const { x, y } = coords[index];
        const isLatest = index === points.length - 1;

        const circle = createSvgEl('circle', {
            cx: x,
            cy: y,
            r: isLatest ? (compact ? 4.5 : 5) : (compact ? 3.5 : 4),
            class: isLatest ? 'chart-point chart-point-latest' : 'chart-point'
        });
        const title = createSvgEl('title');
        title.textContent = `${point.valueLabel} - ${point.timeLabel}`;
        circle.appendChild(title);
        svg.appendChild(circle);

        const hit = createSvgEl('circle', {
            cx: x,
            cy: y,
            r: compact ? 11 : 13,
            tabindex: 0,
            class: 'chart-point-hit'
        });
        hit.addEventListener('mouseenter', (event) => showChartTooltip(event.clientX, event.clientY, point));
        hit.addEventListener('mousemove', (event) => showChartTooltip(event.clientX, event.clientY, point));
        hit.addEventListener('mouseleave', hideChartTooltip);
        hit.addEventListener('focus', () => {
            const rect = svg.getBoundingClientRect();
            showChartTooltip(rect.left + x, rect.top + y, point);
        });
        hit.addEventListener('blur', hideChartTooltip);
        svg.appendChild(hit);
    });
}

function renderTemperatureChartSummary(summary, points, target, hysteresis, snapshotMode) {
    if (!summary) return;

    const latest = points[points.length - 1];
    const relation = describeTargetRelationship(latest.value, target, hysteresis);
    const badges = [`<span class="temp-chart-pill temp-chart-pill-live">Ostatni <strong>${escapeHtml(latest.valueLabel)}</strong></span>`];

    if (relation) {
        badges.push(`<span class="temp-chart-pill temp-chart-pill-${relation.tone}">${escapeHtml(relation.label)}</span>`);
    }

    if (target !== null) {
        badges.push(`<span class="temp-chart-pill temp-chart-pill-target">Cel <strong>${escapeHtml(formatChartTemperature(target))}</strong></span>`);
    }

    if (target !== null && hysteresis > 0) {
        const lower = formatChartTemperature(target - hysteresis);
        const upper = formatChartTemperature(target + hysteresis);
        badges.push(`<span class="temp-chart-pill temp-chart-pill-band">Pasmo <strong>${escapeHtml(`${lower} - ${upper}`)}</strong></span>`);
    }

    if (!snapshotMode) {
        const trend = describeTemperatureTrend(points);
        const minPoint = points.reduce((lowest, point) => (point.value < lowest.value ? point : lowest), points[0]);
        const maxPoint = points.reduce((highest, point) => (point.value > highest.value ? point : highest), points[0]);
        badges.push(`<span class="temp-chart-pill temp-chart-pill-${trend.tone}">${escapeHtml(trend.label)}</span>`);
        badges.push(`<span class="temp-chart-pill temp-chart-pill-neutral">Zakres <strong>${escapeHtml(`${minPoint.valueLabel} - ${maxPoint.valueLabel}`)}</strong></span>`);
    }

    summary.innerHTML = badges.join('');
}

function renderTemperatureChart(temperature) {
    const svg = document.getElementById('temperature-chart-svg');
    const empty = document.getElementById('temperature-chart-empty');
    const summary = document.getElementById('temperature-chart-summary');
    const note = document.getElementById('temperature-chart-note');
    const shell = document.querySelector('.temp-chart-shell');
    const axis = shell ? shell.querySelector('.temp-chart-axis') : null;
    if (!svg || !empty) return;

    ensureTemperatureChartResizeObserver(shell);

    const rawHistory = Array.isArray(temperature?.history) ? temperature.history : [];
    const historyCapacity = Math.max(1, Math.round(toFiniteNumber(temperature?.historyCapacity) ?? 20));
    const historyIntervalMinutes = Math.max(1, Math.round(toFiniteNumber(temperature?.historyIntervalMinutes) ?? 10));
    const dashboardHistoryLimit = Math.min(historyCapacity, 36);
    const snapshotThreshold = 4;
    const points = rawHistory
        .map((item, index) => ({
            value: toFiniteNumber(item?.value),
            epoch: toFiniteNumber(item?.epoch),
            index
        }))
        .filter((item) => isValidTemperature(item.value))
        .slice(-dashboardHistoryLimit)
        .map((item, index, arr) => ({
            value: item.value,
            epoch: item.epoch,
            valueLabel: formatChartTemperature(item.value, 2),
            timeLabel: formatEpoch(item.epoch, {
                fallback: `Pomiar ${index + 1} z ${arr.length}`,
                includeDate: false,
                includeSeconds: false
            })
        }));

    svg.innerHTML = '';
    if (summary) summary.innerHTML = '';
    if (note) {
        note.hidden = true;
        note.textContent = '';
    }
    hideChartTooltip();
    const visibleHours = Math.max(
        1,
        Math.round(((Math.max(points.length, dashboardHistoryLimit) - 1) * historyIntervalMinutes) / 60)
    );
    setText(
        'temperature-chart-meta',
        `${points.length || 0} z ${historyCapacity} pomiarow | ostatnie ~${visibleHours} h`
    );

    const emptyLayout = getTemperatureChartLayout(shell ? shell.clientWidth : 0, false);
    applyTemperatureChartLayout(svg, shell, emptyLayout, false);

    if (points.length === 0) {
        empty.hidden = false;
        if (axis) axis.hidden = false;
        setText('temperature-chart-start', 'Najstarszy pomiar');
        setText('temperature-chart-end', 'Teraz');
        return;
    }

    empty.hidden = true;

    const target = toFiniteNumber(temperature?.target);
    const hysteresis = Math.abs(toFiniteNumber(temperature?.hysteresis) ?? 0);
    const snapshotMode = points.length < snapshotThreshold;
    const layout = getTemperatureChartLayout(shell ? shell.clientWidth : 0, snapshotMode);
    const domain = buildTemperatureDomain(points, target, hysteresis, snapshotMode);

    applyTemperatureChartLayout(svg, shell, layout, snapshotMode);
    if (axis) {
        axis.hidden = snapshotMode;
    }

    renderTemperatureChartSummary(summary, points, target, hysteresis, snapshotMode);

    if (snapshotMode) {
        if (note) {
            note.hidden = false;
            note.textContent = `Tryb podgladu: pelny trend wlaczy sie po ${snapshotThreshold} probkach. Kolejny odczyt za okolo ${historyIntervalMinutes} min.`;
        }
        renderTemperatureSnapshot(svg, points, target, hysteresis, domain, layout);
        return;
    }

    setText('temperature-chart-start', points[0].timeLabel);
    setText('temperature-chart-end', points[points.length - 1].timeLabel);
    renderTemperatureTrendChart(svg, points, target, hysteresis, domain, layout);
}

function renderDashboard(data) {
    lastStatusData = data;
    renderTopbarActiveModules(data);
    renderTemperatureCard(data.temperature || {});
    renderBatteryWidgets(data.battery || {});
    renderFirmwareInfo(data.firmware || {});
    renderNetworkCard(data.network || {});
    renderSettingsNetworkPanel(data.network || {});
    renderSettingsClockPanel(data.clock || {});
    renderTemperatureSettingsPanel(data);
    renderDiagnosticsPanel(data);
    renderScheduleEditor(data);
    renderRelays(data);
    renderTodaySchedule(data);
    renderFeederCard(data);
    renderTemperatureChart(data.temperature || {});
}

async function triggerFeed() {
    if (feedActionState.awaitingResponse || feedActionState.awaitingCompletion || lastStatusData?.feeding?.active) {
        return;
    }

    feedActionState.awaitingResponse = true;
    feedActionState.awaitingCompletion = false;
    feedActionState.sawActive = false;
    feedActionState.startedAtMs = Date.now();
    feedActionState.baselineLastFeedEpoch = Math.max(0, Math.trunc(Number(lastStatusData?.feeding?.lastFeedEpoch) || 0));
    feedActionState.baselineLastResult = normalizeFeedResultCode(lastStatusData?.feeding?.lastResult);

    showFeedModalState('progress', 'Uruchamianie karmienia...', 'Wysylam polecenie do sterownika.');

    try {
        await sendAction('feed_now', {}, { showSaveAnimation: false });
        feedActionState.awaitingResponse = false;
        feedActionState.awaitingCompletion = true;
        showFeedModalState('progress', 'Trwa karmienie...', 'Sterownik przyjal polecenie. Czekam na potwierdzenie.');
        await fetchStatus(true);
    } catch (error) {
        const result = describeFeedResult(error?.code, describeRequestError(error));
        showFeedModalState(result.kind, result.title, result.message, 2600);
        resetFeedActionState();
        await fetchStatus(true);
    }
}
