/* ═══════════════════════════════════════════
   AquaSync — Canvas Chart Engine v2
   + Draw-in animation, sparklines, heatmap,
     day comparison, forecast
   ═══════════════════════════════════════════ */

const ChartEngine = (() => {
    'use strict';

    const DPR = Math.min(window.devicePixelRatio || 1, 3);

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
    function lerp(a, b, t) { return a + (b - a) * t; }

    function setupCanvas(canvas) {
        const rect = canvas.getBoundingClientRect();
        const w = Math.round(rect.width * DPR);
        const h = Math.round(rect.height * DPR);
        if (canvas.width !== w || canvas.height !== h) {
            canvas.width = w;
            canvas.height = h;
        }
        const ctx = canvas.getContext('2d');
        ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
        return { ctx, w: rect.width, h: rect.height };
    }

    function buildSmoothPath(coords) {
        if (coords.length < 2) return coords.map(c => ({ type: 'M', x: c.x, y: c.y }));
        const tension = 0.22;
        const result = [{ type: 'M', x: coords[0].x, y: coords[0].y }];
        for (let i = 0; i < coords.length - 1; i++) {
            const p0 = coords[i - 1] || coords[i];
            const p1 = coords[i];
            const p2 = coords[i + 1];
            const p3 = coords[i + 2] || p2;
            result.push({
                type: 'C',
                cp1x: p1.x + (p2.x - p0.x) * tension,
                cp1y: p1.y + (p2.y - p0.y) * tension,
                cp2x: p2.x - (p3.x - p1.x) * tension,
                cp2y: p2.y - (p3.y - p1.y) * tension,
                x: p2.x,
                y: p2.y
            });
        }
        return result;
    }

    function traceSmooth(ctx, cmds) {
        ctx.beginPath();
        for (const cmd of cmds) {
            if (cmd.type === 'M') ctx.moveTo(cmd.x, cmd.y);
            else ctx.bezierCurveTo(cmd.cp1x, cmd.cp1y, cmd.cp2x, cmd.cp2y, cmd.x, cmd.y);
        }
    }

    // Partial tracing for draw-in animation (0..1 progress)
    function traceSmoothPartial(ctx, cmds, progress) {
        if (progress >= 1) { traceSmooth(ctx, cmds); return; }
        if (cmds.length === 0) return;
        ctx.beginPath();
        // Total segments = cmds.length - 1 (first is M)
        const segments = cmds.length - 1;
        const totalProg = progress * segments;
        const fullSegs = Math.floor(totalProg);
        const partialT = totalProg - fullSegs;

        ctx.moveTo(cmds[0].x, cmds[0].y);
        for (let i = 1; i <= fullSegs && i < cmds.length; i++) {
            const c = cmds[i];
            ctx.bezierCurveTo(c.cp1x, c.cp1y, c.cp2x, c.cp2y, c.x, c.y);
        }
        // Partial segment
        if (fullSegs < segments && fullSegs + 1 < cmds.length) {
            const c = cmds[fullSegs + 1];
            const prev = fullSegs > 0 ? cmds[fullSegs] : cmds[0];
            // Approximate: split bezier at t
            const t = partialT;
            const sx = prev.x, sy = prev.y;
            const ex = c.x, ey = c.y;
            const cx1 = c.cp1x, cy1 = c.cp1y, cx2 = c.cp2x, cy2 = c.cp2y;
            // De Casteljau
            const ax = lerp(sx, cx1, t), ay = lerp(sy, cy1, t);
            const bx = lerp(cx1, cx2, t), by = lerp(cy1, cy2, t);
            const ccx = lerp(cx2, ex, t), ccy = lerp(cy2, ey, t);
            const dx = lerp(ax, bx, t), dy = lerp(ay, by, t);
            const eex = lerp(bx, ccx, t), eey = lerp(by, ccy, t);
            const fx = lerp(dx, eex, t), fy = lerp(dy, eey, t);
            ctx.bezierCurveTo(ax, ay, dx, dy, fx, fy);
        }
    }

    function getLastPointAtProgress(cmds, progress) {
        if (cmds.length === 0) return null;
        if (progress >= 1) { const l = cmds[cmds.length - 1]; return { x: l.x, y: l.y }; }
        const segments = cmds.length - 1;
        const totalProg = progress * segments;
        const fullSegs = Math.floor(totalProg);
        const partialT = totalProg - fullSegs;
        if (fullSegs >= segments) { const l = cmds[cmds.length - 1]; return { x: l.x, y: l.y }; }
        const c = cmds[fullSegs + 1];
        const prev = fullSegs > 0 ? cmds[fullSegs] : cmds[0];
        const t = partialT;
        const sx = prev.x, sy = prev.y;
        const cx1 = c.cp1x, cy1 = c.cp1y, cx2 = c.cp2x, cy2 = c.cp2y;
        const ex = c.x, ey = c.y;
        const ax = lerp(sx, cx1, t), ay = lerp(sy, cy1, t);
        const bx = lerp(cx1, cx2, t), by = lerp(cy1, cy2, t);
        const ccx = lerp(cx2, ex, t), ccy = lerp(cy2, ey, t);
        const dx = lerp(ax, bx, t), dy = lerp(ay, by, t);
        const eex = lerp(bx, ccx, t), eey = lerp(by, ccy, t);
        return { x: lerp(dx, eex, t), y: lerp(dy, eey, t) };
    }

    function formatTemp(v, digits = 1) {
        return `${Number(v).toFixed(digits)}°C`;
    }

    function formatTime(epoch) {
        const d = new Date(epoch);
        return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
    }

    function formatTimeFull(epoch) {
        const d = new Date(epoch);
        const mon = ['sty', 'lut', 'mar', 'kwi', 'maj', 'cze', 'lip', 'sie', 'wrz', 'paź', 'lis', 'gru'][d.getMonth()];
        return `${String(d.getDate()).padStart(2, '0')} ${mon}, ${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}:${String(d.getSeconds()).padStart(2, '0')}`;
    }

    // ── Main Chart Renderer (with draw-in + forecast) ──
    function renderMainChart(canvas, data, options = {}) {
        const { ctx, w, h } = setupCanvas(canvas);
        const {
            target = null,
            hysteresis = 0,
            hoverIndex = -1,
            drawProgress = 1,
            forecast = null
        } = options;

        const isMobile = w < 500;
        const pad = {
            top: 16,
            right: isMobile ? 12 : 32,
            bottom: 8,
            left: isMobile ? 36 : 50
        };
        const plotW = w - pad.left - pad.right;
        const plotH = h - pad.top - pad.bottom;

        ctx.clearRect(0, 0, w, h);

        if (!data || data.length === 0) {
            ctx.fillStyle = 'rgba(148, 163, 184, 0.4)';
            ctx.font = `500 ${isMobile ? 12 : 14}px Inter, sans-serif`;
            ctx.textAlign = 'center';
            ctx.fillText('Oczekiwanie na dane temperatury...', w / 2, h / 2);
            return { coords: [], xFor: null, yFor: null, minVal: 0, maxVal: 0 };
        }

        // Combine data + forecast for domain
        const allPoints = forecast ? [...data, ...forecast] : data;

        let minVal = Infinity, maxVal = -Infinity;
        for (const p of allPoints) {
            if (p.value < minVal) minVal = p.value;
            if (p.value > maxVal) maxVal = p.value;
        }
        if (target !== null) {
            const tLo = hysteresis > 0 ? target - hysteresis : target;
            const tHi = hysteresis > 0 ? target + hysteresis : target;
            minVal = Math.min(minVal, tLo);
            maxVal = Math.max(maxVal, tHi);
        }

        let span = maxVal - minVal;
        if (span < 1) { minVal -= 0.5; maxVal += 0.5; span = 1; }
        const domainPad = Math.max(0.2, span * 0.15);
        minVal -= domainPad;
        maxVal += domainPad;

        const minEpoch = data[0].epoch;
        const maxEpochAll = forecast && forecast.length ? forecast[forecast.length - 1].epoch : data[data.length - 1].epoch;
        const timeSpan = Math.max(1, maxEpochAll - minEpoch);

        const xFor = (epoch) => pad.left + ((epoch - minEpoch) / timeSpan) * plotW;
        const yFor = (val) => pad.top + ((maxVal - val) / (maxVal - minVal)) * plotH;

        // ── Grid ──
        const gridSteps = isMobile ? 3 : 5;
        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';
        ctx.font = `500 ${isMobile ? 9 : 10}px Inter, sans-serif`;
        for (let i = 0; i <= gridSteps; i++) {
            const ratio = i / gridSteps;
            const y = pad.top + ratio * plotH;
            const val = maxVal - ratio * (maxVal - minVal);
            ctx.strokeStyle = 'rgba(148, 163, 184, 0.06)';
            ctx.lineWidth = 1;
            ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(pad.left + plotW, y); ctx.stroke();
            ctx.fillStyle = 'rgba(148, 163, 184, 0.4)';
            ctx.fillText(formatTemp(val), pad.left - 6, y);
        }

        // ── Hysteresis Band ──
        if (target !== null && hysteresis > 0) {
            const bandTop = yFor(target + hysteresis);
            const bandBot = yFor(target - hysteresis);
            ctx.fillStyle = 'rgba(251, 191, 36, 0.04)';
            ctx.fillRect(pad.left, bandTop, plotW, bandBot - bandTop);
            ctx.strokeStyle = 'rgba(251, 191, 36, 0.15)';
            ctx.lineWidth = 1;
            ctx.setLineDash([6, 4]);
            ctx.beginPath(); ctx.moveTo(pad.left, bandTop); ctx.lineTo(pad.left + plotW, bandTop); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(pad.left, bandBot); ctx.lineTo(pad.left + plotW, bandBot); ctx.stroke();
            ctx.setLineDash([]);
        }

        // ── Target Line ──
        if (target !== null) {
            const ty = yFor(target);
            ctx.strokeStyle = 'rgba(251, 191, 36, 0.5)';
            ctx.lineWidth = 1.5;
            ctx.setLineDash([8, 6]);
            ctx.beginPath(); ctx.moveTo(pad.left, ty); ctx.lineTo(pad.left + plotW, ty); ctx.stroke();
            ctx.setLineDash([]);
            ctx.fillStyle = 'rgba(251, 191, 36, 0.7)';
            ctx.textAlign = 'left';
            ctx.font = `600 ${isMobile ? 8 : 9}px Inter, sans-serif`;
            ctx.fillText(`Cel ${target.toFixed(1)}°C`, pad.left + 6, ty - 6);
            ctx.textAlign = 'right';
        }

        // ── Forecast ──
        if (forecast && forecast.length >= 2 && drawProgress >= 1) {
            const fCoords = forecast.map(p => ({ x: xFor(p.epoch), y: yFor(p.value) }));
            // Connect from last data point
            const lastData = data[data.length - 1];
            const connCoords = [{ x: xFor(lastData.epoch), y: yFor(lastData.value) }, ...fCoords];
            const fCmds = buildSmoothPath(connCoords);

            // Forecast area
            const fGrad = ctx.createLinearGradient(0, pad.top, 0, pad.top + plotH);
            fGrad.addColorStop(0, 'rgba(167, 139, 250, 0.1)');
            fGrad.addColorStop(1, 'rgba(167, 139, 250, 0.005)');
            traceSmooth(ctx, fCmds);
            ctx.lineTo(connCoords[connCoords.length - 1].x, pad.top + plotH);
            ctx.lineTo(connCoords[0].x, pad.top + plotH);
            ctx.closePath();
            ctx.fillStyle = fGrad;
            ctx.fill();

            // Forecast line
            ctx.save();
            ctx.shadowColor = 'rgba(167, 139, 250, 0.3)';
            ctx.shadowBlur = 6;
            ctx.setLineDash([6, 4]);
            traceSmooth(ctx, fCmds);
            ctx.strokeStyle = 'rgba(167, 139, 250, 0.7)';
            ctx.lineWidth = 2;
            ctx.lineJoin = 'round';
            ctx.lineCap = 'round';
            ctx.stroke();
            ctx.setLineDash([]);
            ctx.restore();

            // Forecast label
            const lastF = fCoords[fCoords.length - 1];
            ctx.fillStyle = 'rgba(167, 139, 250, 0.8)';
            ctx.textAlign = 'right';
            ctx.font = `600 ${isMobile ? 8 : 9}px Inter, sans-serif`;
            ctx.fillText(`Prognoza ${formatTemp(forecast[forecast.length - 1].value)}`, lastF.x - 4, lastF.y - 8);
        }

        // ── Build Coords ──
        const coords = data.map(p => ({ x: xFor(p.epoch), y: yFor(p.value), value: p.value, epoch: p.epoch }));
        const cmds = buildSmoothPath(coords);

        // Apply draw progress
        const prog = clamp(drawProgress, 0, 1);

        // ── Area Fill ──
        const areaGrad = ctx.createLinearGradient(0, pad.top, 0, pad.top + plotH);
        areaGrad.addColorStop(0, 'rgba(34, 211, 238, 0.18)');
        areaGrad.addColorStop(0.6, 'rgba(34, 211, 238, 0.04)');
        areaGrad.addColorStop(1, 'rgba(34, 211, 238, 0.005)');

        traceSmoothPartial(ctx, cmds, prog);
        const endPt = getLastPointAtProgress(cmds, prog);
        if (endPt) {
            ctx.lineTo(endPt.x, pad.top + plotH);
            ctx.lineTo(coords[0].x, pad.top + plotH);
            ctx.closePath();
            ctx.fillStyle = areaGrad;
            ctx.fill();
        }

        // ── Main Line ──
        ctx.save();
        ctx.shadowColor = 'rgba(34, 211, 238, 0.4)';
        ctx.shadowBlur = 8;
        traceSmoothPartial(ctx, cmds, prog);
        ctx.strokeStyle = '#22d3ee';
        ctx.lineWidth = 2.2;
        ctx.lineJoin = 'round';
        ctx.lineCap = 'round';
        ctx.stroke();
        ctx.restore();

        // ── Points (only visible ones based on progress) ──
        const visibleCount = prog >= 1 ? coords.length : Math.ceil(prog * (coords.length - 1)) + 1;
        for (let i = 0; i < Math.min(visibleCount, coords.length); i++) {
            const c = coords[i];
            const isLast = i === coords.length - 1 && prog >= 1;
            const isHover = i === hoverIndex;
            const radius = isLast ? 5 : (isHover ? 5 : 3);
            if (isLast) {
                ctx.beginPath(); ctx.arc(c.x, c.y, 12, 0, Math.PI * 2);
                ctx.fillStyle = 'rgba(34, 211, 238, 0.12)'; ctx.fill();
            }
            if (isHover) {
                ctx.beginPath(); ctx.arc(c.x, c.y, 10, 0, Math.PI * 2);
                ctx.fillStyle = 'rgba(34, 211, 238, 0.15)'; ctx.fill();
            }
            ctx.beginPath(); ctx.arc(c.x, c.y, radius, 0, Math.PI * 2);
            ctx.fillStyle = isLast ? '#22d3ee' : (isHover ? '#22d3ee' : 'rgba(34, 211, 238, 0.7)');
            ctx.fill();
            ctx.strokeStyle = 'rgba(3, 7, 18, 0.6)'; ctx.lineWidth = 1.5; ctx.stroke();
        }

        // ── Hover Crosshair ──
        if (hoverIndex >= 0 && hoverIndex < coords.length) {
            const hc = coords[hoverIndex];
            ctx.strokeStyle = 'rgba(148, 163, 184, 0.2)'; ctx.lineWidth = 1;
            ctx.setLineDash([4, 3]);
            ctx.beginPath(); ctx.moveTo(hc.x, pad.top); ctx.lineTo(hc.x, pad.top + plotH); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(pad.left, hc.y); ctx.lineTo(pad.left + plotW, hc.y); ctx.stroke();
            ctx.setLineDash([]);
        }

        return { coords, xFor, yFor, minVal, maxVal, pad };
    }

    // ── Sparkline (inline for table rows) ──
    function renderSparkline(canvas, values, options = {}) {
        const { ctx, w, h } = setupCanvas(canvas);
        const { color = '#22d3ee', baseline = null } = options;
        ctx.clearRect(0, 0, w, h);
        if (!values || values.length < 2) return;

        let minV = Infinity, maxV = -Infinity;
        for (const v of values) {
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
        if (baseline !== null) { minV = Math.min(minV, baseline); maxV = Math.max(maxV, baseline); }
        if (Math.abs(maxV - minV) < 0.01) { minV -= 0.1; maxV += 0.1; }

        const padX = 1, padY = 2;
        const coords = values.map((v, i) => ({
            x: padX + (i / (values.length - 1)) * (w - padX * 2),
            y: padY + ((maxV - v) / (maxV - minV)) * (h - padY * 2)
        }));
        const cmds = buildSmoothPath(coords);

        traceSmooth(ctx, cmds);
        ctx.strokeStyle = color;
        ctx.lineWidth = 1.5;
        ctx.lineJoin = 'round';
        ctx.lineCap = 'round';
        ctx.stroke();

        const last = coords[coords.length - 1];
        ctx.beginPath();
        ctx.arc(last.x, last.y, 2, 0, Math.PI * 2);
        ctx.fillStyle = color;
        ctx.fill();
    }

    // ── Mini Chart Renderer ──
    function renderMiniChart(canvas, values, options = {}) {
        const { ctx, w, h } = setupCanvas(canvas);
        const {
            color = '#22d3ee',
            glowColor = 'rgba(34, 211, 238, 0.3)',
            areaColor = 'rgba(34, 211, 238, 0.08)',
            baseline = null
        } = options;
        ctx.clearRect(0, 0, w, h);
        if (!values || values.length < 2) {
            ctx.fillStyle = 'rgba(148, 163, 184, 0.2)';
            ctx.font = '400 10px Inter, sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText('—', w / 2, h / 2 + 4);
            return;
        }

        const padY = 6, padX = 2;
        const plotW = w - padX * 2, plotH = h - padY * 2;
        let minV = Infinity, maxV = -Infinity;
        for (const v of values) { if (v < minV) minV = v; if (v > maxV) maxV = v; }
        if (baseline !== null) { minV = Math.min(minV, baseline); maxV = Math.max(maxV, baseline); }
        if (Math.abs(maxV - minV) < 0.01) { minV -= 0.1; maxV += 0.1; }

        const coords = values.map((v, i) => ({
            x: padX + (i / (values.length - 1)) * plotW,
            y: padY + ((maxV - v) / (maxV - minV)) * plotH
        }));
        const cmds = buildSmoothPath(coords);

        if (baseline !== null) {
            const by = padY + ((maxV - baseline) / (maxV - minV)) * plotH;
            ctx.strokeStyle = 'rgba(251, 191, 36, 0.2)'; ctx.lineWidth = 1;
            ctx.setLineDash([3, 3]);
            ctx.beginPath(); ctx.moveTo(padX, by); ctx.lineTo(padX + plotW, by); ctx.stroke();
            ctx.setLineDash([]);
        }

        const grad = ctx.createLinearGradient(0, padY, 0, padY + plotH);
        grad.addColorStop(0, areaColor); grad.addColorStop(1, 'transparent');
        traceSmooth(ctx, cmds);
        ctx.lineTo(coords[coords.length - 1].x, padY + plotH);
        ctx.lineTo(coords[0].x, padY + plotH);
        ctx.closePath(); ctx.fillStyle = grad; ctx.fill();

        ctx.save(); ctx.shadowColor = glowColor; ctx.shadowBlur = 5;
        traceSmooth(ctx, cmds);
        ctx.strokeStyle = color; ctx.lineWidth = 1.8; ctx.lineJoin = 'round'; ctx.lineCap = 'round';
        ctx.stroke(); ctx.restore();

        const last = coords[coords.length - 1];
        ctx.beginPath(); ctx.arc(last.x, last.y, 3, 0, Math.PI * 2);
        ctx.fillStyle = color; ctx.fill();
    }

    // ── Heatmap Renderer (Premium) ──
    function renderHeatmap(canvas, heatData, options = {}) {
        const { ctx, w, h } = setupCanvas(canvas);
        const {
            minTemp = 23, maxTemp = 27, dayLabels = [],
            hoverCell = null, // { row, col }
            waveProgress = 1, // 0..1 for wave-in animation
            currentHour = new Date().getHours()
        } = options;
        ctx.clearRect(0, 0, w, h);

        const isMobile = w < 500;
        const labelW = isMobile ? 30 : 48;
        const labelH = isMobile ? 16 : 22;
        const avgRowH = isMobile ? 18 : 24;
        const avgColW = isMobile ? 28 : 38;
        const cols = 24;
        const rows = heatData.length;
        if (rows === 0) return { cells: [] };

        const gap = isMobile ? 2 : 3;
        const gridW = w - labelW - avgColW;
        const gridH = h - labelH - avgRowH;
        const cellW = (gridW - gap * (cols - 1)) / cols;
        const cellH = (gridH - gap * (rows - 1)) / rows;
        const radius = Math.min(cellW, cellH) * 0.22;

        // Find min/max cells
        let globalMin = Infinity, globalMax = -Infinity;
        let minCell = null, maxCell = null;
        for (let r = 0; r < rows; r++) {
            for (let c = 0; c < cols; c++) {
                const v = heatData[r][c];
                if (v === null || v === undefined) continue;
                if (v < globalMin) { globalMin = v; minCell = { r, c }; }
                if (v > globalMax) { globalMax = v; maxCell = { r, c }; }
            }
        }

        // Compute averages
        const hourAvg = new Array(cols).fill(0);
        const hourCnt = new Array(cols).fill(0);
        const dayAvg = new Array(rows).fill(0);
        const dayCnt = new Array(rows).fill(0);
        for (let r = 0; r < rows; r++) {
            for (let c = 0; c < cols; c++) {
                const v = heatData[r][c];
                if (v !== null && v !== undefined) {
                    hourAvg[c] += v; hourCnt[c]++;
                    dayAvg[r] += v; dayCnt[r]++;
                }
            }
        }
        for (let c = 0; c < cols; c++) if (hourCnt[c] > 0) hourAvg[c] /= hourCnt[c];
        for (let r = 0; r < rows; r++) if (dayCnt[r] > 0) dayAvg[r] /= dayCnt[r];

        // Cell positions map for hover/click detection
        const cells = [];

        // ── Hour labels ──
        ctx.textAlign = 'center';
        ctx.textBaseline = 'bottom';
        ctx.font = `600 ${isMobile ? 7 : 9}px Inter, sans-serif`;
        for (let c = 0; c < cols; c += (isMobile ? 4 : 2)) {
            const cx = labelW + c * (cellW + gap) + cellW / 2;
            const isNow = c === currentHour;
            ctx.fillStyle = isNow ? 'rgba(34, 211, 238, 0.9)' : 'rgba(148, 163, 184, 0.5)';
            ctx.fillText(`${String(c).padStart(2, '0')}`, cx, labelH - 3);
        }

        // ── Current hour column highlight ──
        const nowX = labelW + currentHour * (cellW + gap);
        ctx.fillStyle = 'rgba(34, 211, 238, 0.04)';
        roundRect(ctx, nowX - 1, labelH - 2, cellW + 2, gridH + 4, 4);
        ctx.fill();
        ctx.strokeStyle = 'rgba(34, 211, 238, 0.15)';
        ctx.lineWidth = 1;
        roundRect(ctx, nowX - 1, labelH - 2, cellW + 2, gridH + 4, 4);
        ctx.stroke();

        // ── Day labels + cells ──
        for (let r = 0; r < rows; r++) {
            const yOff = labelH + r * (cellH + gap);

            // Day label
            ctx.textAlign = 'right';
            ctx.textBaseline = 'middle';
            ctx.fillStyle = 'rgba(148, 163, 184, 0.55)';
            ctx.font = `600 ${isMobile ? 7 : 9}px Inter, sans-serif`;
            ctx.fillText(dayLabels[r] || '', labelW - 6, yOff + cellH / 2);

            for (let c = 0; c < cols; c++) {
                const val = heatData[r][c];
                const x = labelW + c * (cellW + gap);
                const y = yOff;

                // Wave animation: cells appear in diagonal wave
                const waveDist = (r + c) / (rows + cols - 2);
                const cellOpacity = waveProgress >= 1 ? 1 : clamp((waveProgress - waveDist * 0.6) / 0.4, 0, 1);
                if (cellOpacity <= 0) continue;

                ctx.globalAlpha = cellOpacity;

                const isHover = hoverCell && hoverCell.row === r && hoverCell.col === c;
                const isMin = minCell && minCell.r === r && minCell.c === c;
                const isMax = maxCell && maxCell.r === r && maxCell.c === c;

                // Background
                if (val === null || val === undefined) {
                    ctx.fillStyle = 'rgba(148, 163, 184, 0.04)';
                    roundRect(ctx, x, y, cellW, cellH, radius);
                    ctx.fill();
                } else {
                    const t = clamp((val - minTemp) / (maxTemp - minTemp), 0, 1);
                    const color = heatmapColorViridis(t);

                    // Hot cells glow
                    if (t > 0.75) {
                        ctx.save();
                        ctx.shadowColor = color;
                        ctx.shadowBlur = 6 + (t - 0.75) * 16;
                        ctx.fillStyle = color;
                        roundRect(ctx, x, y, cellW, cellH, radius);
                        ctx.fill();
                        ctx.restore();
                    } else {
                        ctx.fillStyle = color;
                        roundRect(ctx, x, y, cellW, cellH, radius);
                        ctx.fill();
                    }

                    // Hover highlight
                    if (isHover) {
                        ctx.strokeStyle = 'rgba(255, 255, 255, 0.8)';
                        ctx.lineWidth = 2;
                        roundRect(ctx, x, y, cellW, cellH, radius);
                        ctx.stroke();
                        // Bright overlay
                        ctx.fillStyle = 'rgba(255, 255, 255, 0.12)';
                        roundRect(ctx, x, y, cellW, cellH, radius);
                        ctx.fill();
                    }

                    // Min cell border (blue glow)
                    if (isMin) {
                        ctx.save();
                        ctx.shadowColor = '#3b82f6';
                        ctx.shadowBlur = 8;
                        ctx.strokeStyle = 'rgba(59, 130, 246, 0.9)';
                        ctx.lineWidth = 2;
                        roundRect(ctx, x, y, cellW, cellH, radius);
                        ctx.stroke();
                        ctx.restore();
                    }

                    // Max cell border (gold glow)
                    if (isMax) {
                        ctx.save();
                        ctx.shadowColor = '#fbbf24';
                        ctx.shadowBlur = 8;
                        ctx.strokeStyle = 'rgba(251, 191, 36, 0.9)';
                        ctx.lineWidth = 2;
                        roundRect(ctx, x, y, cellW, cellH, radius);
                        ctx.stroke();
                        ctx.restore();
                    }
                }

                // Store cell info for detection
                cells.push({ row: r, col: c, x, y, w: cellW, h: cellH, val });

                ctx.globalAlpha = 1;
            }
        }

        // ── Average column (right side) ──
        const avgColX = labelW + cols * (cellW + gap) + 4;
        ctx.font = `600 ${isMobile ? 6 : 8}px Inter, sans-serif`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'bottom';
        ctx.fillStyle = 'rgba(148, 163, 184, 0.4)';
        ctx.fillText('Ø', avgColX + avgColW / 2, labelH - 3);

        for (let r = 0; r < rows; r++) {
            if (dayCnt[r] === 0) continue;
            const yOff = labelH + r * (cellH + gap);
            const t = clamp((dayAvg[r] - minTemp) / (maxTemp - minTemp), 0, 1);
            ctx.fillStyle = heatmapColorViridis(t);
            ctx.globalAlpha = 0.5;
            roundRect(ctx, avgColX, yOff, avgColW - 4, cellH, radius);
            ctx.fill();
            ctx.globalAlpha = 1;
            const fs = Math.max(6, Math.min(cellH * 0.36, 10));
            ctx.font = `700 ${fs}px Inter, sans-serif`;
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillStyle = t > 0.5 ? 'rgba(0, 0, 0, 0.6)' : 'rgba(255, 255, 255, 0.8)';
            ctx.fillText(dayAvg[r].toFixed(1), avgColX + (avgColW - 4) / 2, yOff + cellH / 2);
        }

        // ── Average row (bottom) ──
        const avgRowY = labelH + rows * (cellH + gap) + 4;
        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';
        ctx.fillStyle = 'rgba(148, 163, 184, 0.4)';
        ctx.font = `600 ${isMobile ? 6 : 8}px Inter, sans-serif`;
        ctx.fillText('Ø', labelW - 6, avgRowY + avgRowH / 2 - 2);

        for (let c = 0; c < cols; c++) {
            if (hourCnt[c] === 0) continue;
            const x = labelW + c * (cellW + gap);
            const t = clamp((hourAvg[c] - minTemp) / (maxTemp - minTemp), 0, 1);
            ctx.fillStyle = heatmapColorViridis(t);
            ctx.globalAlpha = 0.5;
            roundRect(ctx, x, avgRowY, cellW, avgRowH - 4, radius);
            ctx.fill();
            ctx.globalAlpha = 1;
            const fs = Math.max(5, Math.min(cellW * 0.34, avgRowH * 0.45, 9));
            ctx.font = `700 ${fs}px Inter, sans-serif`;
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillStyle = t > 0.5 ? 'rgba(0, 0, 0, 0.6)' : 'rgba(255, 255, 255, 0.8)';
            ctx.fillText(hourAvg[c].toFixed(1), x + cellW / 2, avgRowY + (avgRowH - 4) / 2);
        }

        return { cells, cellW, cellH, labelW, labelH, gap, cols, rows };
    }

    // Viridis-inspired heatmap palette (dark purple → teal → green → yellow)
    function heatmapColorViridis(t) {
        const stops = [
            { t: 0.0,  r: 68,  g: 1,   b: 84  },
            { t: 0.15, r: 72,  g: 36,  b: 117 },
            { t: 0.30, r: 56,  g: 88,  b: 140 },
            { t: 0.45, r: 31,  g: 150, b: 139 },
            { t: 0.60, r: 53,  g: 183, b: 121 },
            { t: 0.75, r: 109, g: 205, b: 89  },
            { t: 0.90, r: 180, g: 222, b: 44  },
            { t: 1.0,  r: 253, g: 231, b: 37  }
        ];
        let s0 = stops[0], s1 = stops[stops.length - 1];
        for (let i = 0; i < stops.length - 1; i++) {
            if (t >= stops[i].t && t <= stops[i + 1].t) {
                s0 = stops[i]; s1 = stops[i + 1]; break;
            }
        }
        const f = s1.t === s0.t ? 0 : (t - s0.t) / (s1.t - s0.t);
        const r = Math.round(lerp(s0.r, s1.r, f));
        const g = Math.round(lerp(s0.g, s1.g, f));
        const b = Math.round(lerp(s0.b, s1.b, f));
        const a = 0.65 + t * 0.3;
        return `rgba(${r}, ${g}, ${b}, ${a})`;
    }

    function roundRect(ctx, x, y, w, h, r) {
        ctx.beginPath();
        ctx.moveTo(x + r, y);
        ctx.lineTo(x + w - r, y);
        ctx.quadraticCurveTo(x + w, y, x + w, y + r);
        ctx.lineTo(x + w, y + h - r);
        ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
        ctx.lineTo(x + r, y + h);
        ctx.quadraticCurveTo(x, y + h, x, y + h - r);
        ctx.lineTo(x, y + r);
        ctx.quadraticCurveTo(x, y, x + r, y);
        ctx.closePath();
    }

    // ── Day Comparison Chart ──
    const DAY_COLORS = [
        '#22d3ee', '#a78bfa', '#fbbf24', '#fb7185', '#34d399', '#f97316', '#60a5fa'
    ];

    function renderDayComparison(canvas, dayDataSets, options = {}) {
        const { ctx, w, h } = setupCanvas(canvas);
        const { target = null, hysteresis = 0, dayLabels = [] } = options;
        ctx.clearRect(0, 0, w, h);
        if (!dayDataSets || dayDataSets.length === 0) return;

        const isMobile = w < 500;
        const pad = { top: 16, right: isMobile ? 12 : 28, bottom: 8, left: isMobile ? 36 : 50 };
        const plotW = w - pad.left - pad.right;
        const plotH = h - pad.top - pad.bottom;

        // Domain across all days
        let minVal = Infinity, maxVal = -Infinity;
        for (const ds of dayDataSets) {
            for (const p of ds) {
                if (p.value < minVal) minVal = p.value;
                if (p.value > maxVal) maxVal = p.value;
            }
        }
        if (target !== null) {
            minVal = Math.min(minVal, target - (hysteresis || 0));
            maxVal = Math.max(maxVal, target + (hysteresis || 0));
        }
        let span = maxVal - minVal;
        if (span < 1) { minVal -= 0.5; maxVal += 0.5; span = 1; }
        const dp = Math.max(0.2, span * 0.12);
        minVal -= dp; maxVal += dp;

        const yFor = (val) => pad.top + ((maxVal - val) / (maxVal - minVal)) * plotH;

        // Grid
        const gridSteps = isMobile ? 3 : 5;
        ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
        ctx.font = `500 ${isMobile ? 9 : 10}px Inter, sans-serif`;
        for (let i = 0; i <= gridSteps; i++) {
            const ratio = i / gridSteps;
            const y = pad.top + ratio * plotH;
            const val = maxVal - ratio * (maxVal - minVal);
            ctx.strokeStyle = 'rgba(148, 163, 184, 0.06)'; ctx.lineWidth = 1;
            ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(pad.left + plotW, y); ctx.stroke();
            ctx.fillStyle = 'rgba(148, 163, 184, 0.4)';
            ctx.fillText(formatTemp(val), pad.left - 6, y);
        }

        // Target
        if (target !== null) {
            const ty = yFor(target);
            ctx.strokeStyle = 'rgba(251, 191, 36, 0.3)'; ctx.lineWidth = 1;
            ctx.setLineDash([6, 4]);
            ctx.beginPath(); ctx.moveTo(pad.left, ty); ctx.lineTo(pad.left + plotW, ty); ctx.stroke();
            ctx.setLineDash([]);
        }

        // Time axis labels (0-24h)
        ctx.textAlign = 'center'; ctx.textBaseline = 'top';
        ctx.font = `500 ${isMobile ? 8 : 9}px Inter, sans-serif`;
        ctx.fillStyle = 'rgba(148, 163, 184, 0.35)';
        for (let hr = 0; hr <= 24; hr += (isMobile ? 6 : 3)) {
            const x = pad.left + (hr / 24) * plotW;
            ctx.fillText(`${String(hr).padStart(2, '0')}:00`, x, pad.top + plotH + 2);
        }

        // Draw each day
        dayDataSets.forEach((ds, dayIdx) => {
            if (ds.length < 2) return;
            const color = DAY_COLORS[dayIdx % DAY_COLORS.length];
            // Normalize time to 0-24h (fraction of day)
            const coords = ds.map(p => {
                const d = new Date(p.epoch);
                const frac = (d.getHours() * 60 + d.getMinutes()) / (24 * 60);
                return { x: pad.left + frac * plotW, y: yFor(p.value) };
            });
            const cmds = buildSmoothPath(coords);
            ctx.save();
            ctx.shadowColor = color.replace(')', ', 0.25)').replace('rgb', 'rgba');
            ctx.shadowBlur = 4;
            ctx.globalAlpha = dayIdx === dayDataSets.length - 1 ? 1 : 0.45;
            traceSmooth(ctx, cmds);
            ctx.strokeStyle = color;
            ctx.lineWidth = dayIdx === dayDataSets.length - 1 ? 2.5 : 1.5;
            ctx.lineJoin = 'round'; ctx.lineCap = 'round';
            ctx.stroke();
            ctx.globalAlpha = 1;
            ctx.restore();
        });

        return { dayLabels };
    }

    return {
        renderMainChart,
        renderMiniChart,
        renderSparkline,
        renderHeatmap,
        renderDayComparison,
        formatTemp,
        formatTime,
        formatTimeFull,
        clamp,
        lerp,
        DAY_COLORS
    };
})();
