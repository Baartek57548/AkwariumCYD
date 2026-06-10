function scheduleModeToUi(value) {
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

function clearScheduleDirtyState() {
    document.querySelectorAll('#harmonogramy [data-dirty="1"]').forEach((el) => {
        el.dataset.dirty = '0';
    });
}

function markScheduleDirty() {
    setScheduleStatus('Masz niezapisane zmiany harmonogramow.', 'muted');
}

function renderScheduleEditor(data) {
    const schedule = data.schedule || {};
    const feeding = data.feeding || {};

    setInputValueIfClean('schedule-light-mode', scheduleModeToUi(schedule.lightMode));
    setInputValueIfClean('schedule-light-start', formatTime(schedule.dayStartHour, schedule.dayStartMin));
    setInputValueIfClean('schedule-light-end', formatTime(schedule.dayEndHour, schedule.dayEndMin));

    setInputValueIfClean('schedule-air-mode', scheduleModeToUi(schedule.airMode));
    setInputValueIfClean('schedule-air-start', formatTime(schedule.airStartHour, schedule.airStartMin));
    setInputValueIfClean('schedule-air-end', formatTime(schedule.airEndHour, schedule.airEndMin));

    setInputValueIfClean('schedule-filter-mode', scheduleModeToUi(schedule.filterMode));
    setInputValueIfClean('schedule-filter-start', formatTime(schedule.filterStartHour, schedule.filterStartMin));
    setInputValueIfClean('schedule-filter-end', formatTime(schedule.filterEndHour, schedule.filterEndMin));

    setInputValueIfClean('schedule-feed-mode', feedFrequencyToUi(feeding.freq));
    setInputValueIfClean('schedule-feed-time', formatTime(feeding.hour, feeding.minute));

    document.querySelectorAll('#harmonogramy .schedule-item').forEach((item) => {
        const kind = item.getAttribute('data-schedule-kind') || 'point';
        if (kind === 'range') {
            updateRangeScheduleItem(item);
        } else {
            updatePointScheduleItem(item);
        }
    });

    if (!document.querySelector('#harmonogramy [data-dirty="1"]')) {
        setScheduleStatus('Harmonogramy zsynchronizowane ze sterownikiem.', 'success');
    }
}

async function saveScheduleSettings() {
    const button = document.getElementById('save-schedule-btn');
    if (!button) return;

    const payload = {
        lightMode: scheduleModeToApi(document.getElementById('schedule-light-mode')?.value),
        dayStart: document.getElementById('schedule-light-start')?.value || '00:00',
        dayEnd: document.getElementById('schedule-light-end')?.value || '00:00',
        aerationMode: scheduleModeToApi(document.getElementById('schedule-air-mode')?.value),
        airOn: document.getElementById('schedule-air-start')?.value || '00:00',
        airOff: document.getElementById('schedule-air-end')?.value || '00:00',
        filterMode: scheduleModeToApi(document.getElementById('schedule-filter-mode')?.value),
        filterOn: document.getElementById('schedule-filter-start')?.value || '00:00',
        filterOff: document.getElementById('schedule-filter-end')?.value || '00:00',
        feedFreq: feedFrequencyToApi(document.getElementById('schedule-feed-mode')?.value),
        feedTime: document.getElementById('schedule-feed-time')?.value || '00:00'
    };

    button.disabled = true;
    setScheduleStatus('Zapisywanie harmonogramow...', 'muted');

    try {
        const response = await sendAction('save_schedule', payload);
        clearScheduleDirtyState();
        setScheduleStatus(
            response?.code === 'settings_partial'
                ? 'Zapisano harmonogramy po korekcie niektorych wartosci.'
                : 'Zapisano harmonogramy.',
            response?.code === 'settings_partial' ? 'warning' : 'success'
        );
        await fetchStatus(true);
    } catch (error) {
        setScheduleStatus(`Blad zapisu harmonogramow: ${error?.message || 'nieznany blad'}`, 'error');
    } finally {
        button.disabled = false;
    }
}

function timeToMinutes(time) {
    const [hours, minutes] = (time || '00:00').split(':').map(Number);
    return ((Number.isFinite(hours) ? hours : 0) * 60) + (Number.isFinite(minutes) ? minutes : 0);
}

function minutesToPercent(totalMinutes) {
    return Math.max(0, Math.min(100, (totalMinutes / (24 * 60)) * 100));
}

function updateRangeScheduleItem(item) {
    const modeSelect = item.querySelector('.schedule-mode-select');
    const startInput = item.querySelector('.schedule-time-start');
    const endInput = item.querySelector('.schedule-time-end');
    const bar = item.querySelector('.schedule-bar');
    if (!modeSelect || !startInput || !endInput || !bar) return;

    const mode = modeSelect.value;
    const startMinutes = timeToMinutes(startInput.value);
    let endMinutes = timeToMinutes(endInput.value);
    if (endMinutes < startMinutes) {
        endMinutes = startMinutes;
        endInput.value = startInput.value;
    }

    startInput.disabled = mode !== 'harmonogram';
    endInput.disabled = mode !== 'harmonogram';

    const startPct = minutesToPercent(startMinutes);
    let endPct = minutesToPercent(endMinutes);

    if (mode === 'zawsze_wlaczone') {
        endPct = 100;
        bar.style.left = '0%';
        bar.style.width = '100%';
    } else if (mode === 'zawsze_wylaczone') {
        bar.style.left = '0%';
        bar.style.width = '0%';
    } else {
        bar.style.left = `${startPct}%`;
        bar.style.width = `${Math.max(0, endPct - startPct)}%`;
    }

    startInput.style.left = `${startPct}%`;
    endInput.style.left = `${endPct}%`;
}

function updatePointScheduleItem(item) {
    const pointInput = item.querySelector('.schedule-time-point');
    const point = item.querySelector('.schedule-point');
    if (!pointInput || !point) return;

    const pointPct = minutesToPercent(timeToMinutes(pointInput.value));
    point.style.left = `${pointPct}%`;
    pointInput.style.left = `${pointPct}%`;
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
                updateRangeScheduleItem(item);
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
}
