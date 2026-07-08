'use strict';

const FACTORY = Object.freeze({
    lightStart: 10 * 60,
    morningDaybreakEnd: 10 * 60 + 30,
    dayEnd: 20 * 60,
    eveningDaybreakEnd: 21 * 60,
    lightEnd: 22 * 60,
    filterStart: 10 * 60 + 30,
    filterEnd: 20 * 60 + 30,
    gasStart: 10 * 60,
    gasEnd: 19 * 60,
    feedMinute: 14 * 60,
    co2TargetPh: 6.8
});

const ALARMS = Object.freeze({
    temperatureHigh: 1 << 0,
    temperatureLow: 1 << 1,
    phOutOfRange: 1 << 2,
    waterLevelLow: 1 << 3,
    leak: 1 << 4,
    supplyLow: 1 << 5
});

function clamp(value, minimum, maximum) {
    return Math.max(minimum, Math.min(maximum, value));
}

function inWindow(minute, start, end) {
    if (start === end) return false;
    return start < end
        ? minute >= start && minute < end
        : minute >= start || minute < end;
}

function profileAt(minute) {
    if (!inWindow(minute, FACTORY.lightStart, FACTORY.lightEnd)) return null;
    if (minute < FACTORY.morningDaybreakEnd || minute >= FACTORY.dayEnd && minute < FACTORY.eveningDaybreakEnd) {
        return { value: 1, name: 'DAYBREAK' };
    }
    if (minute >= FACTORY.eveningDaybreakEnd) return { value: 2, name: 'NIGHT' };
    return { value: 0, name: 'DAY' };
}

function countBits(value) {
    let current = value >>> 0;
    let count = 0;
    while (current) {
        count += current & 1;
        current >>>= 1;
    }
    return count;
}

class DevSimulator {
    constructor(options = {}) {
        this.startedAt = options.startedAt || Date.now();
        this.clockBase = options.clockBase || new Date(2026, 5, 29, 13, 58, 0).getTime();
        this.randomState = (options.seed || 0x5A17C0DE) >>> 0;
        this.lastStepAt = this.startedAt;
        this.temperature = 25.25;
        this.ph = 6.92;
        this.ec = 442;
        this.heaterOn = false;
        this.manualRelays = Object.create(null);
        this.feedActiveUntil = 0;
        this.lastFeedEpoch = 0;
        this.lastFeedResult = 'brak';
        this.waterFillStartedAt = 0;
        this.waterTimeoutLatched = false;
        this.history = [];
        this.logs = { normal: [], critical: [] };
        this.webSessions = new Map();
        this.webSessionTimeoutMs = 15000;
        this.config = {
            targetTemp: 26.0,
            tempHysteresis: 0.45,
            co2TargetPh: FACTORY.co2TargetPh,
            co2MaxTimeMin: 540,
            heaterMode: 0,
            lightMode: 0,
            lightStart: FACTORY.lightStart,
            lightEnd: FACTORY.lightEnd,
            lightProfile: 0,
            lightProfileCycle: true,
            plantLightMode: 0,
            plantStart: FACTORY.lightStart,
            plantEnd: FACTORY.lightEnd,
            plantLightProfile: 0,
            plantLightProfileCycle: true,
            filterMode: 0,
            filterStart: FACTORY.filterStart,
            filterEnd: FACTORY.filterEnd,
            airMode: 0,
            airStart: FACTORY.gasStart,
            airEnd: FACTORY.gasEnd,
            feedEnabled: true,
            feedMinute: FACTORY.feedMinute,
            soundEnabled: true,
            alwaysScreenOn: false,
            displayAutoBrightness: true,
            displayProfile: 'always_on',
            displayBrightness: 100,
            waterTimeoutSec: 120,
            leakAction: 'disable_all',
            modemSleep: false,
            quietHoursEnabled: true,
            staSsid: 'AkwariumWiFi_5G',
            modules: {
                heater: true,
                ph: true,
                ec: true,
                co2: true,
                aerator: true,
                waterLevel: true,
                leak: true,
                flow: true,
                feeder: true
            }
        };
        this.seedLogs();
        this.seedHistory();
        this.snapshot = this.buildStatus(this.startedAt);
    }

