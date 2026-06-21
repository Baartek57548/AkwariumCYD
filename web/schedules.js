function scheduleModeToUi(value) {
    if (typeof value === 'string') {
        const normalized = value.trim().toLowerCase();
        if (normalized === 'always_on' || normalized === 'zawsze_wlaczone' || normalized === 'on') {
            return 'zawsze_wlaczone';
        }
        if (normalized === 'always_off' || normalized === 'zawsze_wylaczone' || normalized === 'off') {
            return 'zawsze_wylaczone';
        }
        return 'harmonogram';
    }

    switch (Number(value)) {
        case 1:
            return 'zawsze_wlaczone';
        case 2:
            return 'zawsze_wylaczone';
        default:
            return 'harmonogram';
    }
}

function scheduleModeToApi(value) {
    switch (value) {
        case 'zawsze_wlaczone':
            return 1;
        case 'zawsze_wylaczone':
            return 2;
        default:
            return 0;
    }
}

function heaterModeToUi(value) {
    return Number(value) === 1 ? 'zawsze_wylaczone' : 'harmonogram';
}

function heaterModeToApi(value) {
    return value === 'zawsze_wylaczone' ? 1 : 0;
}

function feedFrequencyToUi(value) {
    switch (Number(value)) {
        case 1:
            return 'codziennie';
        case 2:
            return 'co_2_dni';
        case 3:
            return 'co_3_dni';
        default:
            return 'wylaczone';
    }
}

function feedFrequencyToApi(value) {
    switch (value) {
        case 'codziennie':
            return 1;
        case 'co_2_dni':
            return 2;
        case 'co_3_dni':
            return 3;
        default:
            return 0;
    }
}

function normalizeTimeInputValue(value) {
    const match = String(value || '').match(/^(\d{1,2}):(\d{1,2})$/);
    if (!match) {
        return '00:00';
    }
    const hours = clamp(Number(match[1]), 0, 23);
    const minutes = clamp(Number(match[2]), 0, 59);
    return `${formatTwoDigits(hours)}:${formatTwoDigits(minutes)}`;
}

function normalizeScheduleTimeInput(id) {
    const input = document.getElementById(id);
    const normalized = normalizeTimeInputValue(input?.value);
    if (input && input.value !== normalized) {
        input.value = normalized;
    }
    return normalized;
}

function clearScheduleDirtyState() {
    document.querySelectorAll('#harmonogramy [data-dirty="1"]').forEach((el) => {
        el.dataset.dirty = '0';
    });
}

function markScheduleDirty() {
    setScheduleStatus('Masz niezapisane zmiany harmonogramów.', 'muted');
}

function parseScheduleClock(value, fallbackHour, fallbackMinute) {
    const normalized = normalizeTimeInputValue(value || formatTime(fallbackHour, fallbackMinute));
    const [hour, minute] = normalized.split(':').map(Number);
    return { hour, minute, text: normalized };
}

function nestedSchedule(data, key) {
    const schedule = data?.schedules?.[key];
    return schedule && typeof schedule === 'object' ? schedule : null;
}

function readScheduleRange(data, key, legacy, fallback) {
    const flat = data.schedule || {};
    const nested = nestedSchedule(data, key);
    const legacyStartOk = toFiniteNumber(flat[legacy.startHour]) !== null && toFiniteNumber(flat[legacy.startMin]) !== null;
    const legacyEndOk = toFiniteNumber(flat[legacy.endHour]) !== null && toFiniteNumber(flat[legacy.endMin]) !== null;
    const startSource = nested?.start || (legacyStartOk ? formatTime(flat[legacy.startHour], flat[legacy.startMin]) : null);
    const endSource = nested?.end || (legacyEndOk ? formatTime(flat[legacy.endHour], flat[legacy.endMin]) : null);
    const start = parseScheduleClock(startSource, fallback.startHour, fallback.startMin);
    const end = parseScheduleClock(endSource, fallback.endHour, fallback.endMin);
    const mode = nested?.mode !== undefined ? nested.mode : flat[legacy.mode];

    return {
        mode: scheduleModeToUi(mode),
        start: start.text,
        end: end.text
    };
}

