/**
 * relays-wizard.js - cydAkwarium 8CH Relay Configuration Wizard
 */

(function () {
    let currentStep = 1;
    const totalSteps = 5;

    // Domyślna konfiguracja
    let relaysConfig = {
        relayBoard: {
            enabled: true,
            channels: 8,
            expander: "MCP23017",
            i2cAddress: "0x20",
            activeLow: true,
            storage: "sd"
        },
        relays: [
            { channel: 1, function: "filter", label: "Filtr główny", defaultState: "auto", safeState: "on", manualAllowed: true, pinRequired: false },
            { channel: 2, function: "heater", label: "Grzałka", defaultState: "auto", safeState: "off", manualAllowed: true, pinRequired: true },
            { channel: 3, function: "main_light", label: "Światło 1", defaultState: "auto", safeState: "off", manualAllowed: true, pinRequired: false },
            { channel: 4, function: "plant_light", label: "Światło 2", defaultState: "auto", safeState: "off", manualAllowed: true, pinRequired: false },
            { channel: 5, function: "aeration", label: "Napowietrzanie", defaultState: "auto", safeState: "off", manualAllowed: true, pinRequired: false },
            { channel: 6, function: "co2", label: "Elektrozawór CO2", defaultState: "auto", safeState: "off", manualAllowed: true, pinRequired: true },
            { channel: 7, function: "water_dosing", label: "Dolewka wody ATO", defaultState: "auto", safeState: "off", manualAllowed: true, pinRequired: false },
            { channel: 8, function: "reserve", label: "Rezerwa", defaultState: "off", safeState: "off", manualAllowed: true, pinRequired: false }
        ]
    };

    const RELAY_FUNCTIONS = [
        { value: "none", label: "Brak / nieużywany", icon: "fa-xmark", critical: false },
        { value: "filter", label: "Filtr", icon: "fa-filter", critical: true },
        { value: "heater", label: "Grzałka", icon: "fa-temperature-half", critical: true },
        { value: "main_light", label: "Światło 1", icon: "fa-lightbulb", critical: false },
        { value: "plant_light", label: "Światło 2", icon: "fa-lightbulb", critical: false },
        { value: "aeration", label: "Napowietrzanie", icon: "fa-wind", critical: false },
        { value: "co2", label: "CO2", icon: "fa-cloud", critical: true },
        { value: "water_dosing", label: "Dolewka wody (ATO)", icon: "fa-droplet", critical: true },
        { value: "feeder", label: "Karmnik", icon: "fa-fish", critical: false },
        { value: "circulation_pump", label: "Pompa obiegowa", icon: "fa-rotate-right", critical: false },
        { value: "uv_lamp", label: "Lampa UV", icon: "fa-sun", critical: false },
        { value: "reserve", label: "Rezerwa", icon: "fa-sliders", critical: false },
        { value: "custom", label: "Własna nazwa...", icon: "fa-sliders", critical: false }
    ];

    function getFunctionInfo(value) {
        return RELAY_FUNCTIONS.find(f => f.value === value) || RELAY_FUNCTIONS[0];
    }

    function buildFirmwareDefaultMap() {
        return [
            { channel: 1, function: "main_light", label: "Światło 1", defaultState: "auto", safeState: "off", manualAllowed: true, pinRequired: false },
            { channel: 2, function: "plant_light", label: "Światło 2", defaultState: "auto", safeState: "off", manualAllowed: true, pinRequired: false },
            { channel: 3, function: "filter", label: "Filtr", defaultState: "auto", safeState: "on", manualAllowed: true, pinRequired: false },
            { channel: 4, function: "aeration", label: "Napowietrzanie", defaultState: "auto", safeState: "off", manualAllowed: true, pinRequired: false },
            { channel: 5, function: "heater", label: "Grzałka", defaultState: "auto", safeState: "off", manualAllowed: true, pinRequired: true },
            { channel: 6, function: "co2", label: "Elektrozawór CO2", defaultState: "auto", safeState: "off", manualAllowed: true, pinRequired: true },
            { channel: 7, function: "feeder", label: "Karmnik", defaultState: "off", safeState: "off", manualAllowed: true, pinRequired: true },
            { channel: 8, function: "water_dosing", label: "Dolewka ATO", defaultState: "auto", safeState: "off", manualAllowed: false, pinRequired: true }
        ];
    }

    const FIRMWARE_RELAY_MAP = buildFirmwareDefaultMap();
    const MAX_RELAY_CONFIG_FILE_BYTES = 32 * 1024;
    relaysConfig.relays = FIRMWARE_RELAY_MAP.map((relay) => ({ ...relay }));

    function normalizeBoolean(value, fallback) {
        return typeof value === 'boolean' ? value : fallback;
    }

    function normalizeEnum(value, allowed, fallback) {
        const normalized = String(value || '').trim();
        return allowed.has(normalized) ? normalized : fallback;
    }

    function normalizeRelayLabel(value, fallback) {
        const normalized = String(value ?? '')
            .replace(/[\u0000-\u001F\u007F]/g, ' ')
            .replace(/\s+/g, ' ')
            .trim()
            .slice(0, 48);
        return normalized || fallback;
    }

    function normalizeRelayConfig(input) {
        if (!input || typeof input !== 'object' || !Array.isArray(input.relays)) {
            throw new Error('Profil nie zawiera tablicy przekaźników.');
        }
        if (input.relays.length < 1 || input.relays.length > 8) {
            throw new Error('Profil musi zawierać od 1 do 8 unikalnych kanałów.');
        }

        const allowedFunctions = new Set(RELAY_FUNCTIONS.map((item) => item.value));
        const allowedDefaultStates = new Set(['auto', 'on', 'off']);
        const allowedSafeStates = new Set(['off', 'on', 'keep']);
        const normalizedByChannel = new Map();

        input.relays.forEach((relay, index) => {
            if (!relay || typeof relay !== 'object') {
                throw new Error(`Kanał na pozycji ${index + 1} ma nieprawidłowy format.`);
            }

            const channel = Number(relay.channel);
            if (!Number.isInteger(channel) || channel < 1 || channel > 8) {
                throw new Error(`Kanał na pozycji ${index + 1} musi być liczbą od 1 do 8.`);
            }
            if (normalizedByChannel.has(channel)) {
                throw new Error(`Kanał ${channel} występuje w profilu więcej niż raz.`);
            }

            const functionName = String(relay.function || 'none').trim();
            if (!allowedFunctions.has(functionName)) {
                throw new Error(`Kanał ${channel} zawiera nieobsługiwaną funkcję.`);
            }

            const firmwareFallback = FIRMWARE_RELAY_MAP[channel - 1];
            const functionInfo = getFunctionInfo(functionName);
            normalizedByChannel.set(channel, {
                channel,
                function: functionName,
                label: normalizeRelayLabel(relay.label, functionInfo.label || `Kanał ${channel}`),
                defaultState: normalizeEnum(
                    relay.defaultState,
                    allowedDefaultStates,
                    firmwareFallback?.defaultState || 'off'
                ),
                safeState: normalizeEnum(
                    relay.safeState,
                    allowedSafeStates,
                    firmwareFallback?.safeState || 'off'
                ),
                manualAllowed: normalizeBoolean(
                    relay.manualAllowed,
                    firmwareFallback?.manualAllowed ?? false
                ),
                pinRequired: normalizeBoolean(
                    relay.pinRequired,
                    firmwareFallback?.pinRequired ?? true
                )
            });
        });

        const relays = Array.from({ length: 8 }, (_, index) => {
            const channel = index + 1;
            return normalizedByChannel.get(channel) || {
                channel,
                function: 'none',
                label: `Kanał ${channel}`,
                defaultState: 'off',
                safeState: 'off',
                manualAllowed: false,
                pinRequired: true
            };
        });

        const board = input.relayBoard && typeof input.relayBoard === 'object'
            ? input.relayBoard
            : {};
        const startDelay = Number(board.startDelay);
        const atoLimit = Number(input.atoLimit);

        return {
            relayBoard: {
                enabled: normalizeBoolean(board.enabled, true),
                channels: 8,
                expander: 'MCP23017',
                i2cAddress: /^0x[0-9a-f]{2}$/i.test(String(board.i2cAddress || ''))
                    ? String(board.i2cAddress).toLowerCase()
                    : '0x20',
                activeLow: normalizeBoolean(board.activeLow, true),
                storage: board.storage === 'internal' ? 'internal' : 'sd',
                startDelay: Number.isFinite(startDelay)
                    ? Math.max(0, Math.min(10000, Math.trunc(startDelay)))
                    : 500
            },
            relays,
            atoLimit: Number.isFinite(atoLimit)
                ? Math.max(5, Math.min(300, Math.trunc(atoLimit)))
                : 30
        };
    }

    function relayRuntimeState(functionName, status) {
        const relays = status?.relays || {};
        const mapping = {
            main_light: relays.light,
            plant_light: relays.light2 ?? relays.plantLight,
            filter: relays.pump,
            aeration: relays.aeration,
            heater: relays.heater,
            co2: relays.co2,
            water_dosing: status?.modules?.water_dosing_on ?? status?.water?.active ?? relays.waterDosing,
            feeder: status?.feeding?.active
        };
        return Object.prototype.hasOwnProperty.call(mapping, functionName)
            ? Boolean(mapping[functionName])
            : null;
    }

    function setModuleCommand(id, value, detail, tone) {
        const valueElement = document.getElementById(id);
        const detailElement = document.getElementById(`${id}-detail`);
        if (valueElement) {
            valueElement.textContent = value;
            valueElement.closest('.module-command-card')?.setAttribute('data-tone', tone || 'neutral');
        }
        if (detailElement) detailElement.textContent = detail;
    }

    function setModuleMessage(message, tone = 'neutral') {
        const element = document.getElementById('module-map-warning');
        if (!element) return;
        element.textContent = message;
        element.setAttribute('data-tone', tone);
    }

    function validateRelayMap(relays = relaysConfig.relays) {
        const warnings = [];
        ['filter', 'heater', 'co2', 'water_dosing'].forEach((functionName) => {
            const matches = relays.filter((relay) => relay.function === functionName);
            if (matches.length > 1) warnings.push(`Funkcja ${getFunctionInfo(functionName).label} ma kilka kanałów.`);
        });
        relays.forEach((relay) => {
            if (['heater', 'co2', 'water_dosing'].includes(relay.function) && relay.safeState !== 'off') {
                warnings.push(`CH${relay.channel} ${relay.label}: stan awaryjny powinien być OFF.`);
            }
        });
        return warnings;
    }

    function renderRelayModuleOverview(status = window.lastStatusData || {}) {
        const system = status.system || {};
        const mcpOk = status.sensors?.mcp_ok ?? system.mcpConnected ?? system.i2cConnected ?? null;
        const activeCount = FIRMWARE_RELAY_MAP.reduce((count, relay) =>
            count + (relayRuntimeState(relay.function, status) === true ? 1 : 0), 0);
        const warnings = validateRelayMap(FIRMWARE_RELAY_MAP);

        setModuleCommand('module-overview-i2c', mcpOk === true ? 'ONLINE' : (mcpOk === false ? 'BRAK' : 'OCZEKUJE'),
            mcpOk === true ? 'MCP23017 odpowiada pod 0x20' : 'Brak potwierdzonej odpowiedzi magistrali',
            mcpOk === true ? 'ok' : (mcpOk === false ? 'danger' : 'warn'));
        setModuleCommand('module-overview-map', '8 kanałów', '8 funkcji firmware', 'ok');
        setModuleCommand('module-overview-active', `${activeCount} / 8`, 'Stan z ostatniej telemetrii', activeCount > 0 ? 'ok' : 'neutral');
        setModuleCommand('module-overview-safety', warnings.length ? `${warnings.length} UWAG` : 'GOTOWE',
            warnings.length ? warnings[0] : 'Stany awaryjne funkcji krytycznych są poprawne', warnings.length ? 'warn' : 'ok');

        const grid = document.getElementById('module-channel-grid');
        if (grid) {
            grid.innerHTML = FIRMWARE_RELAY_MAP.map((relay) => {
                const info = getFunctionInfo(relay.function);
                const runtimeState = relayRuntimeState(relay.function, status);
                const stateText = runtimeState === null ? 'REZERWA' : (runtimeState ? 'ON' : 'OFF');
                const defaultText = relay.defaultState === 'auto' ? 'harmonogram/automatyka' : relay.defaultState.toUpperCase();
                return `<article class="module-channel-card" data-critical="${info.critical ? 'true' : 'false'}">
                    <div class="module-channel-head"><span class="module-channel-number">CH ${relay.channel}</span><span class="module-channel-state" data-active="${runtimeState === true ? 'true' : 'false'}">${stateText}</span></div>
                    <h3>${window.escapeHtml(relay.label || info.label)}</h3>
                    <p>${window.escapeHtml(info.label)} · start: ${window.escapeHtml(defaultText)}</p>
                    <div class="module-channel-failsafe">Awaria: <strong>${window.escapeHtml(String(relay.safeState || 'off').toUpperCase())}</strong></div>
                </article>`;
            }).join('');
        }

        const warningElement = document.getElementById('module-map-warning');
        if (warningElement) {
            warningElement.textContent = warnings.length ? warnings.join(' ') : 'Mapa kanałów jest spójna z profilem bezpieczeństwa firmware.';
            warningElement.setAttribute('data-tone', warnings.length ? 'warn' : 'ok');
        }
    }

    // Inicjalizacja i nasłuchiwanie zdarzeń
    function initWizard() {
        // Podpięcie przycisków nawigacji
        document.getElementById("wizard-prev-btn")?.addEventListener("click", prevStep);
        document.getElementById("wizard-next-btn")?.addEventListener("click", nextStep);
        document.getElementById("wizard-save-btn")?.addEventListener("click", saveWizardConfig);
        document.getElementById("wizard-default-map-btn")?.addEventListener("click", restoreDefaultMap);
        document.getElementById("wizard-export-btn")?.addEventListener("click", exportConfigJson);
        document.getElementById("wizard-import-file")?.addEventListener("change", importConfigJson);
        document.getElementById("module-open-wizard")?.addEventListener("click", () => {
            goToStep(1);
            document.getElementById("relays-wizard-card")?.scrollIntoView({ behavior: "smooth", block: "start" });
        });

        // Nasłuchiwanie zmian konfiguracji sprzętowej w kroku 1
        document.getElementById("wizard-expander")?.addEventListener("change", (e) => {
            relaysConfig.relayBoard.expander = e.target.value;
        });
        document.getElementById("wizard-active-low")?.addEventListener("change", (e) => {
            relaysConfig.relayBoard.activeLow = e.target.value === "1";
        });
        document.getElementById("wizard-start-delay")?.addEventListener("change", (e) => {
            relaysConfig.relayBoard.startDelay = parseInt(e.target.value) || 0;
        });

        // Haczyk do menu sidebaru
        const navItems = document.querySelectorAll('.nav-item[data-target]');
        navItems.forEach(item => {
            if (item.getAttribute('data-target') === 'przekazniki') {
                item.addEventListener('click', () => {
                    syncWizardWithBackend();
                });
            }
        });
    }

    // Synchronizacja z danymi z ESP32
    function syncWizardWithBackend() {
        // Pobierz aktualny status i sprawdź czy zawiera konfigurację przekaźników
        const status = window.lastStatusData || {};
        const system = status.system || {};
        const sdMounted = status.sd_mounted ?? false;

        // Karta SD
        const storageLabel = document.getElementById("wizard-storage-label");
        const storageDesc = document.getElementById("wizard-storage-desc");
        if (storageLabel && storageDesc) {
            if (sdMounted) {
                storageLabel.textContent = "Karta SD (Wariant A)";
                storageLabel.style.color = "var(--success-color)";
                storageDesc.textContent = "Konfiguracja zostanie zapisana w pliku /config/relays.json na karcie SD.";
                relaysConfig.relayBoard.storage = "sd";
            } else {
                storageLabel.textContent = "Pamięć wewnętrzna Flash (Wariant B)";
                storageLabel.style.color = "var(--accent-cyan)";
                storageDesc.textContent = "Brak karty SD. Konfiguracja zostanie zapisana w LittleFS / Preferences.";
                relaysConfig.relayBoard.storage = "internal";
            }
        }

        // Status I2C expandera
        const i2cStatus = document.getElementById("wizard-i2c-status");
        const i2cAddress = document.getElementById("wizard-i2c-address");
        const mcpOk = status.sensors?.mcp_ok ?? system.mcpConnected ?? system.i2cConnected ?? null;

        if (i2cStatus && i2cAddress) {
            if (mcpOk === true) {
                i2cStatus.textContent = "POŁĄCZONO";
                i2cStatus.style.color = "var(--success-color)";
                i2cAddress.textContent = system.mcpAddress || relaysConfig.relayBoard.i2cAddress || "0x20";
            } else if (mcpOk === null) {
                i2cStatus.textContent = "OCZEKIWANIE";
                i2cStatus.style.color = "var(--warning-color)";
                i2cAddress.textContent = relaysConfig.relayBoard.i2cAddress || "0x20";
            } else {
                i2cStatus.textContent = "BŁĄD POŁĄCZENIA";
                i2cStatus.style.color = "var(--danger-color)";
                i2cAddress.textContent = "0x--";
            }
        }

        // Zaciągnij konfigurację jeśli backend ją przysłał
        if (status.relaysConfig) {
            try {
                const parsed = typeof status.relaysConfig === 'string' ? JSON.parse(status.relaysConfig) : status.relaysConfig;
                relaysConfig = normalizeRelayConfig(parsed);
                // zaktualizuj inputy w Kroku 1
                window.setValue("wizard-expander", relaysConfig.relayBoard.expander || "MCP23017");
                window.setValue("wizard-active-low", relaysConfig.relayBoard.activeLow ? "1" : "0");
                window.setValue("wizard-start-delay", relaysConfig.relayBoard.startDelay ?? 500);
            } catch (e) {
                console.warn("Błąd parsowania relaysConfig ze sterownika", e);
            }
        }

        renderRelayModuleOverview(status);

        // Rozpocznij od kroku 1
        goToStep(1);
    }

    // Nawigacja po krokach
    function goToStep(step) {
        if (step < 1 || step > totalSteps) return;

        // Chowanie/Pokazywanie paneli
        for (let i = 1; i <= totalSteps; i++) {
            const panel = document.getElementById(`wizard-step-${i}`);
            const ind = document.getElementById(`step-ind-${i}`);
            if (panel) {
                panel.style.display = (i === step) ? "block" : "none";
            }
            if (ind) {
                ind.classList.toggle("active", i === step);
                ind.style.color = (i === step) ? "var(--accent-cyan)" : (i < step ? "var(--success-color)" : "var(--text-muted)");
            }
        }

        currentStep = step;

        // Obsługa przycisków
        const prevBtn = document.getElementById("wizard-prev-btn");
        const nextBtn = document.getElementById("wizard-next-btn");
        const saveBtn = document.getElementById("wizard-save-btn");
        const defaultBtn = document.getElementById("wizard-default-map-btn");

        if (prevBtn) prevBtn.disabled = (currentStep === 1);
        if (nextBtn) nextBtn.style.display = (currentStep === totalSteps) ? "none" : "block";
        if (saveBtn) saveBtn.style.display = (currentStep === totalSteps) ? "block" : "none";
        if (defaultBtn) defaultBtn.style.display = (currentStep === 2) ? "block" : "none";

        // Renderowanie kroku
        if (currentStep === 2) {
            renderStep2();
        } else if (currentStep === 3) {
            renderStep3();
        } else if (currentStep === 4) {
            renderStep4();
        } else if (currentStep === 5) {
            renderStep5();
        }
    }

    function nextStep() {
        if (currentStep < totalSteps) {
            if (validateStep(currentStep)) {
                goToStep(currentStep + 1);
            }
        }
    }

    function prevStep() {
        if (currentStep > 1) {
            goToStep(currentStep - 1);
        }
    }

    // Walidacja bieżącego kroku
    function validateStep(step) {
        if (step === 2) {
            // Przeczytaj wartości z dropdownów przed pójściem dalej
            for (let ch = 1; ch <= 8; ch++) {
                const funcSelect = document.getElementById(`wizard-function-ch${ch}`);
                const labelInput = document.getElementById(`wizard-label-ch${ch}`);
                if (funcSelect) {
                    relaysConfig.relays[ch - 1].function = funcSelect.value;
                    const info = getFunctionInfo(funcSelect.value);
                    if (funcSelect.value === "custom" && labelInput) {
                        relaysConfig.relays[ch - 1].label = labelInput.value.trim() || "Urządzenie własne";
                    } else {
                        relaysConfig.relays[ch - 1].label = info.label;
                    }
                }
            }
        } else if (step === 3) {
            // Przeczytaj zabezpieczenia
            for (let ch = 1; ch <= 8; ch++) {
                if (relaysConfig.relays[ch - 1].function === "none") continue;

                const defaultSelect = document.getElementById(`wizard-default-ch${ch}`);
                const safeSelect = document.getElementById(`wizard-safe-ch${ch}`);
                const manualCheckbox = document.getElementById(`wizard-manual-ch${ch}`);
                const pinCheckbox = document.getElementById(`wizard-pin-ch${ch}`);

                if (defaultSelect) relaysConfig.relays[ch - 1].defaultState = defaultSelect.value;
                if (safeSelect) relaysConfig.relays[ch - 1].safeState = safeSelect.value;
                if (manualCheckbox) relaysConfig.relays[ch - 1].manualAllowed = manualCheckbox.checked;
                if (pinCheckbox) relaysConfig.relays[ch - 1].pinRequired = pinCheckbox.checked;
            }

            // ATO limit
            const hasAto = relaysConfig.relays.some(r => r.function === "water_dosing");
            if (hasAto) {
                const limitInput = document.getElementById("wizard-ato-limit");
                const val = parseInt(limitInput?.value || 30);
                if (Number.isNaN(val) || val < 5 || val > 300) {
                    setModuleMessage("Czas limitu dolewki ATO musi wynosić od 5 do 300 sekund.", 'danger');
                    limitInput.focus();
                    return false;
                }
                relaysConfig.atoLimit = val;
            }
        }
        return true;
    }

    // Render KROK 2: Tabela przypisania funkcji
    function renderStep2() {
        const tbody = document.getElementById("wizard-relays-table-body");
        if (!tbody) return;

        tbody.innerHTML = "";
        for (let ch = 1; ch <= 8; ch++) {
            const relay = relaysConfig.relays[ch - 1] || { channel: ch, function: "none", label: "" };
            const tr = document.createElement("tr");

            // Dropdown options
            const optionsMarkup = RELAY_FUNCTIONS.map(f => {
                const selected = f.value === relay.function ? "selected" : "";
                return `<option value="${f.value}" ${selected}>${f.label}</option>`;
            }).join("");

            const info = getFunctionInfo(relay.function);
            const isCustom = relay.function === "custom";

            tr.innerHTML = `
                <td style="font-weight: bold; font-size: 14px; text-align: center; vertical-align: middle;">Ch ${ch}</td>
                <td>
                    <select id="wizard-function-ch${ch}" class="form-control wizard-func-select" data-channel="${ch}">
                        ${optionsMarkup}
                    </select>
                </td>
                <td>
                    <input type="text" id="wizard-label-ch${ch}" class="form-control wizard-label-input" value="${window.escapeHtml(relay.label)}" 
                           style="display: ${isCustom ? 'block' : 'none'};" placeholder="Wpisz nazwę...">
                    <span id="wizard-label-preview-ch${ch}" class="text-muted" style="display: ${isCustom ? 'none' : 'inline-block'}; font-size: 13px; font-weight: 500;">
                        ${window.escapeHtml(relay.label)}
                    </span>
                </td>
                <td style="text-align: center; vertical-align: middle;">
                    <span id="wizard-critical-badge-ch${ch}" class="status-label ${info.critical ? 'danger' : 'success'}" style="display: ${relay.function === 'none' ? 'none' : 'inline-block'}; font-size: 11px;">
                        ${info.critical ? 'Krytyczne' : 'Zwykłe'}
                    </span>
                </td>
            `;
            tbody.appendChild(tr);

            // Zdarzenie zmiany funkcji w tabeli
            const selectEl = tr.querySelector(`.wizard-func-select`);
            selectEl.addEventListener("change", (e) => {
                const channel = parseInt(e.target.dataset.channel);
                const val = e.target.value;
                const funcInfo = getFunctionInfo(val);

                const labelInput = document.getElementById(`wizard-label-ch${channel}`);
                const labelPreview = document.getElementById(`wizard-label-preview-ch${channel}`);
                const criticalBadge = document.getElementById(`wizard-critical-badge-ch${channel}`);

                if (val === "custom") {
                    labelInput.style.display = "block";
                    labelPreview.style.display = "none";
                    labelInput.value = "";
                    labelInput.focus();
                } else {
                    labelInput.style.display = "none";
                    labelPreview.style.display = "inline-block";
                    labelPreview.textContent = funcInfo.label;
                }

                if (val === "none") {
                    criticalBadge.style.display = "none";
                } else {
                    criticalBadge.style.display = "inline-block";
                    criticalBadge.className = `status-label ${funcInfo.critical ? 'danger' : 'success'}`;
                    criticalBadge.textContent = funcInfo.critical ? 'Krytyczne' : 'Zwykłe';
                }
            });
        }
    }

    // Render KROK 3: Zabezpieczenia
    function renderStep3() {
        const grid = document.getElementById("wizard-safety-grid");
        if (!grid) return;

        grid.innerHTML = "";
        let hasAto = false;
        let count = 0;

        for (let ch = 1; ch <= 8; ch++) {
            const relay = relaysConfig.relays[ch - 1];
            if (!relay || relay.function === "none") continue;

            count++;
            const info = getFunctionInfo(relay.function);
            if (relay.function === "water_dosing") hasAto = true;

            const card = document.createElement("div");
            card.className = "card glass";
            card.style.padding = "16px";
            card.style.border = "1px solid rgba(255, 255, 255, 0.05)";
            card.style.borderRadius = "8px";

            // default safeState recommendation
            let safeStateRec = "off";
            if (relay.function === "filter" || relay.function === "circulation_pump") safeStateRec = "on";

            card.innerHTML = `
                <div style="display: flex; align-items: center; gap: 10px; margin-bottom: 12px; border-bottom: 1px solid var(--glass-border); padding-bottom: 8px;">
                    <span style="font-size: 16px; color: ${info.critical ? 'var(--danger-color)' : 'var(--accent-cyan)'};">
                        ${window.getLocalIconMarkup(info.icon)}
                    </span>
                    <span style="font-weight: 700; font-size: 14px;">Ch ${ch} — ${window.escapeHtml(relay.label)}</span>
                    <span class="status-label ${info.critical ? 'danger' : 'success'}" style="margin-left: auto; font-size: 10px; padding: 2px 8px;">
                        ${info.critical ? 'Krytyczne' : 'Zwykłe'}
                    </span>
                </div>
                <div class="grid-2-col" style="grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 12px; margin-bottom: 10px;">
                    <div>
                        <label style="font-size: 11px;">Stan po restarcie:</label>
                        <select id="wizard-default-ch${ch}" class="form-control" style="padding: 6px 10px; font-size: 12px;">
                            <option value="auto" ${relay.defaultState === 'auto' ? 'selected' : ''}>Auto (Harmonogram)</option>
                            <option value="on" ${relay.defaultState === 'on' ? 'selected' : ''}>Włączony (ON)</option>
                            <option value="off" ${relay.defaultState === 'off' ? 'selected' : ''}>Wyłączony (OFF)</option>
                        </select>
                    </div>
                    <div>
                        <label style="font-size: 11px; display: flex; justify-content: space-between;">
                            <span>Stan awaryjny:</span>
                            <span class="text-muted">(Zalecana: ${safeStateRec.toUpperCase()})</span>
                        </label>
                        <select id="wizard-safe-ch${ch}" class="form-control" style="padding: 6px 10px; font-size: 12px;">
                            <option value="off" ${relay.safeState === 'off' ? 'selected' : ''}>Wyłączony (OFF)</option>
                            <option value="on" ${relay.safeState === 'on' ? 'selected' : ''}>Włączony (ON)</option>
                            <option value="keep" ${relay.safeState === 'keep' ? 'selected' : ''}>Zachowaj stan</option>
                        </select>
                    </div>
                </div>
                <div style="display: flex; gap: 15px; margin-top: 10px; border-top: 1px dashed rgba(255,255,255,0.05); padding-top: 8px;">
                    <label style="display: flex; align-items: center; gap: 8px; font-size: 12px; cursor: pointer; user-select: none;">
                        <input type="checkbox" id="wizard-manual-ch${ch}" ${relay.manualAllowed ? 'checked' : ''} style="width: 14px; height: 14px;">
                        Zezwól na sterowanie ręczne
                    </label>
                    <label style="display: flex; align-items: center; gap: 8px; font-size: 12px; cursor: pointer; user-select: none;">
                        <input type="checkbox" id="wizard-pin-ch${ch}" ${relay.pinRequired ? 'checked' : ''} style="width: 14px; height: 14px;">
                        Wymagaj PIN admina
                    </label>
                </div>
            `;
            grid.appendChild(card);
        }

        if (count === 0) {
            grid.innerHTML = `<div class="text-muted" style="text-align: center; padding: 20px;">Najpierw przypisz funkcje w Kroku 2.</div>`;
        }

        // Pokaż/ukryj ATO timeout panel
        const atoContainer = document.getElementById("wizard-ato-limit-container");
        if (atoContainer) {
            atoContainer.style.display = hasAto ? "block" : "none";
            if (hasAto && relaysConfig.atoLimit) {
                window.setValue("wizard-ato-limit", relaysConfig.atoLimit);
            }
        }
    }

    // Render KROK 4: Testy
    function renderStep4() {
        const grid = document.getElementById("wizard-test-grid");
        if (!grid) return;

        grid.innerHTML = "";
        let count = 0;

        for (let ch = 1; ch <= 8; ch++) {
            const relay = relaysConfig.relays[ch - 1];
            if (!relay || relay.function === "none") continue;

            count++;
            const row = document.createElement("div");
            row.style.display = "flex";
            row.style.justifyContent = "space-between";
            row.style.alignItems = "center";
            row.style.padding = "10px 15px";
            row.style.background = "rgba(255,255,255,0.02)";
            row.style.borderRadius = "8px";
            row.style.border = "1px solid var(--glass-border)";

            row.innerHTML = `
                <div>
                    <span style="font-weight: 700; font-size: 13px; color: var(--accent-cyan);">Kanał ${ch}</span>
                    <span style="font-size: 13px; color: var(--text-main); margin-left: 10px;">${window.escapeHtml(relay.label)}</span>
                </div>
                <div style="display: flex; gap: 8px;">
                    <button type="button" class="btn btn-secondary btn-sm wizard-test-btn" data-channel="${ch}" data-state="1" style="min-height: 32px; padding: 4px 12px; font-size: 12px; background: rgba(16, 185, 129, 0.1); border-color: rgba(16, 185, 129, 0.3); color: var(--success-color);">
                        Test ON (3s)
                    </button>
                    <button type="button" class="btn btn-secondary btn-sm wizard-test-btn" data-channel="${ch}" data-state="0" style="min-height: 32px; padding: 4px 12px; font-size: 12px; background: rgba(239, 68, 68, 0.1); border-color: rgba(239, 68, 68, 0.3); color: var(--danger-color);">
                        Test OFF
                    </button>
                </div>
            `;
            grid.appendChild(row);
        }

        // Dodaj słuchacze dla przycisków testowych
        grid.querySelectorAll(".wizard-test-btn").forEach(btn => {
            btn.addEventListener("click", async (e) => {
                const channel = parseInt(e.target.dataset.channel);
                const state = parseInt(e.target.dataset.state);

                e.target.disabled = true;
                const prevLabel = e.target.textContent;
                e.target.textContent = "Wysyłanie...";

                try {
                    // Wyślij żądanie testu przekaźnika
                    // Endpoint: /api/action?action=test_relay&channel=x&state=y
                    await window.sendAction("test_relay", {
                        channel: String(channel),
                        state: String(state),
                        duration: state === 1 ? "3" : "0"
                    }, { requirePin: true, showSaveAnimation: false });
                    e.target.textContent = "Wykonano!";
                    setModuleMessage(`Kanał ${channel}: test ${state === 1 ? 'ON przez 3 sekundy' : 'OFF'} wykonany.`, 'ok');
                } catch (err) {
                    e.target.textContent = "Błąd";
                    setModuleMessage(`Błąd testu kanału ${channel}: ${err.message}`, 'danger');
                } finally {
                    setTimeout(() => {
                        e.target.disabled = false;
                        e.target.textContent = prevLabel;
                    }, 1500);
                }
            });
        });

        if (count === 0) {
            grid.innerHTML = `<div class="text-muted" style="text-align: center; padding: 20px;">Najpierw przypisz funkcje w Kroku 2.</div>`;
        }
    }

    // Render KROK 5: Podsumowanie i zapis
    function renderStep5() {
        const warningsBlock = document.getElementById("wizard-validation-warnings");
        const warningsList = document.getElementById("wizard-warnings-list");
        if (!warningsList || !warningsBlock) return;

        warningsList.innerHTML = "";
        const warnings = [];

        // 1. Walidacja konfliktów (te same funkcje krytyczne)
        const criticalFunctions = ["filter", "heater", "co2", "water_dosing"];
        criticalFunctions.forEach(func => {
            const occurrences = relaysConfig.relays.filter(r => r.function === func);
            if (occurrences.length > 1) {
                const funcName = RELAY_FUNCTIONS.find(f => f.value === func)?.label || func;
                warnings.push(`Funkcja krytyczna ${funcName} została przypisana do więcej niż jednego przekaźnika (kanały: ${occurrences.map(o => o.channel).join(", ")}).`);
            }
        });

        // 2. Walidacja braku grzałki lub filtra
        const hasFilter = relaysConfig.relays.some(r => r.function === "filter");
        if (!hasFilter) {
            warnings.push("Brak przypisanego przekaźnika dla Filtra. Czy zbiornik posiada oddzielne zasilanie filtrowania?");
        }
        const hasHeater = relaysConfig.relays.some(r => r.function === "heater");
        if (!hasHeater) {
            warnings.push("Brak przypisanego przekaźnika dla Grzałki.");
        }

        // 3. Walidacja bezpieczeństwa grzałki
        const heaterRelay = relaysConfig.relays.find(r => r.function === "heater");
        if (heaterRelay && heaterRelay.safeState !== "off") {
            warnings.push("Zalecenie: Przekaźnik Grzałki powinien mieć Stan awaryjny ustawiony na OFF.");
        }

        // 4. Walidacja bezpieczeństwa CO2
        const co2Relay = relaysConfig.relays.find(r => r.function === "co2");
        if (co2Relay && co2Relay.safeState !== "off") {
            warnings.push("Zalecenie: Przekaźnik CO2 powinien mieć Stan awaryjny ustawiony na OFF.");
        }

        // Prezentacja
        if (warnings.length > 0) {
            warningsBlock.style.display = "block";
            warnings.forEach(w => {
                const li = document.createElement("li");
                li.textContent = w;
                warningsList.appendChild(li);
            });
        } else {
            warningsBlock.style.display = "none";
        }
    }

    // Eksport pliku JSON
    function exportConfigJson() {
        const dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(relaysConfig, null, 2));
        const dlAnchorElem = document.createElement('a');
        dlAnchorElem.setAttribute("href", dataStr);
        dlAnchorElem.setAttribute("download", `relays_config_${new Date().toISOString().slice(0,10)}.json`);
        dlAnchorElem.click();
    }

    // Import pliku JSON
    function importConfigJson(e) {
        const file = e.target.files[0];
        if (!file) return;
        if (file.size > MAX_RELAY_CONFIG_FILE_BYTES) {
            setModuleMessage("Plik profilu przekracza bezpieczny limit 32 KB.", 'danger');
            e.target.value = "";
            return;
        }

        const reader = new FileReader();
        reader.onload = function(evt) {
            try {
                if (typeof evt.target.result !== 'string') {
                    throw new Error('Nie udało się odczytać pliku jako tekst UTF-8.');
                }
                const parsed = JSON.parse(evt.target.result);
                relaysConfig = normalizeRelayConfig(parsed);
                renderRelayModuleOverview();
                setModuleMessage("Zaimportowano profil mapowania. Zweryfikuj kolejne kroki przed zapisem.", 'ok');
                goToStep(1);
            } catch (err) {
                setModuleMessage(`Błąd odczytu pliku JSON: ${err.message}`, 'danger');
            } finally {
                e.target.value = "";
            }
        };
        reader.onerror = function() {
            setModuleMessage("Nie udało się odczytać pliku profilu.", 'danger');
            e.target.value = "";
        };
        reader.onabort = function() {
            setModuleMessage("Import profilu został anulowany.", 'neutral');
            e.target.value = "";
        };
        reader.readAsText(file, 'UTF-8');
    }

    // Przywrócenie domyślnych map przekaźników
    function restoreDefaultMap() {
        if (!confirm("Czy na pewno chcesz przywrócić domyślne mapowanie przekaźników?")) return;

        relaysConfig.relays = FIRMWARE_RELAY_MAP.map((relay) => ({ ...relay }));

        renderStep2();
        renderRelayModuleOverview();
    }

    // Zapis konfiguracji do sterownika
    async function saveWizardConfig() {
        const status = window.lastStatusData || {};
        const system = status.system || {};
        const mcpOk = status.sensors?.mcp_ok ?? system.mcpConnected ?? system.i2cConnected ?? null;

        if (mcpOk !== true && !confirm("Ostrzeżenie: Moduł ekspandera I2C nie odpowiada. Czy na pewno chcesz zapisać tę konfigurację jako tryb projektowy/offline?")) {
            return;
        }

        const button = document.getElementById("wizard-save-btn");
        if (button) button.disabled = true;

        try {
            // Zrzutuj konfigurację do JSON
            relaysConfig = normalizeRelayConfig(relaysConfig);
            const dataString = JSON.stringify(relaysConfig);

            // Wyślij akcję do ESP32
            // Endpoint: POST na /api/action?action=save_relays&data=...
            await window.sendAction("save_relays", {
                data: dataString
            }, { requirePin: true });

            renderRelayModuleOverview();
            if (typeof window.fetchStatus === 'function') await window.fetchStatus(false);
            setModuleMessage("Profil mapowania zapisano. Aktywne kanały pozostają zgodne ze stałym układem firmware.", 'ok');

        } catch (e) {
            setModuleMessage(`Błąd zapisu profilu mapowania: ${e.message}`, 'danger');
        } finally {
            if (button) button.disabled = false;
        }
    }

    // Inicjalizacja po załadowaniu DOM
    document.addEventListener("DOMContentLoaded", () => {
        initWizard();
        renderRelayModuleOverview();
    });

    window.renderRelayModuleOverview = renderRelayModuleOverview;

})();