    randomNoise() {
        this.randomState = (Math.imul(this.randomState, 1664525) + 1013904223) >>> 0;
        return ((this.randomState >>> 16) & 0x3FF) / 511.5 - 1;
    }

    deviceDate(now = Date.now()) {
        return new Date(this.clockBase + Math.max(0, now - this.startedAt));
    }

    seedLogs() {
        const epoch = Math.floor(this.deviceDate(this.startedAt).getTime() / 1000);
        this.logs.normal.push(
            { ts: epoch - 3600, level: 'info', code: 'system', message: 'Uruchomiono sterownik w trybie DEV RAM.' },
            { ts: epoch - 3598, level: 'info', code: 'wifi', message: 'Połączono z siecią AkwariumWiFi_5G.' },
            { ts: epoch - 3595, level: 'info', code: 'schedule', message: 'Załadowano fabryczny harmonogram DAY / DAYBREAK / NIGHT.' }
        );
    }

    seedHistory() {
        const endEpoch = Math.floor(this.deviceDate(this.startedAt).getTime() / 1000);
        for (let index = 0; index < 32; index += 1) {
            const phase = index / 5;
            this.history.push({
                value: Number((25.15 + Math.sin(phase) * 0.28).toFixed(2)),
                ph: Number((6.9 + Math.sin(phase * 0.6) * 0.06).toFixed(3)),
                ec: Number((440 + Math.sin(phase * 0.4) * 18).toFixed(1)),
                ldr: Math.round(900 + Math.sin(phase * 0.8) * 250),
                heap: 93684 - index * 8,
                heater: index % 9 < 3,
                epoch: endEpoch - (31 - index) * 60
            });
        }
    }

    scheduleState(date) {
        const minute = date.getHours() * 60 + date.getMinutes();
        const factoryLightWindow = this.config.lightProfileCycle && this.config.lightStart === FACTORY.lightStart && this.config.lightEnd === FACTORY.lightEnd;
        const scheduledProfile = factoryLightWindow
            ? profileAt(minute)
            : (inWindow(minute, this.config.lightStart, this.config.lightEnd)
                ? { value: this.config.lightProfile, name: ['DAY', 'DAYBREAK', 'NIGHT'][this.config.lightProfile] || 'DAY' }
                : null);
        const profile = this.config.lightMode === 1
            ? { value: this.config.lightProfile, name: ['DAY', 'DAYBREAK', 'NIGHT'][this.config.lightProfile] || 'DAY' }
            : (this.config.lightMode === 2 ? null : scheduledProfile);
        const factoryLight2Window = this.config.plantLightProfileCycle && this.config.plantStart === FACTORY.lightStart && this.config.plantEnd === FACTORY.lightEnd;
        const plantScheduledProfile = factoryLight2Window
            ? profileAt(minute)
            : (inWindow(minute, this.config.plantStart, this.config.plantEnd)
                ? { value: this.config.plantLightProfile, name: ['DAY', 'DAYBREAK', 'NIGHT'][this.config.plantLightProfile] || 'DAY' }
                : null);
        const plantProfile = this.config.plantLightMode === 1
            ? { value: this.config.plantLightProfile, name: ['DAY', 'DAYBREAK', 'NIGHT'][this.config.plantLightProfile] || 'DAY' }
            : (this.config.plantLightMode === 2 ? null : plantScheduledProfile);
        const filterScheduled = inWindow(minute, this.config.filterStart, this.config.filterEnd);
        const airScheduled = inWindow(minute, this.config.airStart, this.config.airEnd);
        return {
            minute,
            profile,
            plantProfile,
            lightOn: profile !== null,
            plantLightOn: plantProfile !== null,
            filterOn: this.config.filterMode === 1 ? true : (this.config.filterMode === 2 ? false : filterScheduled),
            aerationWindow: this.config.airMode === 1 ? true : (this.config.airMode === 2 ? false : airScheduled),
            gasWindow: inWindow(minute, FACTORY.gasStart, FACTORY.gasEnd)
        };
    }