function syncWorkWindowDependents() {
    const start = normalizeScheduleTimeInput('schedule-work-start');
    const end = normalizeScheduleTimeInput('schedule-work-end');

    [
        ['schedule-light-start', start],
        ['schedule-light-end', end],
        ['schedule-heater-start', start],
        ['schedule-heater-end', end]
    ].forEach(([id, value]) => {
        const input = document.getElementById(id);
        if (!input) return;
        input.value = value;
        input.disabled = true;
        input.title = 'Godziny wynikają z okna pracy akwarium.';
    });

    ['schedule-light-mode', 'schedule-heater-mode'].forEach((id) => {
        const select = document.getElementById(id);
        if (select) {
            select.title = id === 'schedule-heater-mode'
                ? 'Tryb grzałki zapisuje automatykę progową albo OFF.'
                : 'Tryb światła głównego korzysta z okna pracy akwarium.';
        }
    });

    document.querySelectorAll('#harmonogramy .schedule-item').forEach((item) => {
        const kind = item.getAttribute('data-schedule-kind') || 'point';
        if (kind === 'range') {
            updateRangeScheduleItem(item);
        }
    });
}

function renderScheduleEditor(data) {
    const schedule = data.schedule || {};
    const feeding = data.feeding || {};

    const work = readScheduleRange(
        data,
        'light',
        {
            mode: 'lightMode',
            startHour: 'dayStartHour',
            startMin: 'dayStartMin',
            endHour: 'dayEndHour',
            endMin: 'dayEndMin'
        },
        { startHour: 10, startMin: 0, endHour: 21, endMin: 0 }
    );
    const plant = readScheduleRange(
        data,
        'plant_light',
        {
            mode: 'plantLightMode',
            startHour: 'plantStartHour',
            startMin: 'plantStartMin',
            endHour: 'plantEndHour',
            endMin: 'plantEndMin'
        },
        { startHour: 12, startMin: 0, endHour: 18, endMin: 0 }
    );
    const filter = readScheduleRange(
        data,
        'filter',
        {
            mode: 'filterMode',
            startHour: 'filterStartHour',
            startMin: 'filterStartMin',
            endHour: 'filterEndHour',
            endMin: 'filterEndMin'
        },
        { startHour: 10, startMin: 30, endHour: 20, endMin: 30 }
    );
    const air = readScheduleRange(
        data,
        'air',
        {
            mode: 'airMode',
            startHour: 'airStartHour',
            startMin: 'airStartMin',
            endHour: 'airEndHour',
            endMin: 'airEndMin'
        },
        { startHour: 10, startMin: 0, endHour: 19, endMin: 0 }
    );

    setInputValueIfClean('schedule-work-mode', 'harmonogram');
    setInputValueIfClean('schedule-work-start', work.start);
    setInputValueIfClean('schedule-work-end', work.end);

    setInputValueIfClean('schedule-light-mode', work.mode);
    setInputValueIfClean('schedule-light-start', work.start);
    setInputValueIfClean('schedule-light-end', work.end);

    setInputValueIfClean('schedule-plant-light-mode', plant.mode);
    setInputValueIfClean('schedule-plant-light-start', plant.start);
    setInputValueIfClean('schedule-plant-light-end', plant.end);

    setInputValueIfClean('schedule-filter-mode', filter.mode);
    setInputValueIfClean('schedule-filter-start', filter.start);
    setInputValueIfClean('schedule-filter-end', filter.end);

    setInputValueIfClean('schedule-air-mode', air.mode);
    setInputValueIfClean('schedule-air-start', air.start);
    setInputValueIfClean('schedule-air-end', air.end);

    setInputValueIfClean('schedule-heater-mode', heaterModeToUi(schedule.heaterMode ?? data.temperature?.heaterMode));
    setInputValueIfClean('schedule-heater-start', work.start);
    setInputValueIfClean('schedule-heater-end', work.end);

    setInputValueIfClean('schedule-feed-mode', feedFrequencyToUi(feeding.freq));
    setInputValueIfClean('schedule-feed-time', formatTime(feeding.hour, feeding.minute));

    syncWorkWindowDependents();

    const heaterEnabled = heaterModeToUi(schedule.heaterMode ?? data.temperature?.heaterMode) !== 'zawsze_wylaczone';
    const feedEnabled = Number(feeding.freq) > 0;
    const enabledOutputs = [
        work.mode !== 'zawsze_wylaczone',
        plant.mode !== 'zawsze_wylaczone',
        filter.mode !== 'zawsze_wylaczone',
        air.mode !== 'zawsze_wylaczone',
        heaterEnabled,
        feedEnabled
    ].filter(Boolean).length;

    setCommandStatus(
        'schedule-strip-window',
        work.mode === 'zawsze_wlaczone' ? 'Cala doba' : `${work.start} - ${work.end}`,
        heaterEnabled ? 'Grzalka dziala tylko w oknie akwarium' : 'Grzalka wylaczona',
        work.mode === 'zawsze_wylaczone' ? 'warn' : 'info'
    );
    setCommandStatus(
        'schedule-strip-outputs',
        commandCountLabel(enabledOutputs, 'modul aktywny', 'modulow aktywnych'),
        `${plant.mode === 'harmonogram' ? 'roslinne wg planu' : 'roslinne recznie'} / karmnik ${feedEnabled ? 'ON' : 'OFF'}`,
        enabledOutputs > 0 ? 'ok' : 'neutral'
    );

    if (!document.querySelector('#harmonogramy [data-dirty="1"]')) {
        setScheduleStatus('Harmonogramy zsynchronizowane ze sterownikiem.', 'success');
    }
}

