/* ═══════════════════════════════════════════
   AquaSync — Temperature Charts App v2
   + Draw-in, sparklines, forecast, swipe, pull,
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
        forecastPoints: 6, // 1 hour ahead
        ranges: { '1h': 6, '3h': 18, '6h': 36, '12h': 72, '24h': 144 },
        rangeKeys: ['1h', '3h', '6h', '12h', '24h']
    };
    const MAX_HISTORY_POINTS = 1440;

    // ── State ──
    let allData = [];
    let activeRange = '3h';
    let hoverIndex = -1;
    let animFrameId = null;
    let drawProgress = 0;
    let drawAnimStarted = false;
    let isFullscreen = false;
    let fullscreenHoverIndex = -1;
    let lastHistorySignature = '';
    let chartClockTimer = null;
    let chartRefreshTimer = null;
    let historyRefreshTimer = null;
    let appShellObserver = null;

    // ── DOM Cache ──
    const dom = {};
    function cacheDom() {
        const fallbackNode = document.createElement('div');
        const fallbackCanvas = document.createElement('canvas');
        function s(id, isCanvas) { return document.getElementById(id) || (isCanvas ? fallbackCanvas : fallbackNode); }

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
        dom.archiveList = s('archive-list', false);
        dom.archiveStatus = s('archive-status', false);
        dom.archiveRefreshBtn = s('archive-refresh-btn', false);
        dom.bubblesCanvas = s('bubbles-canvas', true);
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

        const nextCapacityRaw = Math.round(Number(temperatureData?.historyCapacity));
        const nextCapacity = Number.isFinite(nextCapacityRaw)
            ? Math.min(MAX_HISTORY_POINTS, Math.max(1, nextCapacityRaw))
            : null;
        if (nextCapacity !== null && nextCapacity !== CONFIG.maxPoints) {
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

            const boundedHistory = history
                ? history.slice(-Math.min(CONFIG.maxPoints, MAX_HISTORY_POINTS))
                : null;
            const firstPoint = boundedHistory?.[0];
            const lastPoint = boundedHistory?.[boundedHistory.length - 1];
            const historySignature = boundedHistory
                ? `${boundedHistory.length}|${firstPoint?.epoch ?? ''}|${firstPoint?.value ?? ''}|${lastPoint?.epoch ?? ''}|${lastPoint?.value ?? ''}`
                : '';

            if (boundedHistory && historySignature !== lastHistorySignature) {
                lastHistorySignature = historySignature;
                allData = [];

                for (const item of boundedHistory) {
                    const epoch = Number(item?.epoch);
                    const value = Number(item?.value);
                    if (Number.isFinite(epoch) && epoch > 0 && Number.isFinite(value) && value >= -20 && value <= 120) {
                        allData.push({ epoch: epoch * 1000, value });
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
            }
        }
    };

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

            html += `<tr role="row">
                <td role="cell" data-label="Czas">${ChartEngine.formatTimeFull(p.epoch)}</td>
                <td role="cell" data-label="Temperatura" style="color: var(--accent-cyan); font-weight: 600;">${ChartEngine.formatTemp(p.value, 2)}</td>
                <td role="cell" data-label="Trend"><canvas class="sparkline-cell" data-spark-idx="${ri}" width="80" height="24"></canvas></td>
                <td role="cell" data-label="Odchylenie">${dev >= 0 ? '+' : ''}${dev.toFixed(2)}°C</td>
                <td role="cell" data-label="Status"><span class="status-pill ${statusClass}"><span class="status-pip ${statusClass}"></span>${statusText}</span></td>
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

    // Draw-in animation keeps chart refreshes visually stable without extra panels.
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

    function stopBubbles() {
        if (bubblesAnimId !== null) {
            cancelAnimationFrame(bubblesAnimId);
            bubblesAnimId = null;
        }
        bubbles = [];
        fishes = [];
    }

    function initBubbles() {
        const canvas = dom.bubblesCanvas;
        const reduceMotion = typeof window.matchMedia === 'function' &&
            window.matchMedia('(prefers-reduced-motion: reduce)').matches;
        if (!canvas?.isConnected || reduceMotion || getComputedStyle(canvas).display === 'none') {
            stopBubbles();
            return;
        }
        stopBubbles();
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
            if (document.visibilityState !== 'visible' || !canvas.isConnected || getComputedStyle(canvas).display === 'none') {
                stopBubbles();
                return;
            }
            const ctx = canvas.getContext('2d');
            if (!ctx) {
                stopBubbles();
                return;
            }
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
            window.fetchStatus(true, true);
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
    // ═══════════════════

    // ═══════════════════
    // CSV Export
    // ═══════════════════
    async function exportCSV() {
        try {
            const result = await fetchWithTimeout(
                '/api/history.csv',
                { cache: 'no-store' },
                API_REQUEST_TIMEOUT_MS,
                (response) => response.ok ? response.text() : null
            );
            const { response, body: csv } = result;
            if (response.ok) {
                const blob = new Blob(['\uFEFF' + csv], { type: 'text/csv;charset=utf-8;' });
                const url = URL.createObjectURL(blob);
                const a = document.createElement('a');
                a.href = url;
                a.download = `cydaquarium_history_${new Date().toISOString().slice(0, 10)}.csv`;
                a.click();
                URL.revokeObjectURL(url);
                return;
            }
        } catch (_) {
        }

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
    function formatArchiveBytes(bytes) {
        const value = Number(bytes);
        if (!Number.isFinite(value) || value < 0) return '--';
        if (value >= 1024 * 1024) return `${(value / (1024 * 1024)).toFixed(2)} MB`;
        if (value >= 1024) return `${(value / 1024).toFixed(1)} KB`;
        return `${Math.round(value)} B`;
    }

    function archiveRecordCount(fileSize) {
        const size = Math.max(0, Math.trunc(Number(fileSize) || 0));
        if (size <= 32) return 0;
        return Math.floor((size - 32) / 18);
    }

    function setArchiveStatus(message, tone = 'muted') {
        if (!dom.archiveStatus) return;
        dom.archiveStatus.textContent = message;
        dom.archiveStatus.dataset.tone = tone;
    }

    function renderArchiveFiles(files) {
        if (!dom.archiveList) return;
        dom.archiveList.replaceChildren();

        const archives = (Array.isArray(files) ? files : [])
            .filter((file) => String(file?.name || '').toLowerCase().endsWith('.aqbin'))
            .sort((a, b) => String(b.name || '').localeCompare(String(a.name || '')));

        if (!archives.length) {
            const empty = document.createElement('div');
            empty.className = 'archive-empty';
            empty.textContent = 'Brak archiwow .aqbin na karcie SD.';
            dom.archiveList.appendChild(empty);
            setArchiveStatus('Archiwum miesieczne pojawi sie po pierwszym zapisie probki na SD.', 'muted');
            if (typeof setCommandStatus === 'function') {
                setCommandStatus('measure-strip-export', 'CSV', 'Brak archiwow .aqbin na SD', 'neutral');
            }
            return;
        }

        const fragment = document.createDocumentFragment();
        for (const file of archives) {
            const row = document.createElement('div');
            row.className = 'archive-row';

            const name = document.createElement('div');
            name.className = 'archive-name';
            name.textContent = file.name || 'archive.aqbin';

            const meta = document.createElement('div');
            meta.className = 'archive-meta';
            meta.textContent = `${formatArchiveBytes(file.size)} / ${archiveRecordCount(file.size)} rekordow`;

            const link = document.createElement('a');
            link.className = 'archive-download';
            link.href = `/download?path=${encodeURIComponent(file.path || `/aq/data/history/${file.name}`)}`;
            link.textContent = 'Pobierz';

            row.append(name, meta, link);
            fragment.appendChild(row);
        }
        dom.archiveList.appendChild(fragment);
        setArchiveStatus(`Dostepne archiwa: ${archives.length}. Retencja usuwa najstarsze rekordy, gdy SD jest pelna.`, 'ok');
        if (typeof setCommandStatus === 'function') {
            setCommandStatus('measure-strip-export', 'CSV + AQBIN', `${archives.length} archiwow miesiecznych`, 'ok');
        }
    }

    async function refreshArchiveFiles() {
        if (dom.archiveRefreshBtn) {
            dom.archiveRefreshBtn.disabled = true;
        }
        setArchiveStatus('Odczytuje liste archiwow z karty SD...', 'muted');
        try {
            const result = await fetchWithTimeout(
                '/api/files?dir=/aq/data/history',
                { cache: 'no-store' },
                API_REQUEST_TIMEOUT_MS,
                (response) => response.ok ? response.json() : null
            );
            const { response, body: payload } = result;
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            renderArchiveFiles(payload.files || []);
        } catch (error) {
            if (dom.archiveList) {
                const empty = document.createElement('div');
                empty.className = 'archive-empty';
                empty.textContent = 'Nie mozna pobrac listy archiwow. Sprawdz polaczenie z ESP32 i karte SD.';
                dom.archiveList.replaceChildren(empty);
            }
            setArchiveStatus('Lista archiwow niedostepna.', 'error');
            if (typeof setCommandStatus === 'function') {
                setCommandStatus('measure-strip-export', 'Offline', 'Brak listy archiwow SD', 'warn');
            }
        } finally {
            if (dom.archiveRefreshBtn) {
                dom.archiveRefreshBtn.disabled = false;
            }
        }
    }

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

    function stopPeriodicTasks() {
        if (chartClockTimer !== null) {
            clearInterval(chartClockTimer);
            chartClockTimer = null;
        }
        if (chartRefreshTimer !== null) {
            clearInterval(chartRefreshTimer);
            chartRefreshTimer = null;
        }
        if (historyRefreshTimer !== null) {
            clearInterval(historyRefreshTimer);
            historyRefreshTimer = null;
        }
    }

    function startPeriodicTasks() {
        if (document.visibilityState !== 'visible') {
            return;
        }
        if (chartClockTimer === null) {
            chartClockTimer = setInterval(updateClock, 1000);
        }
        if (chartRefreshTimer === null) {
            chartRefreshTimer = setInterval(() => {
                if (dom.appShell?.classList.contains('active')) {
                    requestRender();
                }
            }, 30000);
        }
        if (historyRefreshTimer === null) {
            historyRefreshTimer = setInterval(() => {
                if (typeof window.fetchStatus === 'function') {
                    window.fetchStatus(true, true);
                }
            }, 300000);
        }
    }

    function suspendChartsRuntime() {
        stopPeriodicTasks();
        stopBubbles();
        appShellObserver?.disconnect();
        if (animFrameId !== null) {
            cancelAnimationFrame(animFrameId);
            animFrameId = null;
        }
        if (drawAnimId !== null) {
            cancelAnimationFrame(drawAnimId);
            drawAnimId = null;
        }
    }

    // ═══════════════════
    // Init
    // ═══════════════════
    function init() {
        cacheDom();
        allData = [];
        syncRangeControls();

        appShellObserver = new MutationObserver((mutations) => {
            mutations.forEach((mutation) => {
                const chartsViewActive = mutation.target.classList.contains('active');
                document.body.classList.toggle('charts-view-active', chartsViewActive);
                if (chartsViewActive) {
                    refreshArchiveFiles();
                    if (dom.bubblesCanvas) {
                        dom.bubblesCanvas.width = window.innerWidth;
                        dom.bubblesCanvas.height = window.innerHeight;
                    }
                    if (allData.length > 0) {
                        requestRender();
                        startDrawAnimation();
                    }
                }
            });
        });
        if (dom.appShell) appShellObserver.observe(dom.appShell, { attributes: true, attributeFilter: ['class'] });
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
        dom.archiveRefreshBtn.addEventListener('click', refreshArchiveFiles);

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
            }, 150);
        });

        // Clock
        updateClock();
        startPeriodicTasks();

        // Bubbles
        initBubbles();

        // Swipe & Pull
        initSwipeGestures();
        initPullToRefresh();

        // Initial draw-in animation
        startDrawAnimation();

        refreshArchiveFiles();

        document.addEventListener('visibilitychange', () => {
            if (document.visibilityState === 'visible') {
                startPeriodicTasks();
                if (dom.appShell && appShellObserver) {
                    appShellObserver.observe(dom.appShell, { attributes: true, attributeFilter: ['class'] });
                }
                if (dom.appShell?.classList.contains('active')) {
                    requestRender();
                }
            } else {
                suspendChartsRuntime();
            }
        });
        window.addEventListener('pagehide', suspendChartsRuntime);
        window.addEventListener('pageshow', () => {
            startPeriodicTasks();
            if (dom.appShell && appShellObserver) {
                appShellObserver.observe(dom.appShell, { attributes: true, attributeFilter: ['class'] });
            }
        });
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