    step(now = Date.now()) {
        const dt = clamp((now - this.lastStepAt) / 1000, 0.05, 5);
        this.lastStepAt = now;
        const date = this.deviceDate(now);
        const schedule = this.scheduleState(date);
        const uptime = Math.max(0, Math.floor((now - this.startedAt) / 1000));
        const scenario = uptime % 1800;
        const waterLevelHigh = !(scenario >= 600 && scenario < 625);
        const leakDetected = scenario >= 900 && scenario < 920;
        const supplyLow = scenario >= 1350 && scenario < 1370;
        const supplyVoltage = supplyLow ? 4.55 : 5.01 + Math.sin(uptime / 83) * 0.035;

        const co2On = this.config.modules.co2 && schedule.gasWindow && this.ph > this.config.co2TargetPh && !leakDetected;
        const aerationOn = this.config.modules.aerator && schedule.aerationWindow && !co2On && !leakDetected;
        if (waterLevelHigh) {
            this.waterFillStartedAt = 0;
            this.waterTimeoutLatched = false;
        }
        let waterDosingOn = this.config.modules.waterLevel && !waterLevelHigh && !leakDetected && !this.waterTimeoutLatched;
        if (waterDosingOn) {
            if (!this.waterFillStartedAt) this.waterFillStartedAt = now;
            if (now - this.waterFillStartedAt >= this.config.waterTimeoutSec * 1000) {
                waterDosingOn = false;
                this.waterFillStartedAt = 0;
                this.waterTimeoutLatched = true;
            }
        } else if (!this.waterTimeoutLatched) {
            this.waterFillStartedAt = 0;
        }
        if (this.config.heaterMode === 1 || !this.config.modules.heater) {
            this.heaterOn = false;
        } else if (this.temperature < this.config.targetTemp - this.config.tempHysteresis) {
            this.heaterOn = true;
        } else if (this.temperature >= this.config.targetTemp) {
            this.heaterOn = false;
        }

        const ambient = 24.35 + Math.sin(uptime / 240) * 0.18;
        const thermalRate = this.heaterOn ? 0.01 : (ambient - this.temperature) * 0.004;
        this.temperature = clamp(this.temperature + thermalRate * dt + this.randomNoise() * 0.008, 23.8, 26.2);
        const phTarget = co2On ? 6.72 : 6.94;
        this.ph = clamp(this.ph + (phTarget - this.ph) * 0.006 * dt + this.randomNoise() * 0.0018, 6.55, 7.15);
        this.ec = clamp(442 + Math.sin(uptime / 97) * 21 + this.randomNoise() * 3, 390, 500);

        const ldrBase = !schedule.profile ? 95 : schedule.profile.value === 0 ? 1280 : schedule.profile.value === 1 ? 690 : 330;
        const ldr = Math.round(clamp(ldrBase + this.randomNoise() * 24, 60, 1550));
        let alarmFlags = 0;
        if (this.temperature > 28) alarmFlags |= ALARMS.temperatureHigh;
        if (this.temperature < 20) alarmFlags |= ALARMS.temperatureLow;
        if (this.ph < 6 || this.ph > 8) alarmFlags |= ALARMS.phOutOfRange;
        if (!waterLevelHigh) alarmFlags |= ALARMS.waterLevelLow;
        if (leakDetected) alarmFlags |= ALARMS.leak;
        if (supplyLow) alarmFlags |= ALARMS.supplyLow;

        const epoch = Math.floor(date.getTime() / 1000);
        const last = this.history[this.history.length - 1];
        if (!last || epoch - last.epoch >= 60) {
            this.history.push({
                value: Number(this.temperature.toFixed(2)),
                ph: Number(this.ph.toFixed(3)),
                ec: Number(this.ec.toFixed(1)),
                ldr,
                heap: 93684 - uptime % 512,
                heater: this.heaterOn,
                epoch
            });
            if (this.history.length > 32) this.history.shift();
        }

        const feedActive = now < this.feedActiveUntil;
        if (!feedActive && this.feedActiveUntil !== 0 && this.lastFeedResult === 'w_toku') {
            this.lastFeedResult = 'ok';
        }

        this.snapshot = this.buildStatus(now, {
            date, schedule, uptime, waterLevelHigh, leakDetected, supplyVoltage,
            alarmFlags, ldr, co2On, aerationOn, waterDosingOn, feedActive
        });
        return this.snapshot;
    }

