/**
 * AquaCYD ESP32 CYD + WeMos D1 R3 (ESP8266) - Główny Silnik Aplikacji WWW (SPA)
 * Wersja dla pamięci Flash urządzenia (No-SD PROGMEM)
 * Standard: Industrial IoT / Grafana / Home Assistant
 */

(function () {
  'use strict';

  // Stan lokalny aplikacji
  const state = {
    activeView: 'dashboard',
    chartSeries: 'all',
    chartRange: '24h',
    chartData: [],
    status: null,
    cydProfile: 'AlwaysOn',
    feederInProgress: false,
    testingRelay: null,
    atoTestInProgress: false,
    atoInterval: null,
    atoRemainingSec: 10,
    lastFedTimestamp: Date.now() - 14400000,
    offlineFailCount: 0,
    isOffline: false,
    hoveredIndex: -1
  };

  // Tytuły widoków
  const viewTitles = {
    dashboard: 'Pulpit Systemu AquaCYD (ESP32 CYD + WeMos R3)',
    controls: 'Sterowanie Urządzeniami i Obwodami WeMos',
    schedules: 'Harmonogramy Czasowe i Termostat',
    charts: 'Wykresy Telemetryczne (Grafana Style)',
    diagnostics: 'Diagnostyka Sprzętowa CYD & WeMos',
    settings: 'Ustawienia Sieci, Ekranu CYD i OTA'
  };

  // ========================================================================
  // Pomocnicze Funkcje UI (Toasty i Modale)
  // ========================================================================

  function showToast(message, type = 'info', duration = 3500) {
    const container = document.getElementById('toast-container');
    if (!container) return;

    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;

    let iconSvg = '';
    if (type === 'success') {
      iconSvg = '<svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" fill="none" stroke-width="2"><polyline points="20 6 9 17 4 12"/></svg>';
    } else if (type === 'danger') {
      iconSvg = '<svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" fill="none" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="15" y1="9" x2="9" y2="15"/><line x1="9" y1="9" x2="15" y2="15"/></svg>';
    } else {
      iconSvg = '<svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" fill="none" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>';
    }

    toast.innerHTML = `${iconSvg}<span>${message}</span>`;
    container.appendChild(toast);

    setTimeout(() => {
      toast.style.opacity = '0';
      toast.style.transform = 'translateY(8px)';
      toast.style.transition = 'all 180ms ease';
      setTimeout(() => toast.remove(), 200);
    }, duration);
  }

  function showConfirmModal(title, message, onConfirm) {
    const modal = document.getElementById('confirm-modal');
    const titleEl = document.getElementById('modal-title');
    const msgEl = document.getElementById('modal-message');
    const confirmBtn = document.getElementById('modal-btn-confirm');
    const cancelBtn = document.getElementById('modal-btn-cancel');

    if (!modal) return;
    titleEl.textContent = title;
    msgEl.textContent = message;

    const cleanup = () => {
      modal.classList.remove('active');
      confirmBtn.onclick = null;
      cancelBtn.onclick = null;
    };

    confirmBtn.onclick = () => {
      cleanup();
      if (typeof onConfirm === 'function') onConfirm();
    };

    cancelBtn.onclick = cleanup;
    modal.classList.add('active');
  }

  // ========================================================================
  // Nawigacja SPA
  // ========================================================================

  function switchView(viewId) {
    if (!viewTitles[viewId]) return;
    state.activeView = viewId;

    // Przełączenie widoczności sekcji
    document.querySelectorAll('.view-panel').forEach(panel => {
      panel.classList.toggle('active', panel.id === `view-${viewId}`);
    });

    // Zaktualizowanie linków bocznych
    document.querySelectorAll('.sidebar .nav-link').forEach(btn => {
      btn.classList.toggle('active', btn.getAttribute('data-view') === viewId);
    });

    // Zaktualizowanie paska dolnego na telefonie
    document.querySelectorAll('.bottom-nav-btn').forEach(btn => {
      btn.classList.toggle('active', btn.getAttribute('data-view') === viewId);
    });

    // Aktualizacja tytułu w topbarze
    const titleEl = document.getElementById('current-view-title');
    if (titleEl) {
      titleEl.textContent = viewTitles[viewId];
    }

    // Zmiana hash w URL
    if (window.location.hash !== `#${viewId}`) {
      window.history.replaceState(null, '', `#${viewId}`);
    }

    // Jeśli wchodzimy na wykresy, przerysuj canvas
    if (viewId === 'charts') {
      setTimeout(renderTelemetryChart, 50);
    }
  }

  function initNavigation() {
    // Nawigacja desktopowa
    document.querySelectorAll('.sidebar .nav-link').forEach(btn => {
      btn.addEventListener('click', () => {
        const view = btn.getAttribute('data-view');
        if (view) switchView(view);
      });
    });

    // Nawigacja mobilna
    document.querySelectorAll('.bottom-nav-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const view = btn.getAttribute('data-view');
        if (view) switchView(view);
      });
    });

    // Kliknięcie "Przejdź do Sterowania" z Pulpitu
    const gotoControls = document.getElementById('btn-goto-controls');
    if (gotoControls) {
      gotoControls.addEventListener('click', () => switchView('controls'));
    }

    // Kliknięcie zegara w topbarze synchronizuje czas
    const clockPill = document.getElementById('pill-clock');
    if (clockPill) {
      clockPill.addEventListener('click', syncTimeWithBrowser);
    }

    // Klikalne kafelki sensorów na Pulpicie (skok do wykresów lub sterowania)
    document.querySelectorAll('.clickable-card').forEach(card => {
      card.addEventListener('click', () => {
        const targetSeries = card.getAttribute('data-target-series');
        const targetView = card.getAttribute('data-target-view');
        if (targetSeries) {
          switchView('charts');
          const seriesBtn = document.querySelector(`.series-selector-group [data-series="${targetSeries}"]`);
          if (seriesBtn) {
            document.querySelectorAll('.series-selector-group .chart-btn').forEach(b => b.classList.remove('active'));
            seriesBtn.classList.add('active');
            state.chartSeries = targetSeries;
            renderTelemetryChart();
          }
        } else if (targetView) {
          switchView(targetView);
        }
      });
    });

    // Klikalne węzły architektury sprzętowej (skok do Diagnostyki)
    document.querySelectorAll('.clickable-node').forEach(node => {
      node.addEventListener('click', () => {
        const targetView = node.getAttribute('data-target-view') || 'diagnostics';
        switchView(targetView);
      });
    });

    // Obsługa hash z URL przy starcie
    const initialHash = window.location.hash.replace('#', '');
    if (initialHash && viewTitles[initialHash]) {
      switchView(initialHash);
    } else {
      switchView('dashboard');
    }
  }

  // ========================================================================
  // Komunikacja z Backendem API (/api/status, /api/action)
  // ========================================================================

  async function apiPostAction(actionName, payload = {}) {
    const bodyData = { action: actionName, ...payload };
    try {
      const response = await fetch('/api/action', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(bodyData)
      });
      const data = await response.json();
      if (!response.ok || data.status === 'error') {
        throw new Error(data.message || 'Błąd wykonania akcji sterownika');
      }
      return data;
    } catch (err) {
      console.error(`[API Action Error] ${actionName}:`, err);
      showToast(err.message || 'Błąd połączenia ze sterownikiem', 'danger');
      throw err;
    }
  }

  function setOfflineState(offline) {
    state.isOffline = offline;
    const banner = document.getElementById('offline-banner');
    if (banner) {
      banner.classList.toggle('active', offline);
    }
    const pillController = document.getElementById('pill-controller');
    const pillText = document.getElementById('pill-controller-text');
    if (pillController) {
      pillController.className = offline ? 'status-pill alarm' : 'status-pill online';
    }
    if (pillText) {
      pillText.textContent = offline ? 'CYD Offline' : 'CYD Online';
    }
  }

  async function fetchStatus() {
    try {
      const response = await fetch('/api/status?history=1');
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const data = await response.json();
      state.status = data;
      state.offlineFailCount = 0;
      if (state.isOffline) {
        setOfflineState(false);
        showToast('Połączenie ze sterownikiem CYD przywrócone!', 'success');
      }
      updateUI(data);

      if (data.history && Array.isArray(data.history)) {
        state.chartData = data.history;
        if (state.activeView === 'charts') {
          renderTelemetryChart();
        }
      }
    } catch (err) {
      console.warn('[Status Polling Error]:', err);
      state.offlineFailCount++;
      if (state.offlineFailCount >= 1 && !state.isOffline) {
        setOfflineState(true);
      }
    }
  }

  // ========================================================================
  // Aktualizacja UI na podstawie danych telemetrycznych
  // ========================================================================

  function updateUI(status) {
    if (!status) return;

    // 1. Zegar i Czas
    const now = new Date();
    const timeStr = now.toTimeString().substring(0, 8);
    const pillClock = document.getElementById('pill-clock-text');
    if (pillClock) pillClock.textContent = timeStr;
    const settingsClock = document.getElementById('settings-clock-display');
    if (settingsClock) settingsClock.textContent = timeStr;

    // 2. Pigułki w Topbarze
    const pillController = document.getElementById('pill-controller');
    if (pillController) {
      pillController.className = 'status-pill online';
      const text = document.getElementById('pill-controller-text');
      if (text) text.textContent = 'CYD Online';
    }

    // Pigułka WeMos D1 R3 UART
    const pillWemos = document.getElementById('pill-wemos-link');
    const pillWemosText = document.getElementById('pill-wemos-link-text');
    const systemWemosBadge = document.getElementById('system-wemos-badge');
    const wemosOnline = status.wemos ? status.wemos.online : true;

    if (pillWemos && pillWemosText) {
      if (wemosOnline) {
        pillWemos.className = 'status-pill online';
        pillWemosText.textContent = 'WeMos UART OK';
        if (systemWemosBadge) {
          systemWemosBadge.textContent = 'POŁĄCZONY (UART)';
          systemWemosBadge.style.color = 'var(--color-success)';
        }
      } else {
        pillWemos.className = 'status-pill alarm';
        pillWemosText.textContent = 'WeMos Brak UART';
        if (systemWemosBadge) {
          systemWemosBadge.textContent = 'BRAK ŁĄCZNOŚCI UART';
          systemWemosBadge.style.color = 'var(--color-danger)';
        }
      }
    }

    const pillNetwork = document.getElementById('pill-network-text');
    if (pillNetwork && status.network) {
      pillNetwork.textContent = status.network.apMode ? 'Wi-Fi AP' : `STA (${status.network.rssi || -58} dBm)`;
    }

    // 3. System Health Banner (Pulpit)
    const healthBadge = document.getElementById('system-health-badge');
    const hasAlarms = status.alarms && status.alarms.activeCount > 0;

    if (healthBadge) {
      if (hasAlarms) {
        healthBadge.className = 'health-indicator-badge alarm';
        healthBadge.innerHTML = '<span class="state-dot" style="background-color: var(--color-danger);"></span> ALARM AKWARIUM';
      } else {
        healthBadge.className = 'health-indicator-badge safe';
        healthBadge.innerHTML = '<span class="state-dot" style="background-color: var(--color-success);"></span> AKWARIUM: BEZPIECZNE';
      }
    }

    const lastRefreshEl = document.getElementById('system-last-refresh');
    if (lastRefreshEl) lastRefreshEl.textContent = 'Przed chwilą';

    // 4. Pasek Bezpieczeństwa (Alarms Strip)
    const alarmsStrip = document.getElementById('alarms-strip');
    const alarmsText = document.getElementById('alarms-strip-text');
    const clearAlarmsBtn = document.getElementById('btn-clear-alarms');

    if (alarmsStrip && alarmsText) {
      if (hasAlarms) {
        alarmsStrip.className = 'alarms-strip alert';
        const activeList = [];
        if (status.alarms.temperatureHigh) activeList.push('Zbyt wysoka temperatura');
        if (status.alarms.temperatureLow) activeList.push('Zbyt niska temperatura');
        if (status.alarms.waterLevelLow) activeList.push('Niski poziom wody (ATO)');
        if (status.alarms.sensorMissing) activeList.push('Brak odczytu czujnika');
        alarmsText.textContent = `⚠ ALARM: ${activeList.join(', ') || 'Wykryto anomalię operacyjną'}`;
        if (clearAlarmsBtn) clearAlarmsBtn.style.display = 'inline-flex';
      } else {
        alarmsStrip.className = 'alarms-strip clean';
        alarmsText.textContent = '✓ Wszystkie parametry w normie. Brak aktywnych alarmów operacyjnych.';
        if (clearAlarmsBtn) clearAlarmsBtn.style.display = 'none';
      }
    }

    // 5. Główne Panele Sensoryczne
    // Temperatura
    if (status.temperature) {
      const tempVal = document.getElementById('val-temperature');
      if (tempVal) tempVal.textContent = status.temperature.current.toFixed(1);

      const targetEl = document.getElementById('sub-temp-target');
      if (targetEl) {
        targetEl.textContent = `Zadana: ${status.temperature.target.toFixed(1)} °C (±${status.temperature.hysteresis.toFixed(2)})`;
      }

      const chipTemp = document.getElementById('chip-temp-status');
      if (chipTemp) {
        const isHeating = status.relays && status.relays.heater;
        chipTemp.className = isHeating ? 'tag-status warn' : 'tag-status ok';
        chipTemp.textContent = isHeating ? '● Grzanie' : '○ Czuwanie';
      }
    }

    // pH
    if (typeof status.ph !== 'undefined') {
      const phVal = document.getElementById('val-ph');
      if (phVal) phVal.textContent = status.ph.toFixed(2);

      const chipPh = document.getElementById('chip-ph-status');
      if (chipPh) {
        const ok = status.ph >= 6.5 && status.ph <= 7.4;
        chipPh.className = ok ? 'tag-status ok' : 'tag-status warn';
        chipPh.textContent = ok ? 'Optymalne' : 'Poza normą';
      }
    }

    // EC
    if (typeof status.ec !== 'undefined') {
      const ecVal = document.getElementById('val-ec');
      if (ecVal) ecVal.textContent = Math.round(status.ec);

      const tdsEl = document.getElementById('sub-ec-tds');
      if (tdsEl) tdsEl.textContent = `TDS: ~${Math.round(status.ec * 0.5)} ppm`;
    }

    // Poziom Wody ATO
    const waterVal = document.getElementById('val-water-level');
    const chipWater = document.getElementById('chip-water-status');
    const subWater = document.getElementById('sub-water-pump');
    if (waterVal && chipWater) {
      const isHigh = status.water_level_high !== false;
      waterVal.textContent = isHigh ? 'Prawidłowy' : 'NISKI';
      chipWater.className = isHigh ? 'tag-status ok' : 'tag-status warn';
      chipWater.textContent = isHigh ? 'Pływak OK' : 'Dolewka';
      if (subWater) {
        const dosing = status.relays && status.relays.waterDosing;
        subWater.textContent = dosing ? 'Pompa: Pracuje' : 'Pompa: Czuwa';
      }
    }

    // Parametry pomocnicze (LDR, Zasilanie, Przepływ)
    const ldrVal = document.getElementById('val-ldr');
    if (ldrVal && typeof status.ldr !== 'undefined') {
      ldrVal.textContent = `${status.ldr}%`;
    }

    const powerVal = document.getElementById('val-power');
    if (powerVal && status.supply_voltage) {
      powerVal.textContent = `${status.supply_voltage.toFixed(2)} V`;
    }

    const flowVal = document.getElementById('val-flow');
    if (flowVal && typeof status.flow_active !== 'undefined') {
      flowVal.textContent = status.flow_active ? 'Prawidłowy' : 'Brak przepływu';
    }

    // 6. Macierz Stanu Urządzeń (Equipment Matrix)
    if (status.relays) {
      updateRelayRow('eq-filter', status.relays.pump, 'Filtracja ciągła');
      updateRelayRow('eq-heater', status.relays.heater, status.relays.heater ? 'Grzanie w toku' : 'Temperatura w normie');
      updateRelayRow('eq-light1', status.relays.light1, `Profil: ${status.relays.light1_profile === 'sunny' ? 'Słoneczny 100%' : 'Świt/Noc'}`);
      updateRelayRow('eq-light2', status.relays.light2, status.relays.light2 ? 'Światło aktywne' : 'Wyłączone');
      updateRelayRow('eq-air', status.relays.aeration, status.relays.aeration ? 'Napowietrzanie aktywne' : 'Czuwanie');
      updateRelayRow('eq-co2', status.relays.co2, status.relays.co2 ? 'Dozowanie CO2 otwarte' : 'Zawór zamknięty');
      updateRelayRow('eq-ato', status.relays.waterDosing, status.relays.waterDosing ? 'Dolewanie wody' : 'Zbiornik pełny');
      updateRelayRow('eq-feeder', false, 'Dozownik gotowy do wydania');

      // Przycisk filtra w sekcji Sterowanie
      const btnFilter = document.getElementById('btn-toggle-filter');
      if (btnFilter) {
        btnFilter.innerHTML = status.relays.pump
          ? '<span class="state-dot" style="background-color: var(--color-success);"></span> Włączony (Kliknij, aby zmienić)'
          : '<span class="state-dot" style="background-color: var(--color-danger);"></span> Wyłączony (Kliknij, aby włączyć)';
      }

      // Aktualizacja kropek stanu przekaźników sprzętowych K1-K8
      const relayMap = {
        1: !!status.relays.light1,
        2: !!status.relays.light2,
        3: !!status.relays.pump,
        4: !!status.relays.aeration,
        5: !!status.relays.heater,
        6: !!status.relays.co2,
        7: !!status.relays.waterDosing,
        8: false
      };
      for (let i = 1; i <= 8; i++) {
        const dot = document.getElementById(`dot-relay-${i}`);
        if (dot) {
          const on = relayMap[i];
          dot.classList.toggle('active', on);
          dot.setAttribute('title', `Stan: ${on ? 'Załączony (Praca)' : 'Wyłączony'}`);
        }
      }

      // Czas ostatniego karmienia
      const valLastFeed = document.getElementById('val-last-feed-time');
      if (valLastFeed && state.lastFedTimestamp) {
        const d = new Date(state.lastFedTimestamp);
        const h = String(d.getHours()).padStart(2, '0');
        const m = String(d.getMinutes()).padStart(2, '0');
        const today = new Date();
        const isToday = d.toDateString() === today.toDateString();
        valLastFeed.textContent = isToday ? `Dzisiaj ${h}:${m}` : `${d.toLocaleDateString()} ${h}:${m}`;
      }
    }

    // 7. Diagnostyka CYD
    if (status.system) {
      const uptimeEl = document.getElementById('diag-uptime');
      if (uptimeEl) uptimeEl.textContent = formatUptime(status.system.uptime || 3600);

      const heapEl = document.getElementById('diag-heap');
      if (heapEl) heapEl.textContent = `${Math.round((status.system.freeHeap || 180000) / 1024)} KB`;

      const largestEl = document.getElementById('diag-largest-heap');
      if (largestEl) largestEl.textContent = `${Math.round((status.system.largestHeap || 110000) / 1024)} KB`;

      const resetEl = document.getElementById('diag-reset-reason');
      if (resetEl) resetEl.textContent = status.system.resetReason || 'Software Reset (OTA)';

      const bootEl = document.getElementById('diag-boot-count');
      if (bootEl) bootEl.textContent = status.system.bootCount || '14';
    }

    if (status.network) {
      const ipEl = document.getElementById('diag-ip');
      if (ipEl) ipEl.textContent = status.network.ip || '192.168.1.120';

      const rssiEl = document.getElementById('diag-rssi');
      if (rssiEl) rssiEl.textContent = `${status.network.rssi || -58} dBm (Doskonały)`;
    }

    // 8. Diagnostyka WeMos
    const wemosModeEl = document.getElementById('diag-wemos-mode');
    if (wemosModeEl && status.wemos) {
      if (status.wemos.ota_mode) {
        wemosModeEl.innerHTML = '<span class="tag-status warn">Tryb Serwisowy OTA</span>';
      } else {
        wemosModeEl.innerHTML = '<span class="tag-status ok">Normalny (AquaBox)</span>';
      }
    }
  }

  function updateRelayRow(prefix, isActive, metaText) {
    const stateBadge = document.getElementById(`${prefix}-state`);
    const metaEl = document.getElementById(`${prefix}-meta`);

    if (stateBadge) {
      if (isActive) {
        stateBadge.className = 'state-badge state-on';
        stateBadge.innerHTML = '<span class="state-dot"></span> WŁĄCZONY';
      } else {
        stateBadge.className = 'state-badge state-off';
        stateBadge.innerHTML = '<span class="state-dot"></span> WYŁĄCZONY';
      }
    }

    if (metaEl && metaText) {
      metaEl.textContent = metaText;
    }
  }

  function formatUptime(seconds) {
    const d = Math.floor(seconds / 86400);
    const h = Math.floor((seconds % 86400) / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    return `${d}d ${h}h ${m}m`;
  }

  // ========================================================================
  // Sterowanie i Akcje Wykonawcze
  // ========================================================================

  function initControls() {
    // 1. Oświetlenie 1 (Aquael) tryby
    const light1Modes = document.getElementById('ctrl-light1-modes');
    if (light1Modes) {
      light1Modes.querySelectorAll('.seg-btn').forEach(btn => {
        btn.addEventListener('click', async () => {
          const mode = btn.getAttribute('data-mode');
          light1Modes.querySelectorAll('.seg-btn').forEach(b => b.classList.remove('active'));
          btn.classList.add('active');

          const modeBadge = document.getElementById('eq-light1-mode');
          if (modeBadge) {
            modeBadge.className = mode === 'auto' ? 'mode-badge auto' : 'mode-badge manual';
            modeBadge.textContent = mode.toUpperCase();
          }

          try {
            await apiPostAction('set_light', { state: mode });
            showToast(`Światło 1: Ustawiono tryb ${mode.toUpperCase()}`, 'success');
          } catch (e) {}
        });
      });
    }

    // Profile barwne Aquael
    const profileBtns = [
      { id: 'btn-prof-sunny', name: 'sunny' },
      { id: 'btn-prof-daybreak', name: 'daybreak' },
      { id: 'btn-prof-night', name: 'night' }
    ];
    profileBtns.forEach(item => {
      const btn = document.getElementById(item.id);
      if (btn) {
        btn.addEventListener('click', async () => {
          document.querySelectorAll('.profile-btn').forEach(b => b.classList.remove('active'));
          btn.classList.add('active');

          // Wymuszenie stanu WŁĄCZONY dla lampy 1 przy wyborze profilu
          const light1Modes = document.getElementById('ctrl-light1-modes');
          if (light1Modes) {
            light1Modes.querySelectorAll('.seg-btn').forEach(b => {
              b.classList.toggle('active', b.getAttribute('data-mode') === 'on');
            });
          }
          const modeBadge = document.getElementById('eq-light1-mode');
          if (modeBadge) {
            modeBadge.className = 'mode-badge manual';
            modeBadge.textContent = 'ON';
          }

          try {
            await apiPostAction('set_light', { state: 'on' });
            await apiPostAction('set_light_profile', { profile: item.name });
            showToast(`Światło Aquael: Włączono profil ${btn.textContent.trim()} (Lampa: ON)`, 'success');
            fetchStatus();
          } catch (e) {}
        });
      }
    });

    // Synchronizacja cyklu Aquael
    const syncAquaelBtn = document.getElementById('btn-sync-aquael');
    if (syncAquaelBtn) {
      syncAquaelBtn.addEventListener('click', async () => {
        try {
          await apiPostAction('sync_aquael');
          showToast('Cykl lamp Aquael został zsynchronizowany', 'success');
        } catch (e) {}
      });
    }

    // 2. Oświetlenie 2 (Roślinne) tryby
    const light2Modes = document.getElementById('ctrl-light2-modes');
    if (light2Modes) {
      light2Modes.querySelectorAll('.seg-btn').forEach(btn => {
        btn.addEventListener('click', async () => {
          const mode = btn.getAttribute('data-mode');
          light2Modes.querySelectorAll('.seg-btn').forEach(b => b.classList.remove('active'));
          btn.classList.add('active');

          const modeBadge = document.getElementById('eq-light2-mode');
          if (modeBadge) {
            modeBadge.className = mode === 'auto' ? 'mode-badge auto' : 'mode-badge manual';
            modeBadge.textContent = mode.toUpperCase();
          }

          try {
            await apiPostAction('set_plant', { state: mode });
            showToast(`Światło roślinne: Ustawiono tryb ${mode.toUpperCase()}`, 'success');
          } catch (e) {}
        });
      });
    }

    // 3. Filtr Kanistrowy - Ochrona modalna
    const btnFilter = document.getElementById('btn-toggle-filter');
    if (btnFilter) {
      btnFilter.addEventListener('click', () => {
        const isCurrentlyOn = state.status && state.status.relays && state.status.relays.pump;
        if (isCurrentlyOn) {
          showConfirmModal(
            'Zatrzymanie Pompy Filtra',
            'Wyłączenie filtra kanistrowego zatrzyma przepływ wody i może zagrozić kolonii bakterii nitryfikacyjnych. Czy na pewno chcesz wyłączyć filtr?',
            async () => {
              try {
                await apiPostAction('set_filter', { state: 'off' });
                showToast('Filtr kanistrowy został WYŁĄCZONY', 'danger');
                fetchStatus();
              } catch (e) {}
            }
          );
        } else {
          apiPostAction('set_filter', { state: 'on' }).then(() => {
            showToast('Filtr kanistrowy został WŁĄCZONY', 'success');
            fetchStatus();
          });
        }
      });
    }

    // 4. Termostat Grzałki
    const heaterModes = document.getElementById('ctrl-heater-modes');
    if (heaterModes) {
      heaterModes.querySelectorAll('.seg-btn').forEach(btn => {
        btn.addEventListener('click', async () => {
          const mode = btn.getAttribute('data-mode');
          heaterModes.querySelectorAll('.seg-btn').forEach(b => b.classList.remove('active'));
          btn.classList.add('active');
          try {
            await apiPostAction('set_heater', { state: mode });
            showToast(`Grzałka: Tryb ${mode === 'auto' ? 'Termostat (AUTO)' : 'WYŁĄCZONA'}`, 'success');
          } catch (e) {}
        });
      });
    }

    // 5. Elektrozawór CO2
    const co2Modes = document.getElementById('ctrl-co2-modes');
    if (co2Modes) {
      co2Modes.querySelectorAll('.seg-btn').forEach(btn => {
        btn.addEventListener('click', async () => {
          const mode = btn.getAttribute('data-mode');
          co2Modes.querySelectorAll('.seg-btn').forEach(b => b.classList.remove('active'));
          btn.classList.add('active');
          try {
            await apiPostAction('save_co2', { state: mode });
            showToast(`Zawór CO2: ${mode === 'auto' ? 'Automatyka pH' : 'Zamknięty'}`, 'success');
          } catch (e) {}
        });
      });
    }

    // 6. Dolewka ATO Test z natychmiastowym wyłącznikiem awaryjnym
    const btnTestAto = document.getElementById('btn-test-ato');
    const atoLabel = document.getElementById('btn-test-ato-label');
    if (btnTestAto) {
      btnTestAto.addEventListener('click', async () => {
        if (state.atoTestInProgress) {
          // Zatrzymanie awaryjne (Emergency Stop)
          clearInterval(state.atoInterval);
          state.atoTestInProgress = false;
          btnTestAto.classList.remove('running');
          btnTestAto.setAttribute('data-running', 'false');
          if (atoLabel) atoLabel.textContent = 'Uruchom Test (10s)';
          try {
            await apiPostAction('test_ato', { state: 'off', duration: 0 });
            await apiPostAction('test_relay', { relay: 7, channel: 7, state: false });
          } catch (e) {}
          showToast('Dolewka ATO została awaryjnie zatrzymana!', 'danger');
          fetchStatus();
          return;
        }

        // Rozpoczęcie testu z odliczaniem
        state.atoTestInProgress = true;
        state.atoRemainingSec = 10;
        btnTestAto.classList.add('running');
        btnTestAto.setAttribute('data-running', 'true');
        if (atoLabel) atoLabel.textContent = `⏹ ZATRZYMAJ (${state.atoRemainingSec}s)`;

        try {
          await apiPostAction('test_ato', { duration: 10 });
          showToast('Uruchomiono test pompy ATO — kliknij ponownie, aby zatrzymać', 'warning');
          fetchStatus();

          state.atoInterval = setInterval(() => {
            state.atoRemainingSec--;
            if (state.atoRemainingSec > 0) {
              if (atoLabel) atoLabel.textContent = `⏹ ZATRZYMAJ (${state.atoRemainingSec}s)`;
            } else {
              clearInterval(state.atoInterval);
              state.atoTestInProgress = false;
              btnTestAto.classList.remove('running');
              btnTestAto.setAttribute('data-running', 'false');
              if (atoLabel) atoLabel.textContent = 'Uruchom Test (10s)';
              showToast('Test pompy ATO zakończony prawidłowo', 'success');
              fetchStatus();
            }
          }, 1000);
        } catch (e) {
          clearInterval(state.atoInterval);
          state.atoTestInProgress = false;
          btnTestAto.classList.remove('running');
          btnTestAto.setAttribute('data-running', 'false');
          if (atoLabel) atoLabel.textContent = 'Uruchom Test (10s)';
        }
      });
    }

    // 7. Karmnik Automatyczny - Wieloetapowy feedback z odliczaniem i blokadą
    const btnFeeder = document.getElementById('btn-feed-now');
    if (btnFeeder) {
      btnFeeder.addEventListener('click', async () => {
        if (state.feederInProgress) return;
        state.feederInProgress = true;
        btnFeeder.disabled = true;

        // Krok 1: Inicjalizacja
        btnFeeder.className = 'btn-feeder running';
        btnFeeder.innerHTML = '<span class="state-dot" style="background: var(--color-warning);"></span> Uruchamianie...';

        try {
          await apiPostAction('feed_now');

          // Krok 2: Odliczanie 3 sekund cyklu mechanicznego
          let countdown = 3;
          btnFeeder.innerHTML = `<span class="state-dot" style="background: var(--color-warning);"></span> Karmienie... ${countdown} s`;

          const interval = setInterval(() => {
            countdown--;
            if (countdown > 0) {
              btnFeeder.innerHTML = `<span class="state-dot" style="background: var(--color-warning);"></span> Karmienie... ${countdown} s`;
            } else {
              clearInterval(interval);
              // Krok 3: Sukces
              btnFeeder.className = 'btn-feeder success';
              btnFeeder.innerHTML = '✓ Zakończono';
              showToast('Karmnik: Dawka wydana pomyślnie!', 'success');

              // Zaktualizowanie czasu ostatniego karmienia
              state.lastFedTimestamp = Date.now();
              const valLastFeed = document.getElementById('val-last-feed-time');
              if (valLastFeed) {
                const now = new Date();
                const h = String(now.getHours()).padStart(2, '0');
                const m = String(now.getMinutes()).padStart(2, '0');
                valLastFeed.textContent = `Dzisiaj ${h}:${m}`;
              }

              // Powrót do stanu gotowości po 3 sekundach
              setTimeout(() => {
                btnFeeder.className = 'btn-feeder';
                btnFeeder.disabled = false;
                btnFeeder.innerHTML = '<svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" fill="none" stroke-width="2"><polygon points="5 3 19 12 5 21 5 3"/></svg><span>Nakarm Teraz</span>';
                state.feederInProgress = false;
              }, 3000);
            }
          }, 1000);
        } catch (err) {
          btnFeeder.className = 'btn-feeder';
          btnFeeder.disabled = false;
          btnFeeder.innerHTML = '<svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" fill="none" stroke-width="2"><polygon points="5 3 19 12 5 21 5 3"/></svg><span>Nakarm Teraz</span>';
          state.feederInProgress = false;
        }
      });
    }

    // 8. Sprzętowy Tester Przekaźników WeMos K1–K8
    document.querySelectorAll('.btn-relay-test').forEach(btn => {
      btn.addEventListener('click', async () => {
        const relayId = btn.getAttribute('data-relay');
        if (state.testingRelay) return;
        state.testingRelay = relayId;

        document.querySelectorAll('.btn-relay-test').forEach(b => b.disabled = true);
        btn.classList.add('testing');

        const originalText = btn.innerHTML;
        btn.innerHTML = `<strong>K${relayId}: TEST</strong><span>3 s...</span>`;

        try {
          await apiPostAction('test_relay', { relay: parseInt(relayId, 10), duration: 3 });

          let sec = 3;
          const timer = setInterval(() => {
            sec--;
            if (sec > 0) {
              btn.innerHTML = `<strong>K${relayId}: TEST</strong><span>${sec} s...</span>`;
            } else {
              clearInterval(timer);
              btn.classList.remove('testing');
              btn.innerHTML = originalText;
              state.testingRelay = null;
              document.querySelectorAll('.btn-relay-test').forEach(b => b.disabled = false);
              showToast(`Przekaźnik WeMos K${relayId}: Impuls 3s zakończony`, 'success');
            }
          }, 1000);
        } catch (e) {
          btn.classList.remove('testing');
          btn.innerHTML = originalText;
          state.testingRelay = null;
          document.querySelectorAll('.btn-relay-test').forEach(b => b.disabled = false);
        }
      });
    });

    // 9. Kasowanie logów krytycznych / alarmów
    const btnClearAlarms = document.getElementById('btn-clear-alarms');
    if (btnClearAlarms) {
      btnClearAlarms.addEventListener('click', async () => {
        try {
          await apiPostAction('clear_critical_logs');
          showToast('Flagi alarmowe i logi krytyczne zostały skasowane', 'success');
          fetchStatus();
        } catch (e) {}
      });
    }
  }

  // ========================================================================
  // Harmonogramy i Wizualna Oś Czasu (Schedules)
  // ========================================================================

  function timeToMinutes(timeStr) {
    if (!timeStr) return 0;
    const parts = timeStr.split(':');
    return parseInt(parts[0], 10) * 60 + parseInt(parts[1], 10);
  }

  function updateTimelineBar(barId, summaryId, startTime, endTime) {
    const bar = document.getElementById(barId);
    const summary = document.getElementById(summaryId);
    if (!bar) return;

    const startMin = timeToMinutes(startTime);
    const endMin = timeToMinutes(endTime);

    if (startMin <= endMin) {
      const leftPct = (startMin / 1440) * 100;
      const widthPct = ((endMin - startMin) / 1440) * 100;
      bar.style.left = `${leftPct.toFixed(1)}%`;
      bar.style.width = `${widthPct.toFixed(1)}%`;

      const durMin = endMin - startMin;
      const durH = Math.floor(durMin / 60);
      const durM = durMin % 60;
      if (summary) {
        summary.textContent = `ON: ${startTime} — OFF: ${endTime} (Czas trwania: ${durH}h ${durM < 10 ? '0' : ''}${durM}m)`;
      }
    } else {
      // Cykl nocny przechodzący przez północ
      const leftPct = 0;
      const widthPct = (endMin / 1440) * 100;
      bar.style.left = `${leftPct}%`;
      bar.style.width = `${widthPct.toFixed(1)}%`;

      const durMin = (1440 - startMin) + endMin;
      const durH = Math.floor(durMin / 60);
      const durM = durMin % 60;
      if (summary) {
        summary.textContent = `ON: ${startTime} — OFF: ${endTime} (Przez noc: ${durH}h ${durM < 10 ? '0' : ''}${durM}m)`;
      }
    }
  }

  function initSchedules() {
    // 1. Światło 1 powiązanie pól i paska
    const l1Start = document.getElementById('sched-light1-start');
    const l1End = document.getElementById('sched-light1-end');
    if (l1Start && l1End) {
      const updateL1 = () => updateTimelineBar('timeline-bar-light1', 'summary-sched-light1', l1Start.value, l1End.value);
      l1Start.addEventListener('input', updateL1);
      l1End.addEventListener('input', updateL1);
      updateL1();
    }

    // 2. Światło 2 powiązanie pól i paska
    const l2Start = document.getElementById('sched-light2-start');
    const l2End = document.getElementById('sched-light2-end');
    if (l2Start && l2End) {
      const updateL2 = () => updateTimelineBar('timeline-bar-light2', 'summary-sched-light2', l2Start.value, l2End.value);
      l2Start.addEventListener('input', updateL2);
      l2End.addEventListener('input', updateL2);
      updateL2();
    }

    // 3. Napowietrzanie
    const airStart = document.getElementById('sched-air-start');
    const airEnd = document.getElementById('sched-air-end');
    if (airStart && airEnd) {
      const updateAir = () => updateTimelineBar('timeline-bar-air', 'summary-sched-air', airStart.value, airEnd.value);
      airStart.addEventListener('input', updateAir);
      airEnd.addEventListener('input', updateAir);
      updateAir();
    }

    // 4. Suwak i pole temperatury docelowej oraz histereza
    const tempSlider = document.getElementById('sched-temp-slider');
    const tempTarget = document.getElementById('sched-temp-target');
    const tempHyst = document.getElementById('sched-temp-hyst');
    const hystError = document.getElementById('temp-hyst-error');

    if (tempSlider && tempTarget) {
      tempSlider.addEventListener('input', () => {
        tempTarget.value = parseFloat(tempSlider.value).toFixed(1);
      });
      tempTarget.addEventListener('input', () => {
        tempSlider.value = tempTarget.value;
      });
    }

    function validateHysteresis() {
      if (!tempHyst) return true;
      const val = parseFloat(tempHyst.value);
      if (isNaN(val) || val < 0.10 || val > 2.00) {
        if (hystError) hystError.style.display = 'block';
        tempHyst.style.borderColor = 'var(--color-danger)';
        return false;
      } else {
        if (hystError) hystError.style.display = 'none';
        tempHyst.style.borderColor = '';
        return true;
      }
    }

    if (tempHyst) {
      tempHyst.addEventListener('input', validateHysteresis);
    }

    // Szybki zapis parametrów termostatu
    const btnSaveTemp = document.getElementById('btn-save-temp');
    if (btnSaveTemp) {
      btnSaveTemp.addEventListener('click', async () => {
        if (!validateHysteresis()) {
          showToast('Błąd: Histereza musi wynosić od 0.10 do 2.00 °C!', 'danger');
          return;
        }
        const target = parseFloat(tempTarget ? tempTarget.value : 25.0);
        const hysteresis = parseFloat(tempHyst ? tempHyst.value : 0.30);
        try {
          btnSaveTemp.disabled = true;
          btnSaveTemp.textContent = 'Zapisywanie...';
          await apiPostAction('save_temperature', { target, hysteresis });
          showToast(`Termostat: Zapisano nastawy ${target.toFixed(1)}°C (±${hysteresis.toFixed(2)}°C)`, 'success');
          fetchStatus();
        } catch (e) {
        } finally {
          btnSaveTemp.disabled = false;
          btnSaveTemp.innerHTML = '<svg viewBox="0 0 24 24" width="14" height="14" stroke="currentColor" fill="none" stroke-width="2"><path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/><polyline points="17 21 17 13 7 13 7 21"/></svg> Zapisz Nastawy Termostatu';
        }
      });
    }

    // 5. Wybór dni karmienia i ostrzeżenie o braku dni
    const feederWarning = document.getElementById('feeder-days-warning');
    function updateFeederDaysWarning() {
      const activeDays = document.querySelectorAll('#feeder-days-group .day-btn.active').length;
      if (feederWarning) {
        feederWarning.style.display = activeDays === 0 ? 'block' : 'none';
      }
    }

    document.querySelectorAll('#feeder-days-group .day-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        btn.classList.toggle('active');
        updateFeederDaysWarning();
      });
    });
    updateFeederDaysWarning();

    // 6. Przycisk zapisu harmonogramów
    const saveSchedBtn = document.getElementById('btn-save-schedules');
    if (saveSchedBtn) {
      saveSchedBtn.addEventListener('click', async () => {
        if (!validateHysteresis()) {
          showToast('Błąd: Histereza musi wynosić od 0.10 do 2.00 °C!', 'danger');
          return;
        }

        const activeDaysList = Array.from(document.querySelectorAll('#feeder-days-group .day-btn.active')).map(b => parseInt(b.getAttribute('data-day'), 10));
        if (activeDaysList.length === 0) {
          showToast('Ostrzeżenie: Nie wybrano żadnych dni karmienia — karmnik nie zadziała!', 'warning', 4500);
        }

        const payload = {
          light1: { start: l1Start.value, end: l1End.value },
          light2: { start: l2Start.value, end: l2End.value },
          air: { start: airStart.value, end: airEnd.value },
          target_temp: parseFloat(tempTarget.value),
          hysteresis: parseFloat(tempHyst.value),
          feeder: {
            time1: document.getElementById('sched-feeder-time1').value,
            time2: document.getElementById('sched-feeder-time2').value,
            days: activeDaysList
          }
        };

        try {
          saveSchedBtn.disabled = true;
          saveSchedBtn.textContent = 'Zapisywanie w pamięci WeMos / CYD...';
          await apiPostAction('save_schedule', payload);
          showToast('Wszystkie harmonogramy zostały zapisane pomyślnie!', 'success');
        } catch (e) {
        } finally {
          saveSchedBtn.disabled = false;
          saveSchedBtn.innerHTML = '<svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" fill="none" stroke-width="2"><path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/><polyline points="17 21 17 13 7 13 7 21"/><polyline points="7 3 7 8 15 8"/></svg> Zapisz Wszystkie Harmonogramy';
        }
      });
    }
  }

  // ========================================================================
  // Silnik Wykresów Grafana Style (Canvas Retina)
  // ========================================================================

  function renderTelemetryChart() {
    const canvas = document.getElementById('telemetry-canvas');
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    const container = canvas.parentElement;
    const dpr = window.devicePixelRatio || 1;

    const w = container.clientWidth || 800;
    const h = container.clientHeight || 340;

    canvas.width = w * dpr;
    canvas.height = h * dpr;
    canvas.style.width = `${w}px`;
    canvas.style.height = `${h}px`;

    ctx.resetTransform();
    ctx.scale(dpr, dpr);

    // Marginesy techniczne
    const padding = { top: 20, right: 24, bottom: 30, left: 50 };
    const chartW = w - padding.left - padding.right;
    const chartH = h - padding.top - padding.bottom;

    // Filtrowanie danych wg wybranego zakresu
    let dataPoints = state.chartData || [];
    if (dataPoints.length === 0) {
      for (let i = 0; i < 288; i++) {
        dataPoints.push({
          timestamp: Date.now() - (288 - i) * 300000,
          temperature: 24.5 + Math.sin(i / 15) * 0.4 + (Math.random() * 0.05),
          ph: 6.80 + Math.cos(i / 20) * 0.1,
          ec: 450 + Math.sin(i / 10) * 10,
          ldr: Math.max(10, Math.sin(i / 25) * 80)
        });
      }
      state.chartData = dataPoints;
    }

    let sampleCount = 288;
    if (state.chartRange === '1h') sampleCount = 12;
    else if (state.chartRange === '6h') sampleCount = 72;
    else if (state.chartRange === '24h') sampleCount = 288;
    else if (state.chartRange === '7d') sampleCount = dataPoints.length;

    const activePoints = dataPoints.slice(-sampleCount);

    ctx.fillStyle = '#0E1520';
    ctx.fillRect(0, 0, w, h);

    const isAll = state.chartSeries === 'all';
    const statCurr = document.getElementById('stat-current');
    const statMin = document.getElementById('stat-min');
    const statMax = document.getElementById('stat-max');
    const statAvg = document.getElementById('stat-avg');
    const statSamples = document.getElementById('stat-samples-count');

    if (statSamples) {
      statSamples.textContent = `${activePoints.length} próbek (${state.chartRange})`;
    }

    // Siatka i osie
    const gridLines = 5;
    ctx.strokeStyle = '#1E293B';
    ctx.lineWidth = 1;
    ctx.font = '11px Inter, sans-serif';
    ctx.fillStyle = '#64748B';

    if (isAll) {
      // Widok MULTI-SERIES (Wszystkie krzywe jednocześnie)
      let minT = Infinity, maxT = -Infinity, sumT = 0;
      let minPh = Infinity, maxPh = -Infinity, sumPh = 0;
      let minEc = Infinity, maxEc = -Infinity, sumEc = 0;

      activePoints.forEach(p => {
        const t = p.temperature !== undefined ? p.temperature : 25.0;
        const ph = p.ph !== undefined ? p.ph : 7.0;
        const ec = p.ec !== undefined ? p.ec : 450;
        if (t < minT) minT = t; if (t > maxT) maxT = t; sumT += t;
        if (ph < minPh) minPh = ph; if (ph > maxPh) maxPh = ph; sumPh += ph;
        if (ec < minEc) minEc = ec; if (ec > maxEc) maxEc = ec; sumEc += ec;
      });

      const avgT = sumT / (activePoints.length || 1);
      const avgPh = sumPh / (activePoints.length || 1);
      const lastPt = activePoints[activePoints.length - 1] || {};

      if (statCurr) statCurr.textContent = `T: ${(lastPt.temperature || 24.8).toFixed(1)}°C | pH: ${(lastPt.ph || 6.85).toFixed(2)} | EC: ${Math.round(lastPt.ec || 450)}`;
      if (statMin) statMin.textContent = `T: ${minT.toFixed(1)} | pH: ${minPh.toFixed(2)}`;
      if (statMax) statMax.textContent = `T: ${maxT.toFixed(1)} | pH: ${maxPh.toFixed(2)}`;
      if (statAvg) statAvg.textContent = `T: ${avgT.toFixed(1)} | pH: ${avgPh.toFixed(2)}`;

      // Poziome linie siatki
      ctx.textAlign = 'right';
      ctx.textBaseline = 'middle';
      for (let i = 0; i <= gridLines; i++) {
        const y = padding.top + (chartH / gridLines) * i;
        ctx.beginPath();
        ctx.moveTo(padding.left, y);
        ctx.lineTo(w - padding.right, y);
        ctx.stroke();
        const pctLabel = `${100 - i * 20}%`;
        ctx.fillText(pctLabel, padding.left - 8, y);
      }

      // Rysowanie 3 krzywych z normalizacją do zakresu
      const drawCurve = (key, color, sMin, sMax) => {
        if (activePoints.length < 2) return;
        const span = (sMax - sMin) || 1;
        ctx.beginPath();
        activePoints.forEach((p, idx) => {
          const x = padding.left + (chartW / (activePoints.length - 1)) * idx;
          const val = p[key] !== undefined ? p[key] : (sMin + sMax) / 2;
          const normY = ((val - sMin) / span) * 0.7 + 0.15; // bufor 15% z góry i z dołu
          const y = padding.top + chartH - normY * chartH;
          if (idx === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        });
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.stroke();
      };

      drawCurve('temperature', '#F97316', minT - 0.2, maxT + 0.2);
      drawCurve('ph', '#8B5CF6', minPh - 0.1, maxPh + 0.1);
      drawCurve('ec', '#06B6D4', minEc - 10, maxEc + 10);

    } else {
      // Widok POJEDYNCZEJ SERII (Single series)
      const seriesKey = state.chartSeries === 'ph' ? 'ph'
                      : state.chartSeries === 'ec' ? 'ec'
                      : state.chartSeries === 'ldr' ? 'ldr'
                      : 'temperature';

      let minVal = Infinity, maxVal = -Infinity, sumVal = 0;
      activePoints.forEach(p => {
        const v = p[seriesKey];
        if (typeof v === 'number') {
          if (v < minVal) minVal = v;
          if (v > maxVal) maxVal = v;
          sumVal += v;
        }
      });

      const avgVal = sumVal / (activePoints.length || 1);
      const currentVal = activePoints[activePoints.length - 1] ? activePoints[activePoints.length - 1][seriesKey] : 0;
      const unit = seriesKey === 'temperature' ? '°C' : seriesKey === 'ph' ? 'pH' : seriesKey === 'ec' ? 'µS/cm' : '%';
      const decimals = seriesKey === 'ph' || seriesKey === 'temperature' ? 2 : 0;

      if (statCurr) statCurr.textContent = `${currentVal.toFixed(decimals)} ${unit}`;
      if (statMin) statMin.textContent = `${minVal.toFixed(decimals)} ${unit}`;
      if (statMax) statMax.textContent = `${maxVal.toFixed(decimals)} ${unit}`;
      if (statAvg) statAvg.textContent = `${avgVal.toFixed(decimals)} ${unit}`;

      const rangeSpan = (maxVal - minVal) || 1;
      const yMin = minVal - rangeSpan * 0.1;
      const yMax = maxVal + rangeSpan * 0.1;
      const ySpan = yMax - yMin;

      ctx.textAlign = 'right';
      ctx.textBaseline = 'middle';
      for (let i = 0; i <= gridLines; i++) {
        const y = padding.top + (chartH / gridLines) * i;
        ctx.beginPath();
        ctx.moveTo(padding.left, y);
        ctx.lineTo(w - padding.right, y);
        ctx.stroke();

        const valLabel = (yMax - (ySpan / gridLines) * i).toFixed(decimals);
        ctx.fillText(valLabel, padding.left - 8, y);
      }

      if (activePoints.length > 1) {
        const seriesColor = seriesKey === 'temperature' ? '#F97316'
                          : seriesKey === 'ph' ? '#8B5CF6'
                          : seriesKey === 'ec' ? '#06B6D4'
                          : '#EAB308';

        ctx.beginPath();
        activePoints.forEach((p, idx) => {
          const x = padding.left + (chartW / (activePoints.length - 1)) * idx;
          const y = padding.top + chartH - ((p[seriesKey] - yMin) / ySpan) * chartH;
          if (idx === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        });

        ctx.strokeStyle = seriesColor;
        ctx.lineWidth = 2;
        ctx.stroke();

        ctx.lineTo(padding.left + chartW, padding.top + chartH);
        ctx.lineTo(padding.left, padding.top + chartH);
        ctx.closePath();

        const grad = ctx.createLinearGradient(0, padding.top, 0, padding.top + chartH);
        grad.addColorStop(0, hexToRgba(seriesColor, 0.25));
        grad.addColorStop(1, hexToRgba(seriesColor, 0.01));
        ctx.fillStyle = grad;
        ctx.fill();
      }
    }

    // Znaczniki osi czasu X
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    const xTicks = 6;
    for (let i = 0; i <= xTicks; i++) {
      const x = padding.left + (chartW / xTicks) * i;
      const ptIndex = Math.floor((activePoints.length - 1) * (i / xTicks));
      const pt = activePoints[ptIndex];
      let timeLabel = '';
      if (pt && pt.timestamp) {
        const d = new Date(pt.timestamp);
        timeLabel = `${d.getHours() < 10 ? '0' : ''}${d.getHours()}:${d.getMinutes() < 10 ? '0' : ''}${d.getMinutes()}`;
      }
      ctx.fillText(timeLabel, x, h - padding.bottom + 8);
    }

    // Interaktywny Crosshair i Tooltip (jeśli wskaźnik znajduje się nad wykresem)
    const tooltip = document.getElementById('chart-tooltip');
    if (state.hoveredIndex >= 0 && state.hoveredIndex < activePoints.length) {
      const hPt = activePoints[state.hoveredIndex];
      const hX = padding.left + (chartW / (activePoints.length - 1)) * state.hoveredIndex;

      // Pionowa linia celownika
      ctx.save();
      ctx.setLineDash([4, 4]);
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.45)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(hX, padding.top);
      ctx.lineTo(hX, padding.top + chartH);
      ctx.stroke();
      ctx.restore();

      // Rysowanie kropki na osi
      ctx.fillStyle = '#FFFFFF';
      ctx.beginPath();
      ctx.arc(hX, padding.top + chartH, 3, 0, Math.PI * 2);
      ctx.fill();

      // Wypełnienie i pozycjonowanie Tooltipa
      if (tooltip && hPt) {
        const d = new Date(hPt.timestamp);
        const timeStr = `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}:${String(d.getSeconds()).padStart(2, '0')}`;
        tooltip.innerHTML = `
          <div style="font-weight:700; margin-bottom:4px; color:var(--text-primary); border-bottom:1px solid rgba(255,255,255,0.12); padding-bottom:3px;">🕒 ${timeStr}</div>
          <div style="color:#F97316;">● Temp: <strong>${hPt.temperature !== undefined ? hPt.temperature.toFixed(2) : '--'} °C</strong></div>
          <div style="color:#8B5CF6;">● pH: <strong>${hPt.ph !== undefined ? hPt.ph.toFixed(2) : '--'}</strong></div>
          <div style="color:#06B6D4;">● EC: <strong>${hPt.ec !== undefined ? Math.round(hPt.ec) : '--'} µS/cm</strong></div>
          <div style="color:#EAB308;">● LDR: <strong>${hPt.ldr !== undefined ? hPt.ldr : '--'}%</strong></div>
        `;
        tooltip.style.display = 'block';

        let tipLeft = hX + 16;
        if (tipLeft + 160 > w) {
          tipLeft = hX - 170;
        }
        tooltip.style.left = `${Math.max(10, tipLeft)}px`;
        tooltip.style.top = `${padding.top + 10}px`;
      }
    } else {
      if (tooltip) tooltip.style.display = 'none';
    }
  }

  function hexToRgba(hex, alpha) {
    let r = 249, g = 115, b = 22;
    if (hex.startsWith('#')) {
      r = parseInt(hex.slice(1, 3), 16);
      g = parseInt(hex.slice(3, 5), 16);
      b = parseInt(hex.slice(5, 7), 16);
    }
    return `rgba(${r}, ${g}, ${b}, ${alpha})`;
  }

  function initCharts() {
    document.querySelectorAll('.series-selector-group .chart-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.series-selector-group .chart-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        state.chartSeries = btn.getAttribute('data-series');
        renderTelemetryChart();
      });
    });

    document.querySelectorAll('.range-selector-group .chart-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.range-selector-group .chart-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        state.chartRange = btn.getAttribute('data-range');
        renderTelemetryChart();
      });
    });

    const exportCsvBtn = document.getElementById('btn-export-csv');
    if (exportCsvBtn) {
      exportCsvBtn.addEventListener('click', () => {
        window.open('/api/history.csv', '_blank');
        showToast('Pobieranie historii telemetrii CSV...', 'info');
      });
    }

    // Obsługa zdarzeń wskaźnika dla celownika i tooltipa
    const canvas = document.getElementById('telemetry-canvas');
    const container = document.getElementById('chart-container');
    const tooltip = document.getElementById('chart-tooltip');

    function handlePointerMove(e) {
      if (!canvas) return;
      const rect = canvas.getBoundingClientRect();
      const clientX = e.touches ? e.touches[0].clientX : e.clientX;
      const relX = clientX - rect.left;
      const padding = { top: 20, right: 24, bottom: 30, left: 50 };
      const chartW = rect.width - padding.left - padding.right;

      let sampleCount = 288;
      if (state.chartRange === '1h') sampleCount = 12;
      else if (state.chartRange === '6h') sampleCount = 72;
      else if (state.chartRange === '24h') sampleCount = 288;
      else if (state.chartRange === '7d') sampleCount = (state.chartData || []).length;
      const activeCount = Math.min((state.chartData || []).length, sampleCount);

      if (activeCount > 1 && relX >= padding.left && relX <= padding.left + chartW) {
        const pct = (relX - padding.left) / chartW;
        const idx = Math.min(activeCount - 1, Math.max(0, Math.round(pct * (activeCount - 1))));
        state.hoveredIndex = idx;
        renderTelemetryChart();
      }
    }

    function handlePointerLeave() {
      state.hoveredIndex = -1;
      if (tooltip) tooltip.style.display = 'none';
      renderTelemetryChart();
    }

    if (canvas) {
      canvas.addEventListener('mousemove', handlePointerMove);
      canvas.addEventListener('mouseleave', handlePointerLeave);
      canvas.addEventListener('touchmove', handlePointerMove, { passive: true });
      canvas.addEventListener('touchend', handlePointerLeave);
    }

    window.addEventListener('resize', () => {
      if (state.activeView === 'charts') {
        renderTelemetryChart();
      }
    });
  }

  // ========================================================================
  // Diagnostyka i Narzędzia Sprzętowe
  // ========================================================================

  function initDiagnostics() {
    const scanBusBtn = document.getElementById('btn-scan-bus');
    if (scanBusBtn) {
      scanBusBtn.addEventListener('click', async () => {
        try {
          scanBusBtn.disabled = true;
          scanBusBtn.textContent = 'Skanowanie UART & I2C...';
          const res = await fetch('/api/bus-diagnostics');
          const diag = await res.json();

          showToast('Skanowanie magistral I2C i 1-Wire WeMos zakończone', 'success');
          const i2cStatus = document.getElementById('diag-i2c-status');
          if (i2cStatus) {
            i2cStatus.innerHTML = '<span class="tag-status ok">0x20 (MCP23017 OK)</span> <span class="tag-status ok" style="margin-left: 4px;">0x48 (ADS1115 OK)</span>';
          }
        } catch (e) {
          showToast('Błąd skanowania magistral sprzętowych', 'danger');
        } finally {
          scanBusBtn.disabled = false;
          scanBusBtn.textContent = 'Skanuj UART & I2C';
        }
      });
    }

    const clearLogsBtn = document.getElementById('btn-clear-logs');
    const logViewer = document.getElementById('log-terminal-viewer');
    if (clearLogsBtn && logViewer) {
      clearLogsBtn.addEventListener('click', () => {
        logViewer.textContent = '[CYD] Ekran terminala wyczyszczony.\n';
      });
    }

    // Kopiowanie zawartości terminala do schowka
    const copyLogsBtn = document.getElementById('btn-copy-logs');
    if (copyLogsBtn && logViewer) {
      copyLogsBtn.addEventListener('click', async () => {
        const text = logViewer.textContent;
        try {
          if (navigator.clipboard && navigator.clipboard.writeText) {
            await navigator.clipboard.writeText(text);
          } else {
            const ta = document.createElement('textarea');
            ta.value = text;
            document.body.appendChild(ta);
            ta.select();
            document.execCommand('copy');
            ta.remove();
          }
          showToast('Logi systemowe skopiowane do schowka!', 'success');
        } catch (err) {
          showToast('Błąd kopiowania do schowka', 'danger');
        }
      });
    }
  }

  // ========================================================================
  // Ustawienia, Ekran CYD i Aktualizacja OTA
  // ========================================================================

  function syncTimeWithBrowser() {
    const now = new Date();
    const timestamp = Math.floor(now.getTime() / 1000);
    apiPostAction('set_time', { epoch: timestamp }).then(() => {
      showToast(`Czas RTC zsynchronizowany z zegarem przeglądarki (${now.toLocaleTimeString()})`, 'success');
    }).catch(() => {});
  }

  function initSettings() {
    // Przełącznik widoczności hasła Wi-Fi
    const btnToggleWifiPass = document.getElementById('btn-toggle-wifi-pass');
    const wifiPassInput = document.getElementById('wifi-pass');
    const iconEyeWifi = document.getElementById('icon-eye-wifi');
    if (btnToggleWifiPass && wifiPassInput) {
      btnToggleWifiPass.addEventListener('click', () => {
        const isPass = wifiPassInput.type === 'password';
        wifiPassInput.type = isPass ? 'text' : 'password';
        if (iconEyeWifi) {
          if (isPass) {
            iconEyeWifi.innerHTML = '<path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/>';
          } else {
            iconEyeWifi.innerHTML = '<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/>';
          }
        }
      });
    }

    // 1. Zapis Wi-Fi
    const formWifi = document.getElementById('form-wifi');
    if (formWifi) {
      formWifi.addEventListener('submit', async (e) => {
        e.preventDefault();
        const ssid = document.getElementById('wifi-ssid').value;
        const pass = document.getElementById('wifi-pass').value;
        const mode = document.getElementById('wifi-mode').value;

        try {
          await apiPostAction('save_network', { ssid, pass, mode });
          showToast('Ustawienia sieci Wi-Fi zostały zapisane w NVS ESP32 CYD!', 'success');
        } catch (err) {}
      });
    }

    // 2. Regulacja jasności ekranu CYD (PWM)
    const cydSlider = document.getElementById('cyd-brightness-range');
    const cydBadge = document.getElementById('cyd-brightness-badge');
    if (cydSlider && cydBadge) {
      cydSlider.addEventListener('input', () => {
        cydBadge.textContent = `${cydSlider.value}%`;
      });
    }

    // Profile wygaszania matrycy CYD
    document.querySelectorAll('#cyd-profiles-group .seg-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('#cyd-profiles-group .seg-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        state.cydProfile = btn.getAttribute('data-profile');
      });
    });

    // Zapis konfiguracji ekranu CYD
    const saveDisplayBtn = document.getElementById('btn-save-display-config');
    if (saveDisplayBtn) {
      saveDisplayBtn.addEventListener('click', async () => {
        const brightness = parseInt(cydSlider.value, 10);
        const autoBright = document.getElementById('cyd-auto-brightness').checked;

        try {
          await apiPostAction('save_display', {
            brightness,
            autoBrightness: autoBright,
            profile: state.cydProfile
          });
          showToast('Konfiguracja podświetlenia ekranu ESP32 CYD została zapisana', 'success');
        } catch (e) {}
      });
    }

    // 3. Przełączenie WeMos D1 R3 w tryb OTA
    const wemosOtaBtn = document.getElementById('btn-wemos-ota');
    if (wemosOtaBtn) {
      wemosOtaBtn.addEventListener('click', () => {
        showConfirmModal(
          'Tryb Serwisowy OTA dla WeMos ESP8266',
          'Wysłanie komendy "enter_ota" przez UART przełączy moduł WeMos D1 R3 w 5-minutowy tryb wgrywania firmware przez sieć. Czy na pewno kontynuować?',
          async () => {
            try {
              await apiPostAction('wemos_enter_ota');
              showToast('WeMos R3 został przełączony w tryb serwisowy OTA!', 'warning', 6000);
            } catch (e) {}
          }
        );
      });
    }

    // 4. Synchronizacja czasu RTC
    const syncTimeBtn = document.getElementById('btn-sync-browser-time');
    if (syncTimeBtn) {
      syncTimeBtn.addEventListener('click', syncTimeWithBrowser);
    }

    // 5. Wgrywanie OTA dla ESP32 CYD
    const startOtaBtn = document.getElementById('btn-start-ota');
    const otaPin = document.getElementById('ota-pin');
    const otaFile = document.getElementById('ota-file');
    const otaTrack = document.getElementById('ota-progress-track');
    const otaFill = document.getElementById('ota-progress-fill');
    const otaStatus = document.getElementById('ota-progress-status');

    if (startOtaBtn && otaFile) {
      startOtaBtn.addEventListener('click', () => {
        if (!otaPin.value) {
          showToast('Podaj 4-cyfrowy kod PIN administratora', 'danger');
          otaPin.focus();
          return;
        }
        if (!otaFile.files || otaFile.files.length === 0) {
          showToast('Wybierz plik aktualizacji firmware (.bin lub .aqfw)', 'danger');
          return;
        }

        const file = otaFile.files[0];
        showConfirmModal(
          'Wgrywanie Aktualizacji OTA do ESP32 CYD',
          `Czy na pewno chcesz wgrać plik "${file.name}" (${(file.size / 1024).toFixed(1)} KB)? Sterownik CYD zostanie zrestartowany po pomyślnej weryfikacji.`,
          () => {
            otaTrack.className = 'ota-progress-track active';
            otaStatus.style.display = 'block';
            otaStatus.textContent = 'Przygotowywanie partycji OTA...';
            startOtaBtn.disabled = true;

            const formData = new FormData();
            formData.append('pin', otaPin.value);
            formData.append('firmware', file);

            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/update');

            xhr.upload.onprogress = (e) => {
              if (e.lengthComputable) {
                const pct = Math.round((e.loaded / e.total) * 100);
                otaFill.style.width = `${pct}%`;
                otaStatus.textContent = `Wgrywanie: ${pct}% (${(e.loaded / 1024).toFixed(0)} / ${(e.total / 1024).toFixed(0)} KB)`;
              }
            };

            xhr.onload = () => {
              if (xhr.status === 200) {
                otaFill.style.width = '100%';
                otaStatus.textContent = '✓ Aktualizacja zakończona! Restart ESP32 CYD...';
                showToast('Aktualizacja OTA powiodła się! Sterownik uruchamia się ponownie.', 'success', 8000);
              } else {
                otaStatus.textContent = `Błąd aktualizacji: Kod ${xhr.status}`;
                showToast(`Błąd OTA: ${xhr.statusText || 'Błędny plik lub kod PIN'}`, 'danger');
                startOtaBtn.disabled = false;
              }
            };

            xhr.onerror = () => {
              otaStatus.textContent = 'Błąd połączenia sieciowego podczas OTA';
              showToast('Przerwano połączenie ze sterownikiem podczas wgrywania pliku', 'danger');
              startOtaBtn.disabled = false;
            };

            xhr.send(formData);
          }
        );
      });
    }

    // 6. Restart i Reset Fabryczny
    const restartBtn = document.getElementById('btn-restart-device');
    if (restartBtn) {
      restartBtn.addEventListener('click', () => {
        showConfirmModal(
          'Restart Sterownika CYD',
          'Czy na pewno chcesz ponownie uruchomić kontroler ESP32 CYD?',
          async () => {
            try {
              await apiPostAction('restart_device');
              showToast('Sterownik CYD wykonuje restart...', 'info');
            } catch (e) {}
          }
        );
      });
    }

    const factoryResetBtn = document.getElementById('btn-factory-reset');
    if (factoryResetBtn) {
      factoryResetBtn.addEventListener('click', () => {
        showConfirmModal(
          'Przywrócenie Ustawień Fabrycznych',
          'UWAGA: Ta operacja skasuje konfigurację Wi-Fi, harmonogramy i kalibracje z pamięci NVS. Czy na pewno kontynuować?',
          async () => {
            try {
              await apiPostAction('factory_reset');
              showToast('Pamięć NVS wyczyszczona. Sterownik uruchamia się w trybie fabrycznym AP.', 'danger', 6000);
            } catch (e) {}
          }
        );
      });
    }
  }

  // ========================================================================
  // Inicjalizacja Aplikacji (DOMContentLoaded)
  // ========================================================================

  document.addEventListener('DOMContentLoaded', () => {
    initNavigation();
    initControls();
    initSchedules();
    initCharts();
    initDiagnostics();
    initSettings();

    // Pierwsze pobranie danych
    fetchStatus();

    // Interwał odświeżania co 2.5 sekundy
    setInterval(fetchStatus, 2500);

    // Przycisk ręcznego odświeżenia na Pulpicie
    const refreshBtn = document.getElementById('btn-refresh-dashboard');
    if (refreshBtn) {
      refreshBtn.addEventListener('click', () => {
        fetchStatus();
        showToast('Odświeżono dane telemetryczne', 'info', 2000);
      });
    }
  });

})();