async function saveScheduleSettings() {
    const button = document.getElementById('save-schedule-btn');
    if (!button) return;

    syncWorkWindowDependents();

    const workStart = normalizeScheduleTimeInput('schedule-work-start');
    const workEnd = normalizeScheduleTimeInput('schedule-work-end');
    const payload = {
        lightMode: scheduleModeToApi(document.getElementById('schedule-light-mode')?.value),
        dayStart: workStart,
        dayEnd: workEnd,
        lightStart: workStart,
        lightEnd: workEnd,
        plantLightMode: scheduleModeToApi(document.getElementById('schedule-plant-light-mode')?.value),
        plantLightStart: normalizeScheduleTimeInput('schedule-plant-light-start'),
        plantLightEnd: normalizeScheduleTimeInput('schedule-plant-light-end'),
        aerationMode: scheduleModeToApi(document.getElementById('schedule-air-mode')?.value),
        airOn: normalizeScheduleTimeInput('schedule-air-start'),
        airOff: normalizeScheduleTimeInput('schedule-air-end'),
        filterMode: scheduleModeToApi(document.getElementById('schedule-filter-mode')?.value),
        filterOn: normalizeScheduleTimeInput('schedule-filter-start'),
        filterOff: normalizeScheduleTimeInput('schedule-filter-end'),
        heaterMode: heaterModeToApi(document.getElementById('schedule-heater-mode')?.value),
        heaterStart: workStart,
        heaterEnd: workEnd,
        feedFreq: feedFrequencyToApi(document.getElementById('schedule-feed-mode')?.value),
        feedTime: normalizeScheduleTimeInput('schedule-feed-time')
    };

    button.disabled = true;
    setScheduleStatus('Zapisywanie harmonogramów trwa.', 'muted');

    try {
        const response = await sendAction('save_schedule', payload);
        clearScheduleDirtyState();
        setScheduleStatus(
            response?.code === 'settings_partial'
                ? 'Zapisano harmonogramy po korekcie niektórych wartości.'
                : 'Zapisano harmonogramy.',
            response?.code === 'settings_partial' ? 'warning' : 'success'
        );
        await fetchStatus(true);
    } catch (error) {
        setScheduleStatus(`Błąd zapisu harmonogramów: ${error?.message || 'nieznany błąd'}`, 'error');
    } finally {
        button.disabled = false;
    }
}

function timeToMinutes(time) {
    const normalized = normalizeTimeInputValue(time);
    const [hours, minutes] = normalized.split(':').map(Number);
    return (hours * 60) + minutes;
}

function minutesToPercent(totalMinutes) {
    return Math.max(0, Math.min(100, (totalMinutes / (24 * 60)) * 100));
}

function setTimelineInputPosition(input, percent) {
    if (!input) return;
    const container = input.parentElement;
    const containerWidth = container?.clientWidth || 0;
    const inputWidth = input.offsetWidth || 0;
    if (containerWidth > 0 && inputWidth > 0) {
        const halfWidthPct = (((inputWidth / 2) + 14) / containerWidth) * 100;
        input.style.left = `${clamp(percent, halfWidthPct, 100 - halfWidthPct)}%`;
        return;
    }
    input.style.left = `${clamp(percent, 0, 100)}%`;
}