    buildStatus(now, runtime = {}) {
        const date = runtime.date || this.deviceDate(now);
        const schedule = runtime.schedule || this.scheduleState(date);
        const uptime = runtime.uptime ?? Math.max(0, Math.floor((now - this.startedAt) / 1000));
        const profile = schedule.profile || { value: 0, name: 'DAY' };
        const plantProfile = schedule.plantProfile || { value: 0, name: 'DAY' };
        const factoryLightWindow = this.config.lightProfileCycle && this.config.lightStart === FACTORY.lightStart && this.config.lightEnd === FACTORY.lightEnd;
        const factoryLight2Window = this.config.plantLightProfileCycle && this.config.plantStart === FACTORY.lightStart && this.config.plantEnd === FACTORY.lightEnd;
        const relays = {
            light: this.manualRelays.light ?? schedule.lightOn,
            plantLight: this.manualRelays.plantLight ?? schedule.plantLightOn,
            pump: this.manualRelays.pump ?? schedule.filterOn,
            heater: this.heaterOn,
            co2: runtime.co2On ?? false,
            aeration: this.manualRelays.aeration ?? runtime.aerationOn ?? false,
            waterDosing: runtime.waterDosingOn ?? false,
            aerationPercent: runtime.aerationOn ? 100 : 0
        };
        const alarmFlags = runtime.alarmFlags || 0;
        const history = this.history.map((sample) => ({ value: sample.value, epoch: sample.epoch }));
        const batteryVoltage = 3.25 + Math.sin(uptime / 310) * 0.025;
        const batteryPercent = Math.round(clamp(80 + Math.sin(uptime / 420) * 7, 0, 100));

        return {
            device: 'cydAkwarium',
            mode: 'STA_SERVICE',
            portal_ip: '127.0.0.1',
            ip: '127.0.0.1',
            portal_url: 'http://127.0.0.1:8000/',
            portal_domain: 'http://akwarium.local/',
            hostname: 'akwarium',
            theme: 'light',
            theme_light: true,
            ldr_auto: false,
            manual_light_theme: true,
            clients: this.activeWebSessions(now),
            heap_free: 93684 - uptime % 512,
            heap_largest: 90100,
            sd_mounted: true,
            sd_total_bytes: 8 * 1024 * 1024 * 1024,
            sd_used_bytes: 48 * 1024 * 1024,
            sd_free_bytes: 8 * 1024 * 1024 * 1024 - 48 * 1024 * 1024,
            history_points: history.length,
            uptime_ms: uptime * 1000,
            ota_active: false,
            sensors: {
                temp_c: Number(this.temperature.toFixed(2)), temp_valid: true,
                ph: Number(this.ph.toFixed(3)), ph_valid: true,
                ec: Number(this.ec.toFixed(1)), ec_valid: true,
                ldr: runtime.ldr ?? 900, ldr_valid: true,
                mcp_present: true, mcp_valid: true, mcp_ok: true,
                water_level_high: runtime.waterLevelHigh ?? true, water_level_valid: true,
                leak_detected: runtime.leakDetected ?? false, leak_valid: true,
                flow_active: schedule.filterOn && Math.floor(uptime / 4) % 2 === 0, flow_valid: true,
                supply_voltage: Number((runtime.supplyVoltage ?? 5.01).toFixed(2)), supply_valid: true
            },
            alarms: {
                flags: alarmFlags,
                activeCount: countBits(alarmFlags),
                temperatureHigh: !!(alarmFlags & ALARMS.temperatureHigh),
                temperatureLow: !!(alarmFlags & ALARMS.temperatureLow),
                phOutOfRange: !!(alarmFlags & ALARMS.phOutOfRange),
                waterLevelLow: !!(alarmFlags & ALARMS.waterLevelLow),
                leak: !!(alarmFlags & ALARMS.leak),
                supplyLow: !!(alarmFlags & ALARMS.supplyLow)
            },
            config: {
                target_temp: this.config.targetTemp,
                temp_hysteresis: this.config.tempHysteresis,
                co2TargetPh: this.config.co2TargetPh,
                co2MaxTimeMin: this.config.co2MaxTimeMin,
                dev_mode: true,
                modem_sleep: this.config.modemSleep,
                always_screen_on: this.config.alwaysScreenOn,
                sound_enabled: this.config.soundEnabled,
                quiet_hours_enabled: this.config.quietHoursEnabled,
                quiet_start: '20:00', quiet_end: '10:00'
            },
            display: {
                autoBrightness: this.config.displayAutoBrightness,
                profile: this.config.displayProfile,
                brightness: this.config.displayBrightness,
                appliedBrightness: this.config.displayBrightness
            },
            water: {
                timeoutSec: this.config.waterTimeoutSec,
                active: relays.waterDosing,
                timeoutLatched: this.waterTimeoutLatched,
                runtimeSec: this.waterFillStartedAt ? Math.max(0, Math.floor((now - this.waterFillStartedAt) / 1000)) : 0
            },
            leak: { action: this.config.leakAction },
            modules: {
                light_on: relays.light, plant_light_on: relays.plantLight,
                light1_on: relays.light, light2_on: relays.plantLight, filter_on: relays.pump,
                air_on: relays.aeration, co2_on: relays.co2, heater_on: relays.heater,
                heater_enabled: this.config.modules.heater, ph_sensor_enabled: this.config.modules.ph,
                co2_enabled: this.config.modules.co2, ec_enabled: this.config.modules.ec,
                water_level_enabled: this.config.modules.waterLevel, water_dosing_on: relays.waterDosing,
                leak_enabled: this.config.modules.leak,
                flow_enabled: this.config.modules.flow, feeder_enabled: this.config.modules.feeder
            },
            schedules: {
                light: { mode: this.config.lightMode, start: this.formatMinute(this.config.lightStart), end: this.formatMinute(this.config.lightEnd), active: relays.light, profile: profile.value, profileName: profile.name, profileCycle: factoryLightWindow },
                plant_light: { mode: this.config.plantLightMode, start: this.formatMinute(this.config.plantStart), end: this.formatMinute(this.config.plantEnd), active: relays.plantLight, profile: plantProfile.value, profileName: plantProfile.name, profileCycle: factoryLight2Window },
                filter: { mode: this.config.filterMode, start: this.formatMinute(this.config.filterStart), end: this.formatMinute(this.config.filterEnd), active: relays.pump },
                air: { mode: this.config.airMode, start: this.formatMinute(this.config.airStart), end: this.formatMinute(this.config.airEnd), active: relays.aeration },
                feeder: { enabled: this.config.feedEnabled, count: this.config.feedEnabled ? 1 : 0, time1: this.formatMinute(this.config.feedMinute), time2: '08:00' }
            },
            eco: { safe_active: false, quiet_window: false, deep_ready: false, rtc_ready: true, wake_after_sec: 0, last_wake_cause: 0, blockers: ['dev_mode'] },
            clock: {
                year: date.getFullYear(), month: date.getMonth() + 1, day: date.getDate(),
                hour: date.getHours(), minute: date.getMinutes(), second: date.getSeconds(),
                valid: true, source: 'DEV_RAM', staRetryCooldownMs: 0
            },
            temperature: { current: Number(this.temperature.toFixed(2)), target: this.config.targetTemp, hysteresis: this.config.tempHysteresis, historyCapacity: 32, historyIntervalMinutes: 1, history, heaterMode: this.config.heaterMode },
            battery: { voltage: Number(batteryVoltage.toFixed(2)), percent: batteryPercent },
            firmware: { version: 'dev-simulator', buildDate: 'Jun 29 2026', buildTime: '12:00:00' },
            network: {
                staConnected: true, staConnecting: false, apMode: false, serviceMode: true,
                serviceModePending: false, staSsid: this.config.staSsid, configuredStaSsid: this.config.staSsid,
                configuredApSsid: 'cydAkwarium-OTA', ssid: this.config.staSsid,
                ip: '127.0.0.1', rssi: -47, clients: this.activeWebSessions(now),
                lastTimeSyncOk: true, lastTimeSyncStatus: 'DEV_RAM'
            },
            web: {
                focus: this.activeWebSessions(now) > 0,
                activeClients: this.activeWebSessions(now),
                lastSeenMs: 0,
                timeoutMs: this.webSessionTimeoutMs,
                cpuProfile: this.activeWebSessions(now) > 0 ? 'web_sensor_control' : 'local_ui',
                localUiDeferred: this.activeWebSessions(now) > 0,
                sensorControlIntervalMs: 1000
            },
            system: { uptime, powerMode: 'normal', resetReason: '1', freeHeap: 93684 - uptime % 512, largestHeap: 90100, mcpConnected: true, i2cConnected: true },
            relays: { ...relays, light1: relays.light, light2: relays.plantLight },
            lights: {
                light1: { on: relays.light, profile: ['day', 'daybreak', 'night'][profile.value] || 'day', profileName: profile.name, profileCycle: factoryLightWindow },
                light2: { on: relays.plantLight, profile: ['day', 'daybreak', 'night'][plantProfile.value] || 'day', profileName: plantProfile.name, profileCycle: factoryLight2Window },
                supportedProfiles: ['day', 'daybreak', 'night']
            },
            schedule: {
                lightMode: this.config.lightMode, dayStartHour: Math.floor(this.config.lightStart / 60), dayStartMin: this.config.lightStart % 60, dayEndHour: Math.floor(this.config.lightEnd / 60), dayEndMin: this.config.lightEnd % 60,
                airMode: this.config.airMode, airStartHour: Math.floor(this.config.airStart / 60), airStartMin: this.config.airStart % 60, airEndHour: Math.floor(this.config.airEnd / 60), airEndMin: this.config.airEnd % 60,
                filterMode: this.config.filterMode, filterStartHour: Math.floor(this.config.filterStart / 60), filterStartMin: this.config.filterStart % 60, filterEndHour: Math.floor(this.config.filterEnd / 60), filterEndMin: this.config.filterEnd % 60,
                heaterMode: this.config.heaterMode, lightProfile: profile.value, lightProfileName: profile.name,
                plantLightMode: this.config.plantLightMode, plantStartHour: Math.floor(this.config.plantStart / 60), plantStartMin: this.config.plantStart % 60, plantEndHour: Math.floor(this.config.plantEnd / 60), plantEndMin: this.config.plantEnd % 60,
                plantLightProfile: this.config.plantLightProfile, plantLightProfileName: ['DAY', 'DAYBREAK', 'NIGHT'][this.config.plantLightProfile] || 'DAY'
            },
            feeding: { active: runtime.feedActive ?? false, freq: this.config.feedEnabled ? 1 : 0, hour: Math.floor(this.config.feedMinute / 60), minute: this.config.feedMinute % 60, lastFeedEpoch: this.lastFeedEpoch, lastResult: this.lastFeedResult }
        };
    }

