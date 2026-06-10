/* ═══════════════════════════════════════════
   AquaSync — Temperature Charts App v2
   + Draw-in, sparklines, bubbles, heatmap,
     day comparison, forecast, swipe, pull,
     fullscreen
   ═══════════════════════════════════════════ */

(() => {
    'use strict';

    // ── Configuration ──
    const CONFIG = {
        target: 25.0,
        hysteresis: 0.5,
        historyInterval: 10 * 60 * 1000,
        maxPoints: 144 * 7, // 7 days
        simulationInterval: 3000,
        forecastPoints: 6, // 1 hour ahead
        ranges: { '1h': 6, '3h': 18, '6h': 36, '12h': 72, '24h': 144 },
        rangeKeys: ['1h', '3h', '6h', '12h', '24h']
    };

    // ── State ──
    let allData = [];
    let activeRange = '3h';
    let hoverIndex = -1;
    let animFrameId = null;
    let simulationTimer = null;
    let drawProgress = 0;
    let drawAnimStarted = false;
    let isFullscreen = false;
    let fullscreenHoverIndex = -1;
    let lastHistoryRef = null;

    // ── DOM Cache ──
    const dom = {};
    function cacheDom() {
        const dummyNode = document.createElement('div');
        const dummyCanvas = document.createElement('canvas');
        function s(id, isCanvas) { return document.getElementById(id) || (isCanvas ? dummyCanvas : dummyNode); }

        dom.mainCanvas = s('main-chart-canvas', true);
        dom.mainContainer = s('main-chart-container', false);
        dom.tooltip = s('chart-tooltip', false);
        dom.timeAxis = s('chart-time-axis', false);
        dom.rangeSelector = s('range-selector', false);
        dom.chartSubtitle = s('chart-subtitle', false);
        dom.summaryCurrent = s('summary-current', false);
        dom.summaryTarget = s('summary-target', false);
        dom.summaryBand = s('summary-band', false);
        dom.summaryMin = s('summary-min', false);
        dom.summaryMax = s('summary-max', false);
        dom.summaryTrend = s('summary-trend', false);
        dom.clockTime = s('clock-time', false);
        dom.clockDate = s('clock-date', false);
        dom.footerCount = s('footer-count', false);
        dom.historyTbody = s('history-tbody', false);
        dom.btnExport = s('btn-export', false);
        dom.miniVarCanvas = s('mini-var-canvas', true);
        dom.miniDevCanvas = s('mini-dev-canvas', true);
        dom.miniRateCanvas = s('mini-rate-canvas', true);
        dom.miniVarValue = s('mini-var-value', false);
        dom.miniDevValue = s('mini-dev-value', false);
        dom.miniRateValue = s('mini-rate-value', false);
        dom.bubblesCanvas = s('bubbles-canvas', true);
        dom.heatmapCanvas = s('heatmap-canvas', true);
        dom.comparisonCanvas = s('comparison-canvas', true);
        dom.comparisonLegend = s('comparison-legend', false);
        dom.fullscreenOverlay = s('fullscreen-overlay', false);
        dom.fullscreenCanvas = s('fullscreen-chart-canvas', true);
        dom.fullscreenContainer = s('fullscreen-chart-container', false);
        dom.fullscreenTooltip = s('fullscreen-tooltip', false);
        dom.fullscreenTimeAxis = s('fullscreen-time-axis', false);
        dom.fullscreenRangeSelector = s('fullscreen-range-selector', false);
        dom.fullscreenBtn = s('fullscreen-btn', false);
        dom.fullscreenCloseBtn = s('fullscreen-close-btn', false);
        dom.pullIndicator = s('pull-indicator', false);
        dom.appShell = s('wykresy', false);
        dom.swipeHint = s('swipe-hint', false);
    
}

    // ═══════════════════
    // Data Integration
    // ═══════════════════
    function applyTemperatureConfig(temperatureData) {
        let changed = false;

        const nextTarget = Number(temperatureData?.target);
        if (Number.isFinite(nextTarget) && nextTarget !== CONFIG.target) {
            CONFIG.target = nextTarget;
            changed = true;
        }

        const nextHysteresis = Math.abs(Number(temperatureData?.hysteresis));
        if (Number.isFinite(nextHysteresis) && nextHysteresis !== CONFIG.hysteresis) {
            CONFIG.hysteresis = nextHysteresis;
            changed = true;
        }

        const nextIntervalMinutes = Math.round(Number(temperatureData?.historyIntervalMinutes));
        if (Number.isFinite(nextIntervalMinutes) && nextIntervalMinutes > 0) {
            const nextInterval = nextIntervalMinutes * 60 * 1000;
            if (nextInterval !== CONFIG.historyInterval) {
                CONFIG.historyInterval = nextInterval;
                changed = true;
            }
        }

        const nextCapacity = Math.round(Number(temperatureData?.historyCapacity));
        if (Number.isFinite(nextCapacity) && nextCapacity > 0 && nextCapacity !== CONFIG.maxPoints) {
            CONFIG.maxPoints = nextCapacity;
            changed = true;
        }

        return changed;
    }

    function normalizeActiveRange() {
        if (!allData.length) return;

        const currentRequired = CONFIG.ranges[activeRange] || 0;
        if (currentRequired > 0 && allData.length >= currentRequired) {
            return;
        }

        let fallback = CONFIG.rangeKeys[0];
        for (const key of CONFIG.rangeKeys) {
            if (allData.length >= (CONFIG.ranges[key] || 0)) {
                fallback = key;
            }
        }
        activeRange = fallback;
    }

    function syncRangeControls() {
        if (dom.chartSubtitle) {
            dom.chartSubtitle.textContent = rangeLabels[activeRange] || '';
        }

        document.querySelectorAll('.range-btn').forEach((button) => {
            const range = button.dataset.range;
            const requiredPoints = CONFIG.ranges[range] || 0;
            const disabled = allData.length > 0 && allData.length < requiredPoints && range !== activeRange;
            button.disabled = disabled;
            button.classList.toggle('active', range === activeRange);
        });
    }

    window.ChartsApp = {
        updateData: function(temperatureData) {
            if (!temperatureData || typeof temperatureData !== 'object') {
                return;
            }

            const configChanged = applyTemperatureConfig(temperatureData);
            const history = Array.isArray(temperatureData.history) ? temperatureData.history : null;
            let historyChanged = false;

            if (history && history !== lastHistoryRef) {
                lastHistoryRef = history;
                allData = [];

                for (const item of history) {
                    if (item && item.value !== undefined && item.value !== null) {
                        allData.push({ epoch: item.epoch * 1000, value: item.value });
                    }
                }

                normalizeActiveRange();
                syncRangeControls();
                historyChanged = true;
            } else if (configChanged) {
                syncRangeControls();
            }

            const sect = document.getElementById('wykresy');
            if (sect && sect.classList.contains('active') && (historyChanged || configChanged)) {
                requestRender();
                updateDayComparison();
                if (historyChanged) {
                    updateHeatmap();
                }
            }
        }
    };

    function generateDemoData() {
        return [];
    }

    function getVisibleData() {
        const maxPoints = CONFIG.ranges[activeRange] || 18;
        return allData.slice(-maxPoints);
    }

    // ═══════════════════
    // Forecast (Linear Extrapolation + Mean Reversion)
    // ═══════════════════
    function computeForecast(visibleData) {
        if (visibleData.length < 4) return [];
        const window = visibleData.slice(-6);
        const n = window.length;
        // Linear regression over last window
        let sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
        for (let i = 0; i < n; i++) {
            sumX += i; sumY += window[i].value;
            sumXY += i * window[i].value;
            sumX2 += i * i;
        }
        const slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
        const intercept = (sumY - slope * sumX) / n;
        const lastEpoch = window[n - 1].epoch;
        const interval = CONFIG.historyInterval;

        const forecast = [];
        for (let i = 1; i <= CONFIG.forecastPoints; i++) {
            const rawVal = intercept + slope * (n - 1 + i);
            // Mean reversion toward target
            const reversion = (CONFIG.target - rawVal) * 0.03 * i;
            const val = ChartEngine.clamp(rawVal + reversion, CONFIG.target - 3, CONFIG.target + 3);
            forecast.push({
                epoch: lastEpoch + i * interval,
                value: Math.round(val * 100) / 100
            });
        }
        return forecast;
    }

    // ═══════════════════
    // Clock
    // ═══════════════════
    function updateClock() {
        if (!dom.clockTime || !dom.clockDate) return;
        const now = new Date();
        dom.clockTime.textContent = `${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}:${String(now.getSeconds()).padStart(2, '0')}`;
        const months = ['sty', 'lut', 'mar', 'kwi', 'maj', 'cze', 'lip', 'sie', 'wrz', 'paź', 'lis', 'gru'];
        dom.clockDate.textContent = `${String(now.getDate()).padStart(2, '0')} ${months[now.getMonth()]} ${now.getFullYear()}`;
    }

    // ═══════════════════
    // Summary
    // ═══════════════════
    function updateSummary(visibleData) {
        if (!visibleData.length) return;
        const latest = visibleData[visibleData.length - 1];
        dom.summaryCurrent.textContent = ChartEngine.formatTemp(latest.value, 2);
        dom.summaryTarget.textContent = ChartEngine.formatTemp(CONFIG.target);
        dom.summaryBand.textContent = `±${CONFIG.hysteresis.toFixed(1)}°C`;

        let minV = Infinity, maxV = -Infinity;
        for (const p of visibleData) { if (p.value < minV) minV = p.value; if (p.value > maxV) maxV = p.value; }
        dom.summaryMin.textContent = ChartEngine.formatTemp(minV, 2);
        dom.summaryMax.textContent = ChartEngine.formatTemp(maxV, 2);

        const trendEl = dom.summaryTrend;
        if (visibleData.length >= 3) {
            const win = visibleData.slice(-Math.min(6, visibleData.length));
            const delta = win[win.length - 1].value - win[0].value;
            trendEl.classList.remove('rising', 'falling', 'stable');
            if (Math.abs(delta) < 0.08) {
                trendEl.classList.add('stable');
                trendEl.querySelector('svg').innerHTML = '<path d="M2 8h12" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>';
                trendEl.querySelector('span').textContent = 'Stabilna';
            } else if (delta > 0) {
                trendEl.classList.add('rising');
                trendEl.querySelector('svg').innerHTML = '<path d="M8 13V3M8 3l-3 3M8 3l3 3" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>';
                trendEl.querySelector('span').textContent = `+${delta.toFixed(2)}°C`;
            } else {
                trendEl.classList.add('falling');
                trendEl.querySelector('svg').innerHTML = '<path d="M8 3v10M8 13l-3-3M8 13l3-3" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>';
                trendEl.querySelector('span').textContent = `${delta.toFixed(2)}°C`;
            }
        }
        dom.footerCount.textContent = `${allData.length} pomiarów`;
    }

    // ═══════════════════
    // Time Axis
    // ═══════════════════
    function updateTimeAxis(visibleData, axisEl) {
        const el = axisEl || dom.timeAxis;
        if (!visibleData.length) { el.innerHTML = ''; return; }
        const isMobile = window.innerWidth < 600;
        const labelCount = isMobile ? 4 : 7;
        const step = Math.max(1, Math.floor(visibleData.length / (labelCount - 1)));
        let labels = '';
        for (let i = 0; i < visibleData.length; i += step) {
            labels += `<span>${ChartEngine.formatTime(visibleData[i].epoch)}</span>`;
        }
        const lastTime = ChartEngine.formatTime(visibleData[visibleData.length - 1].epoch);
        if (!labels.endsWith(`>${lastTime}</span>`)) labels += `<span>${lastTime}</span>`;
        el.innerHTML = labels;
    }

    // ═══════════════════
    // Mini Charts
    // ═══════════════════
    function updateMiniCharts(visibleData) {
        if (visibleData.length < 2) return;
        const varWindow = visibleData.slice(-Math.min(12, visibleData.length));
        const varValues = [];
        for (let i = 1; i < varWindow.length; i++) varValues.push(Math.abs(varWindow[i].value - varWindow[i - 1].value));
        const avgVar = varValues.reduce((a, b) => a + b, 0) / varValues.length;
        dom.miniVarValue.textContent = `${avgVar.toFixed(3)}°C`;
        ChartEngine.renderMiniChart(dom.miniVarCanvas, varValues, { color: '#a78bfa', glowColor: 'rgba(167, 139, 250, 0.3)', areaColor: 'rgba(167, 139, 250, 0.08)' });

        const devValues = visibleData.slice(-24).map(p => p.value - CONFIG.target);
        const lastDev = devValues[devValues.length - 1];
        dom.miniDevValue.textContent = `${lastDev >= 0 ? '+' : ''}${lastDev.toFixed(2)}°C`;
        const devOk = Math.abs(lastDev) <= CONFIG.hysteresis;
        dom.miniDevValue.style.color = devOk ? '#34d399' : '#fbbf24';
        ChartEngine.renderMiniChart(dom.miniDevCanvas, devValues, {
            color: devOk ? '#34d399' : '#fbbf24',
            glowColor: devOk ? 'rgba(52, 211, 153, 0.3)' : 'rgba(251, 191, 36, 0.3)',
            areaColor: devOk ? 'rgba(52, 211, 153, 0.08)' : 'rgba(251, 191, 36, 0.08)',
            baseline: 0
        });

        const rateValues = [];
        for (let i = 1; i < visibleData.length; i++) {
            const dt = (visibleData[i].epoch - visibleData[i - 1].epoch) / 3600000;
            const dv = visibleData[i].value - visibleData[i - 1].value;
            rateValues.push(dt > 0 ? dv / dt : 0);
        }
        const latestRate = rateValues[rateValues.length - 1] || 0;
        dom.miniRateValue.textContent = `${latestRate >= 0 ? '+' : ''}${latestRate.toFixed(2)}°C/h`;
        ChartEngine.renderMiniChart(dom.miniRateCanvas, rateValues.slice(-24), { color: '#22d3ee', glowColor: 'rgba(34, 211, 238, 0.3)', areaColor: 'rgba(34, 211, 238, 0.08)', baseline: 0 });
    }

    // ═══════════════════
    // History Table with Sparklines
    // ═══════════════════
    let sparklineCanvases = [];

    function updateHistoryTable(visibleData) {
        const rows = visibleData.slice().reverse().slice(0, 30);
        let html = '';

        for (let ri = 0; ri < rows.length; ri++) {
            const p = rows[ri];
            const dev = p.value - CONFIG.target;
            const absDev = Math.abs(dev);
            let statusClass, statusText;
            if (absDev <= CONFIG.hysteresis * 0.5) { statusClass = 'ok'; statusText = 'Norma'; }
            else if (absDev <= CONFIG.hysteresis) { statusClass = 'warn'; statusText = 'Pasmo'; }
            else { statusClass = 'danger'; statusText = 'Poza normą'; }

            html += `<tr>
                <td>${ChartEngine.formatTimeFull(p.epoch)}</td>
                <td style="color: var(--accent-cyan); font-weight: 600;">${ChartEngine.formatTemp(p.value, 2)}</td>
                <td><canvas class="sparkline-cell" data-spark-idx="${ri}" width="80" height="24"></canvas></td>
                <td>${dev >= 0 ? '+' : ''}${dev.toFixed(2)}°C</td>
                <td><span class="status-pill ${statusClass}"><span class="status-pip ${statusClass}"></span>${statusText}</span></td>
            </tr>`;
        }
        dom.historyTbody.innerHTML = html;

        // Render sparklines
        requestAnimationFrame(() => {
            const canvases = dom.historyTbody.querySelectorAll('.sparkline-cell');
            canvases.forEach(c => {
                const idx = parseInt(c.dataset.sparkIdx);
                // Get surrounding data points for sparkline context
                const rowPoint = rows[idx];
                const dataIdx = visibleData.indexOf(rowPoint);
                if (dataIdx < 0) return;
                const start = Math.max(0, dataIdx - 4);
                const end = Math.min(visibleData.length, dataIdx + 5);
                const sparkData = visibleData.slice(start, end).map(p => p.value);
                const dev = rowPoint.value - CONFIG.target;
                const color = Math.abs(dev) <= CONFIG.hysteresis * 0.5 ? '#34d399' : (Math.abs(dev) <= CONFIG.hysteresis ? '#fbbf24' : '#fb7185');
                ChartEngine.renderSparkline(c, sparkData, { color, baseline: CONFIG.target });
            });
        });
    }

    // ═══════════════════
    // Heatmap (Premium)
    // ═══════════════════
    let heatmapResult = null;
    let heatmapHoverCell = null;
    let heatmapData = null;
    let heatmapOpts = null;
    let heatmapWaveProgress = 0;
    let heatmapWaveAnimId = null;

    function updateHeatmap(animate = false) {
        const now = Date.now();
        const msPerDay = 24 * 60 * 60 * 1000;
        const days = 7;
        const heatData = [];
        const dayLabels = [];
        const dayNames = ['Nd', 'Pn', 'Wt', 'Śr', 'Cz', 'Pt', 'So'];

        for (let d = days - 1; d >= 0; d--) {
            const dayStart = now - d * msPerDay;
            const date = new Date(dayStart);
            dayLabels.push(`${dayNames[date.getDay()]} ${String(date.getDate()).padStart(2, '0')}`);

            const hourlyAvg = new Array(24).fill(null);
            const hourlyCount = new Array(24).fill(0);

            for (const p of allData) {
                const pDate = new Date(p.epoch);
                if (Math.abs(p.epoch - dayStart) < msPerDay &&
                    pDate.getDate() === date.getDate()) {
                    const hr = pDate.getHours();
                    hourlyAvg[hr] = (hourlyAvg[hr] || 0) + p.value;
                    hourlyCount[hr]++;
                }
            }

            for (let h = 0; h < 24; h++) {
                if (hourlyCount[h] > 0) hourlyAvg[h] /= hourlyCount[h];
            }
            heatData.push(hourlyAvg);
        }

        let minT = Infinity, maxT = -Infinity;
        for (const row of heatData) {
            for (const v of row) {
                if (v !== null) {
                    if (v < minT) minT = v;
                    if (v > maxT) maxT = v;
                }
            }
        }
        if (!isFinite(minT)) { minT = 23; maxT = 27; }

        heatmapData = heatData;
        heatmapOpts = { minTemp: minT, maxTemp: maxT, dayLabels };

        if (animate) {
            startHeatmapWave();
        } else {
            heatmapWaveProgress = 1;
            renderHeatmapNow();
        }
    }

    function renderHeatmapNow() {
        if (!heatmapData || !heatmapOpts) return;
        heatmapResult = ChartEngine.renderHeatmap(dom.heatmapCanvas, heatmapData, {
            ...heatmapOpts,
            hoverCell: heatmapHoverCell,
            waveProgress: heatmapWaveProgress,
            currentHour: new Date().getHours()
        });
    }

    function startHeatmapWave() {
        heatmapWaveProgress = 0;
        const startTime = performance.now();
        const WAVE_DURATION = 1500;

        function step(now) {
            const elapsed = now - startTime;
            heatmapWaveProgress = ChartEngine.clamp(elapsed / WAVE_DURATION, 0, 1);
            // Ease out
            heatmapWaveProgress = 1 - Math.pow(1 - heatmapWaveProgress, 2.5);
            renderHeatmapNow();
            if (heatmapWaveProgress < 1) {
                heatmapWaveAnimId = requestAnimationFrame(step);
            } else {
                heatmapWaveProgress = 1;
                heatmapWaveAnimId = null;
            }
        }
        if (heatmapWaveAnimId) cancelAnimationFrame(heatmapWaveAnimId);
        heatmapWaveAnimId = requestAnimationFrame(step);
    }

    function findHeatmapCell(clientX, clientY) {
        if (!heatmapResult || !heatmapResult.cells) return null;
        const rect = dom.heatmapCanvas.getBoundingClientRect();
        const mx = clientX - rect.left;
        const my = clientY - rect.top;
        for (const cell of heatmapResult.cells) {
            if (mx >= cell.x && mx <= cell.x + cell.w && my >= cell.y && my <= cell.y + cell.h) {
                return cell;
            }
        }
        return null;
    }

    function initHeatmapInteraction() {
        const container = document.getElementById('heatmap-container');
        const tooltip = document.getElementById('heatmap-tooltip');

        container.addEventListener('mousemove', (e) => {
            const cell = findHeatmapCell(e.clientX, e.clientY);
            if (cell && cell.val !== null && cell.val !== undefined) {
                const newHover = { row: cell.row, col: cell.col };
                if (!heatmapHoverCell || heatmapHoverCell.row !== newHover.row || heatmapHoverCell.col !== newHover.col) {
                    heatmapHoverCell = newHover;
                    renderHeatmapNow();
                }
                // Tooltip
                const dayLabel = heatmapOpts.dayLabels[cell.row] || '';
                const hourLabel = `${String(cell.col).padStart(2, '0')}:00`;
                tooltip.querySelector('.hm-tip-day').textContent = `${dayLabel}, ${hourLabel}`;
                tooltip.querySelector('.hm-tip-val').textContent = ChartEngine.formatTemp(cell.val, 1);
                tooltip.hidden = false;
                const rect = container.getBoundingClientRect();
                let left = cell.x + cell.w / 2;
                let top = cell.y;
                left = ChartEngine.clamp(left, tooltip.offsetWidth / 2 + 4, rect.width - tooltip.offsetWidth / 2 - 4);
                tooltip.style.left = `${left}px`;
                tooltip.style.top = `${top}px`;
            } else {
                if (heatmapHoverCell) { heatmapHoverCell = null; renderHeatmapNow(); }
                tooltip.hidden = true;
            }
        });

        container.addEventListener('mouseleave', () => {
            if (heatmapHoverCell) { heatmapHoverCell = null; renderHeatmapNow(); }
            tooltip.hidden = true;
        });

        // Touch support
        container.addEventListener('touchmove', (e) => {
            const t = e.touches[0];
            const cell = findHeatmapCell(t.clientX, t.clientY);
            if (cell && cell.val !== null) {
                heatmapHoverCell = { row: cell.row, col: cell.col };
                renderHeatmapNow();
            }
        }, { passive: true });

        container.addEventListener('touchend', () => {
            heatmapHoverCell = null;
            renderHeatmapNow();
            tooltip.hidden = true;
        });

        // Click → scroll to main chart and highlight approximate time
        container.addEventListener('click', (e) => {
            const cell = findHeatmapCell(e.clientX, e.clientY);
            if (!cell || cell.val === null) return;
            // Scroll to main chart smoothly
            const chartSection = document.getElementById('main-chart-section');
            chartSection.scrollIntoView({ behavior: 'smooth', block: 'center' });
            // Flash effect
            chartSection.querySelector('.chart-card').style.boxShadow = '0 0 30px rgba(34, 211, 238, 0.3), var(--shadow-card)';
            setTimeout(() => {
                chartSection.querySelector('.chart-card').style.boxShadow = '';
            }, 1200);
        });
    }

    // ═══════════════════
    // Day Comparison
    // ═══════════════════
    function updateDayComparison() {
        const now = Date.now();
        const msPerDay = 24 * 60 * 60 * 1000;
        const days = Math.min(5, Math.ceil(allData.length / 144));
        const dayDataSets = [];
        const dayLabels = [];
        const dayNames = ['Nd', 'Pn', 'Wt', 'Śr', 'Cz', 'Pt', 'So'];

        for (let d = days - 1; d >= 0; d--) {
            const dayStart = new Date(now - d * msPerDay);
            dayStart.setHours(0, 0, 0, 0);
            const dayEnd = new Date(dayStart.getTime() + msPerDay);
            const pts = allData.filter(p => p.epoch >= dayStart.getTime() && p.epoch < dayEnd.getTime());
            if (pts.length > 0) {
                dayDataSets.push(pts);
                dayLabels.push(`${dayNames[dayStart.getDay()]} ${String(dayStart.getDate()).padStart(2, '0')}`);
            }
        }

        ChartEngine.renderDayComparison(dom.comparisonCanvas, dayDataSets, {
            target: CONFIG.target,
            hysteresis: CONFIG.hysteresis,
            dayLabels
        });

        // Legend
        let legendHtml = '';
        dayLabels.forEach((label, i) => {
            const color = ChartEngine.DAY_COLORS[i % ChartEngine.DAY_COLORS.length];
            const isLast = i === dayLabels.length - 1;
            legendHtml += `<span class="comparison-legend-item${isLast ? ' active' : ''}" style="--dot-color: ${color}"><span class="comp-dot" style="background: ${color}"></span>${label}</span>`;
        });
        dom.comparisonLegend.innerHTML = legendHtml;
    }

    // ═══════════════════
    // Draw-in Animation
    // ═══════════════════
    let drawAnimId = null;
    const DRAW_DURATION = 1200; // ms

    function startDrawAnimation() {
        drawProgress = 0;
        drawAnimStarted = true;
        const startTime = performance.now();

        function animStep(now) {
            const elapsed = now - startTime;
            drawProgress = ChartEngine.clamp(elapsed / DRAW_DURATION, 0, 1);
            // Ease out cubic
            drawProgress = 1 - Math.pow(1 - drawProgress, 3);
            requestRender();
            if (drawProgress < 1) {
                drawAnimId = requestAnimationFrame(animStep);
            } else {
                drawProgress = 1;
                drawAnimId = null;
            }
        }
        drawAnimId = requestAnimationFrame(animStep);
    }

    // ═══════════════════
    // Ambient Aquarium Background
    // ═══════════════════
    let bubbles = [];
    let fishes = [];
    let bubblesAnimId = null;

    function initBubbles() {
        const canvas = dom.bubblesCanvas;
        if (!canvas) return;
        canvas.width = window.innerWidth;
        canvas.height = window.innerHeight;

        bubbles = [];
        const count = Math.min(35, Math.floor(window.innerWidth / 40));
        for (let i = 0; i < count; i++) {
            bubbles.push({
                x: Math.random() * canvas.width,
                y: canvas.height + Math.random() * canvas.height,
                r: Math.random() * 4 + 1.5,
                speed: Math.random() * 0.3 + 0.15,
                wobbleAmp: Math.random() * 20 + 5,
                wobbleSpeed: Math.random() * 0.003 + 0.002,
                wobbleOffset: Math.random() * Math.PI * 2,
                opacity: Math.random() * 0.15 + 0.1
            });
        }

        fishes = [];
        // Add 2 subtle abstract fishes
        for(let j = 0; j < 2; j++) {
            fishes.push({
                x: Math.random() * canvas.width,
                y: canvas.height * 0.2 + Math.random() * (canvas.height * 0.6),
                size: Math.random() * 8 + 12,
                vx: (Math.random() * 0.4 + 0.2) * (j % 2 === 0 ? 1 : -1),
                wobblePhase: Math.random() * Math.PI * 2,
            });
        }

        function animateBubbles() {
            const ctx = canvas.getContext('2d');
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            const now = performance.now();

            for (const b of bubbles) {
                b.y -= b.speed;
                const wx = b.x + Math.sin(now * b.wobbleSpeed + b.wobbleOffset) * b.wobbleAmp;

                if (b.y < -b.r * 2) {
                    b.y = canvas.height + b.r * 2;
                    b.x = Math.random() * canvas.width;
                }

                ctx.beginPath();
                ctx.arc(wx, b.y, b.r, 0, Math.PI * 2);
                ctx.fillStyle = `rgba(34, 211, 238, ${b.opacity})`;
                ctx.fill();

                // Tiny highlight
                ctx.beginPath();
                ctx.arc(wx - b.r * 0.3, b.y - b.r * 0.3, b.r * 0.3, 0, Math.PI * 2);
                ctx.fillStyle = `rgba(255, 255, 255, ${b.opacity * 0.6})`;
                ctx.fill();
            }

            for (const f of fishes) {
                f.x += f.vx;
                const swayY = Math.sin(now * 0.001 + f.wobblePhase) * 0.5;
                f.y += Math.sin(now * 0.0005 + f.wobblePhase) * 0.2;

                if (f.x > canvas.width + 150) { f.vx = -Math.abs(f.vx); f.y = Math.random() * canvas.height; }
                if (f.x < -150) { f.vx = Math.abs(f.vx); f.y = Math.random() * canvas.height; }

                ctx.save();
                ctx.translate(f.x, f.y + swayY);
                // Flip horizontally based on direction
                ctx.scale(f.vx > 0 ? -1 : 1, 1);
                
                const tailWag = Math.sin(now * 0.004 + f.wobblePhase) * 0.5;
                
                ctx.beginPath();
                ctx.moveTo(-f.size*1.2, 0); // nose
                ctx.quadraticCurveTo(-f.size*0.5, -f.size*0.5, 0, -f.size*0.4); // top back
                ctx.quadraticCurveTo(f.size*0.8, -f.size*0.1, f.size, 0); // top tail base
                
                // Tail fin
                ctx.quadraticCurveTo(f.size*1.6, -f.size*tailWag - f.size*0.4, f.size*1.4, tailWag * f.size * 0.5);
                ctx.quadraticCurveTo(f.size*1.6, f.size*tailWag + f.size*0.4, f.size, 0);
                
                ctx.quadraticCurveTo(f.size*0.8, f.size*0.1, 0, f.size*0.4); // bottom belly
                ctx.quadraticCurveTo(-f.size*0.5, f.size*0.5, -f.size*1.2, 0); // bottom chin
                
                const grad = ctx.createLinearGradient(-f.size, 0, f.size * 1.5, 0);
                grad.addColorStop(0, 'rgba(34, 211, 238, 0.25)'); // subtle cyan body
                grad.addColorStop(1, 'rgba(34, 211, 238, 0.0)'); // invisible tail
                ctx.fillStyle = grad;
                ctx.fill();

                // Eye
                ctx.beginPath();
                ctx.arc(-f.size*0.7, -f.size*0.1, f.size*0.12, 0, Math.PI * 2);
                ctx.fillStyle = 'rgba(255, 255, 255, 0.3)';
                ctx.fill();

                ctx.restore();
            }

            bubblesAnimId = requestAnimationFrame(animateBubbles);
        }
        animateBubbles();
    }

    // ═══════════════════
    // Swipe Gestures
    // ═══════════════════
    let swipeStartX = 0;
    let swipeStartY = 0;
    let isSwiping = false;

    function initSwipeGestures() {
        const container = dom.mainContainer;

        container.addEventListener('touchstart', (e) => {
            if (e.touches.length !== 1) return;
            swipeStartX = e.touches[0].clientX;
            swipeStartY = e.touches[0].clientY;
            isSwiping = true;
        }, { passive: true });

        container.addEventListener('touchend', (e) => {
            if (!isSwiping) return;
            isSwiping = false;
            const t = e.changedTouches[0];
            const dx = t.clientX - swipeStartX;
            const dy = t.clientY - swipeStartY;

            // Only horizontal swipes
            if (Math.abs(dx) > 50 && Math.abs(dx) > Math.abs(dy) * 1.5) {
                const keys = CONFIG.rangeKeys;
                const currentIdx = keys.indexOf(activeRange);
                if (dx < 0 && currentIdx < keys.length - 1) {
                    // Swipe left → bigger range
                    changeRange(keys[currentIdx + 1]);
                } else if (dx > 0 && currentIdx > 0) {
                    // Swipe right → smaller range
                    changeRange(keys[currentIdx - 1]);
                }
            }
        }, { passive: true });

        // Show swipe hint briefly on mobile
        if ('ontouchstart' in window && dom.swipeHint) {
            dom.swipeHint.style.opacity = '1';
            setTimeout(() => {
                dom.swipeHint.style.opacity = '0';
            }, 3000);
        }
    }

    // ═══════════════════
    // Pull-to-Refresh
    // ═══════════════════
    let pullStartY = 0;
    let isPulling = false;
    let pullTriggered = false;

    function initPullToRefresh() {
        const shell = dom.appShell;
        if (!shell || !('ontouchstart' in window)) return;

        shell.addEventListener('touchstart', (e) => {
            if (window.scrollY <= 0 && e.touches.length === 1) {
                pullStartY = e.touches[0].clientY;
                isPulling = true;
                pullTriggered = false;
            }
        }, { passive: true });

        shell.addEventListener('touchmove', (e) => {
            if (!isPulling) return;
            const dy = e.touches[0].clientY - pullStartY;
            if (dy > 0 && window.scrollY <= 0) {
                const progress = Math.min(dy / 120, 1);
                dom.pullIndicator.style.transform = `translateX(-50%) translateY(${Math.min(dy * 0.5, 60)}px)`;
                dom.pullIndicator.style.opacity = String(progress);
                if (progress >= 1 && !pullTriggered) {
                    pullTriggered = true;
                    dom.pullIndicator.classList.add('active');
                }
            }
        }, { passive: true });

        shell.addEventListener('touchend', () => {
            if (!isPulling) return;
            isPulling = false;
            dom.pullIndicator.style.transform = 'translateX(-50%) translateY(0)';
            dom.pullIndicator.style.opacity = '0';
            dom.pullIndicator.classList.remove('active');

            if (pullTriggered) {
                pullTriggered = false;
                refreshData();
            }
        }, { passive: true });
    }

    function refreshData() {
        if (typeof window.fetchStatus === 'function') {
            window.fetchStatus(true);
        }
        startDrawAnimation();
    }

    // ═══════════════════
    // Fullscreen Chart
    // ═══════════════════
    function openFullscreen() {
        isFullscreen = true;
        dom.fullscreenOverlay.hidden = false;
        document.body.style.overflow = 'hidden';
        // Sync range buttons
        dom.fullscreenRangeSelector.querySelectorAll('.range-btn').forEach(b => {
            b.classList.toggle('active', b.dataset.range === activeRange);
        });
        renderFullscreen();
    }

    function closeFullscreen() {
        isFullscreen = false;
        dom.fullscreenOverlay.hidden = true;
        document.body.style.overflow = '';
        fullscreenHoverIndex = -1;
        dom.fullscreenTooltip.hidden = true;
    }

    let lastFullscreenResult = null;

    function renderFullscreen() {
        if (!isFullscreen) return;
        const visibleData = getVisibleData();
        lastFullscreenResult = ChartEngine.renderMainChart(dom.fullscreenCanvas, visibleData, {
            target: CONFIG.target,
            hysteresis: CONFIG.hysteresis,
            hoverIndex: fullscreenHoverIndex,
            drawProgress: 1
        });
        updateTimeAxis(visibleData, dom.fullscreenTimeAxis);
    }

    // ═══════════════════
    // Main Render
    // ═══════════════════
    let lastChartResult = null;

    function render() {
        const visibleData = getVisibleData();

        lastChartResult = ChartEngine.renderMainChart(dom.mainCanvas, visibleData, {
            target: CONFIG.target,
            hysteresis: CONFIG.hysteresis,
            hoverIndex,
            drawProgress
        });
        updateSummary(visibleData);
        updateTimeAxis(visibleData);
        updateMiniCharts(visibleData);
        updateHistoryTable(visibleData);

        if (isFullscreen) renderFullscreen();
    }

    function requestRender() {
        if (animFrameId) cancelAnimationFrame(animFrameId);
        animFrameId = requestAnimationFrame(render);
    }

    // ═══════════════════
    // Hover Handling
    // ═══════════════════
    function findClosestPoint(clientX, canvas, chartResult) {
        if (!chartResult || !chartResult.coords.length) return -1;
        const rect = canvas.getBoundingClientRect();
        const mx = clientX - rect.left;
        let closest = -1, minDist = Infinity;
        for (let i = 0; i < chartResult.coords.length; i++) {
            const d = Math.abs(chartResult.coords[i].x - mx);
            if (d < minDist) { minDist = d; closest = i; }
        }
        return minDist < 40 ? closest : -1;
    }

    function showTooltip(idx, tooltipEl, containerEl, chartResult) {
        if (idx < 0 || !chartResult || !chartResult.coords[idx]) {
            tooltipEl.hidden = true;
            return;
        }
        const c = chartResult.coords[idx];
        const rect = containerEl.getBoundingClientRect();
        tooltipEl.querySelector('.tooltip-time').textContent = ChartEngine.formatTimeFull(c.epoch);
        tooltipEl.querySelector('.tooltip-value').textContent = ChartEngine.formatTemp(c.value, 2);
        tooltipEl.hidden = false;
        const tipW = tooltipEl.offsetWidth;
        let left = ChartEngine.clamp(c.x, tipW / 2 + 4, rect.width - tipW / 2 - 4);
        let top = ChartEngine.clamp(c.y, tooltipEl.offsetHeight + 20, rect.height - 20);
        tooltipEl.style.left = `${left}px`;
        tooltipEl.style.top = `${top}px`;
    }

    function handlePointerMove(e) {
        const touch = e.touches ? e.touches[0] : e;
        const idx = findClosestPoint(touch.clientX, dom.mainCanvas, lastChartResult);
        if (idx !== hoverIndex) { hoverIndex = idx; requestRender(); }
        showTooltip(idx, dom.tooltip, dom.mainContainer, lastChartResult);
        if (e.touches) e.preventDefault();
    }

    function handlePointerLeave() {
        if (hoverIndex !== -1) { hoverIndex = -1; dom.tooltip.hidden = true; requestRender(); }
    }

    function handleFullscreenPointerMove(e) {
        const touch = e.touches ? e.touches[0] : e;
        const idx = findClosestPoint(touch.clientX, dom.fullscreenCanvas, lastFullscreenResult);
        if (idx !== fullscreenHoverIndex) { fullscreenHoverIndex = idx; renderFullscreen(); }
        showTooltip(idx, dom.fullscreenTooltip, dom.fullscreenContainer, lastFullscreenResult);
        if (e.touches) e.preventDefault();
    }

    function handleFullscreenPointerLeave() {
        if (fullscreenHoverIndex !== -1) {
            fullscreenHoverIndex = -1;
            dom.fullscreenTooltip.hidden = true;
            renderFullscreen();
        }
    }

    // ═══════════════════
    // Simulate New Data (Removed)
    // ═══════════════════

    // ═══════════════════
    // CSV Export
    // ═══════════════════
    function exportCSV() {
        const visibleData = getVisibleData();
        let csv = 'Czas,Temperatura (°C),Odchylenie od celu (°C)\n';
        for (const p of visibleData) {
            csv += `${ChartEngine.formatTimeFull(p.epoch)},${p.value.toFixed(2)},${(p.value - CONFIG.target).toFixed(2)}\n`;
        }
        const blob = new Blob(['\uFEFF' + csv], { type: 'text/csv;charset=utf-8;' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `aquasync_temp_${activeRange}_${new Date().toISOString().slice(0, 10)}.csv`;
        a.click();
        URL.revokeObjectURL(url);
    }

    // ═══════════════════
    // Range Change
    // ═══════════════════
    const rangeLabels = {
        '1h': 'Ostatnia godzina', '3h': 'Ostatnie 3 godziny',
        '6h': 'Ostatnie 6 godzin', '12h': 'Ostatnie 12 godzin', '24h': 'Ostatnie 24 godziny'
    };

    function changeRange(range) {
        if (range === activeRange) return;
        activeRange = range;
        syncRangeControls();
        hoverIndex = -1;
        dom.tooltip.hidden = true;
        startDrawAnimation();
    }

    // ═══════════════════
    // Init
    // ═══════════════════
    function init() {
        cacheDom();
        allData = [];
        syncRangeControls();

        const observer = new MutationObserver((mutations) => {
            mutations.forEach((mutation) => {
                const chartsViewActive = mutation.target.classList.contains('active');
                document.body.classList.toggle('charts-view-active', chartsViewActive);
                if (chartsViewActive) {
                    if (dom.bubblesCanvas) {
                        dom.bubblesCanvas.width = window.innerWidth;
                        dom.bubblesCanvas.height = window.innerHeight;
                    }
                    if (allData.length > 0) {
                        requestRender();
                        updateHeatmap();
                        updateDayComparison();
                        startDrawAnimation();
                    }
                }
            });
        });
        if (dom.appShell) observer.observe(dom.appShell, { attributes: true, attributeFilter: ['class'] });
        document.body.classList.toggle('charts-view-active', dom.appShell?.classList.contains('active'));

        // Range selectors (both main and fullscreen)
        document.querySelectorAll('#range-selector, #fullscreen-range-selector').forEach(sel => {
            sel.addEventListener('click', (e) => {
                const btn = e.target.closest('.range-btn');
                if (!btn || !btn.dataset.range) return;
                changeRange(btn.dataset.range);
            });
        });

        // Hover / Touch on main chart
        dom.mainContainer.addEventListener('mousemove', handlePointerMove);
        dom.mainContainer.addEventListener('mouseleave', handlePointerLeave);
        dom.mainContainer.addEventListener('touchmove', handlePointerMove, { passive: false });
        dom.mainContainer.addEventListener('touchend', handlePointerLeave);

        // Fullscreen chart interactions
        dom.fullscreenContainer.addEventListener('mousemove', handleFullscreenPointerMove);
        dom.fullscreenContainer.addEventListener('mouseleave', handleFullscreenPointerLeave);
        dom.fullscreenContainer.addEventListener('touchmove', handleFullscreenPointerMove, { passive: false });
        dom.fullscreenContainer.addEventListener('touchend', handleFullscreenPointerLeave);

        // Fullscreen open/close
        dom.fullscreenBtn.addEventListener('click', openFullscreen);
        dom.fullscreenCloseBtn.addEventListener('click', closeFullscreen);
        // Also open on double-tap/click on chart
        dom.mainContainer.addEventListener('dblclick', openFullscreen);
        document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && isFullscreen) closeFullscreen(); });

        // Export
        dom.btnExport.addEventListener('click', exportCSV);

        // Resize
        let resizeTimer = null;
        window.addEventListener('resize', () => {
            clearTimeout(resizeTimer);
            resizeTimer = setTimeout(() => {
                if (dom.bubblesCanvas) {
                    dom.bubblesCanvas.width = window.innerWidth;
                    dom.bubblesCanvas.height = window.innerHeight;
                }
                requestRender();
                updateHeatmap();
                updateDayComparison();
            }, 150);
        });

        // Clock
        updateClock();
        setInterval(updateClock, 1000);

        // Bubbles
        initBubbles();

        // Swipe & Pull
        initSwipeGestures();
        initPullToRefresh();

        // Heatmap & Day Comparison
        updateHeatmap(true);
        updateDayComparison();
        initHeatmapInteraction();

        // Initial draw-in animation
        startDrawAnimation();

        // Simulation (Removed)

        // Periodic heatmap/comparison refresh
        setInterval(() => { updateHeatmap(); updateDayComparison(); }, 30000);
        setInterval(() => {
            if (typeof window.fetchStatus === 'function' && document.visibilityState === 'visible') {
                window.fetchStatus(true);
            }
        }, 300000);
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