function updateRangeScheduleItem(item) {
    const modeSelect = item.querySelector('.schedule-mode-select');
    const startInput = item.querySelector('.schedule-time-start');
    const endInput = item.querySelector('.schedule-time-end');
    const bar = item.querySelector('.schedule-bar');
    if (!modeSelect || !startInput || !endInput || !bar) return;

    const mode = modeSelect.value;
    const startMinutes = timeToMinutes(startInput.value);
    const endMinutes = timeToMinutes(endInput.value);
    const wrapsMidnight = mode === 'harmonogram' && endMinutes < startMinutes;
    const baseColor = bar.dataset.barColor || bar.style.background || 'var(--accent-cyan)';
    const lockedToWork = startInput.id === 'schedule-light-start' || startInput.id === 'schedule-heater-start';

    startInput.disabled = lockedToWork || mode !== 'harmonogram';
    endInput.disabled = lockedToWork || mode !== 'harmonogram';

    const startPct = minutesToPercent(startMinutes);
    let endPct = minutesToPercent(endMinutes);

    if (mode === 'zawsze_wlaczone') {
        endPct = 100;
        bar.style.left = '0%';
        bar.style.width = '100%';
        bar.style.background = baseColor;
    } else if (mode === 'zawsze_wylaczone') {
        bar.style.left = '0%';
        bar.style.width = '0%';
        bar.style.background = baseColor;
    } else if (wrapsMidnight) {
        bar.style.left = '0%';
        bar.style.width = '100%';
        bar.style.background = `linear-gradient(90deg, ${baseColor} 0%, ${baseColor} ${endPct}%, transparent ${endPct}%, transparent ${startPct}%, ${baseColor} ${startPct}%, ${baseColor} 100%)`;
    } else {
        bar.style.left = `${startPct}%`;
        bar.style.width = `${Math.max(0, endPct - startPct)}%`;
        bar.style.background = baseColor;
    }

    setTimelineInputPosition(startInput, startPct);
    setTimelineInputPosition(endInput, endPct);
}

function updatePointScheduleItem(item) {
    const pointInput = item.querySelector('.schedule-time-point');
    const point = item.querySelector('.schedule-point');
    if (!pointInput || !point) return;

    const pointPct = minutesToPercent(timeToMinutes(pointInput.value));
    point.style.left = `${pointPct}%`;
    setTimelineInputPosition(pointInput, pointPct);
}

function refreshScheduleTimelineLayout() {
    syncWorkWindowDependents();
    document.querySelectorAll('#harmonogramy .schedule-item').forEach((item) => {
        const kind = item.getAttribute('data-schedule-kind') || 'point';
        if (kind === 'range') {
            updateRangeScheduleItem(item);
        } else {
            updatePointScheduleItem(item);
        }
    });
}

function initScheduleTimeline() {
    const scheduleItems = document.querySelectorAll('#harmonogramy .schedule-item');
    scheduleItems.forEach((item) => {
        const kind = item.getAttribute('data-schedule-kind') || 'point';
        if (kind === 'range') {
            const modeSelect = item.querySelector('.schedule-mode-select');
            const startInput = item.querySelector('.schedule-time-start');
            const endInput = item.querySelector('.schedule-time-end');
            const onRangeChange = () => {
                [modeSelect, startInput, endInput].forEach((el) => {
                    if (el) el.dataset.dirty = '1';
                });
                if (startInput?.id === 'schedule-work-start' || endInput?.id === 'schedule-work-end') {
                    syncWorkWindowDependents();
                } else {
                    updateRangeScheduleItem(item);
                }
                markScheduleDirty();
            };
            modeSelect?.addEventListener('change', onRangeChange);
            startInput?.addEventListener('input', onRangeChange);
            endInput?.addEventListener('input', onRangeChange);
            updateRangeScheduleItem(item);
        } else {
            const feedModeSelect = item.querySelector('select');
            const pointInput = item.querySelector('.schedule-time-point');
            const onPointChange = () => {
                [feedModeSelect, pointInput].forEach((el) => {
                    if (el) el.dataset.dirty = '1';
                });
                updatePointScheduleItem(item);
                markScheduleDirty();
            };
            feedModeSelect?.addEventListener('change', onPointChange);
            pointInput?.addEventListener('input', onPointChange);
            updatePointScheduleItem(item);
        }
    });
    syncWorkWindowDependents();
}