    formatMinute(value) {
        const minute = Math.max(0, Math.min(1439, Number(value) || 0));
        return `${String(Math.floor(minute / 60)).padStart(2, '0')}:${String(minute % 60).padStart(2, '0')}`;
    }

    addLog(message, critical = false, now = Date.now()) {
        const entry = { ts: Math.floor(this.deviceDate(now).getTime() / 1000), level: critical ? 'critical' : 'info', code: critical ? 'alarm' : 'action', message };
        const target = critical ? this.logs.critical : this.logs.normal;
        target.unshift(entry);
        if (target.length > 50) target.length = 50;
    }

    logPayload() {
        return { normal: this.logs.normal, critical: this.logs.critical, counts: { normal: this.logs.normal.length, critical: this.logs.critical.length } };
    }

    touchWebSession(sessionId, now = Date.now()) {
        if (!/^[A-Za-z0-9_-]{6,24}$/.test(sessionId || '')) return false;
        this.webSessions.set(sessionId, now);
        this.activeWebSessions(now);
        return true;
    }

    closeWebSession(sessionId) {
        return this.webSessions.delete(sessionId);
    }

    activeWebSessions(now = Date.now()) {
        for (const [id, seenAt] of this.webSessions) {
            if (now - seenAt > this.webSessionTimeoutMs) this.webSessions.delete(id);
        }
        return this.webSessions.size;
    }

    triggerFeed(now = Date.now()) {
        if (now < this.feedActiveUntil) return { success: false, code: 'feed_busy', message: 'Karmnik jest zajęty.' };
        this.feedActiveUntil = now + 3000;
        this.lastFeedEpoch = Math.floor(this.deviceDate(now).getTime() / 1000);
        this.lastFeedResult = 'w_toku';
        this.addLog('Karmienie DEV uruchomione z panelu WWW.', false, now);
        return { success: true, code: 'feed_started', message: 'Karmienie DEV uruchomione.' };
    }
}

module.exports = { ALARMS, FACTORY, DevSimulator, inWindow, profileAt };
