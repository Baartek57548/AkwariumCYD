/* ═══════════════════════════════════════════
   AquaSync — Canvas Chart Engine v2
   + Main temperature chart and compact table sparklines
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
            left: isMobile ? 54 : 50
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
        ctx.font = `500 ${isMobile ? 13 : 10}px Inter, sans-serif`;
        for (let i = 0; i <= gridSteps; i++) {
            const ratio = i / gridSteps;
            const y = pad.top + ratio * plotH;
            const val = maxVal - ratio * (maxVal - minVal);
            ctx.strokeStyle = 'rgba(148, 163, 184, 0.06)';
            ctx.lineWidth = 1;
            ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(pad.left + plotW, y); ctx.stroke();
            ctx.fillStyle = document.documentElement.dataset.theme === 'light' ? '#475569' : '#cbd5e1';
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
            ctx.font = `600 ${isMobile ? 12 : 9}px Inter, sans-serif`;
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
            ctx.font = `600 ${isMobile ? 12 : 9}px Inter, sans-serif`;
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

    return {
        renderMainChart,
        renderSparkline,
        formatTemp,
        formatTime,
        formatTimeFull,
        clamp,
        lerp
    };
})();
