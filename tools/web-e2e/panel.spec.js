'use strict';

const { expect, test } = require('@playwright/test');
const DEV_ADMIN_PIN = process.env.CYD_DEV_PIN || '1234';

async function waitForTelemetry(page) {
    await expect(page.locator('#dashboard-temp-current')).not.toHaveText('--.-');
    await expect(page.locator('#dashboard-ph-current')).not.toHaveText('--');
    await expect(page.locator('#dashboard-ec-current')).not.toHaveText('--');
    await expect(page.locator('#dashboard-ldr-current')).not.toHaveText('--');
}

async function loginAsAdmin(page) {
    await page.locator('#admin-login-btn').click();
    await expect(page.locator('#pin-modal')).toBeVisible();
    await page.locator('#pin-modal-input').fill(DEV_ADMIN_PIN);
    await page.locator('#pin-modal-submit').click();
    await expect(page.locator('body')).toHaveClass(/role-admin/);
}

test('dashboard receives complete DEV telemetry without browser errors', async ({ page }) => {
    const consoleErrors = [];
    page.on('console', (message) => {
        if (message.type() === 'error') consoleErrors.push(message.text());
    });
    page.on('pageerror', (error) => consoleErrors.push(error.message));

    await page.goto('/');
    await waitForTelemetry(page);

    await expect(page.locator('#dashboard-battery-voltage')).not.toHaveText('--.--V');
    await expect(page.locator('#network-status')).toContainText('STA');
    await expect(page.locator('#sidebar-firmware-version')).toContainText('dev-simulator');
    await expect(page.locator('.nav-item[data-target="automatyka"] i.fa-sliders')).toHaveCount(1);
    await expect(page.locator('.nav-item[data-target="automatyka"] i.fa-sliders svg')).toHaveCount(1);

    const duplicateIds = await page.locator('[id]').evaluateAll((elements) => {
        const counts = new Map();
        for (const element of elements) counts.set(element.id, (counts.get(element.id) || 0) + 1);
        return [...counts.entries()].filter(([, count]) => count > 1);
    });
    expect(duplicateIds).toEqual([]);
    expect(consoleErrors).toEqual([]);
});

test('keyboard PIN login unlocks every administrative section', async ({ page }) => {
    await page.goto('/');
    await waitForTelemetry(page);
    await loginAsAdmin(page);
    await expect(page.locator('.nav-item[data-target="automatyka"] i.fa-sliders')).toBeVisible();

    const targets = ['przekazniki', 'automatyka', 'harmonogramy', 'zasilanie', 'logi', 'diag', 'ustawienia', 'ota'];
    for (const target of targets) {
        await page.locator(`.nav-item[data-target="${target}"] > a`).click();
        await expect(page.locator(`#${target}`)).toHaveClass(/active/);
        await expect(page.locator(`#${target}`)).toBeVisible();
    }

    await page.locator('#admin-logout-btn').click();
    await expect(page.locator('body')).toHaveClass(/role-guest/);
    await expect(page.locator('#dashboard')).toHaveClass(/active/);
});

test('diagnostics presents I2C, UART and OneWire hardware buses', async ({ page }) => {
    await page.goto('/');
    await waitForTelemetry(page);
    await loginAsAdmin(page);
    await page.locator('.nav-item[data-target="diag"] > a').click();

    await expect(page.locator('#i2c-device-list .bus-device-card')).toHaveCount(2);
    await expect(page.locator('#i2c-device-list')).toContainText('0x20');
    await expect(page.locator('#i2c-device-list')).toContainText('MCP23017');
    await expect(page.locator('#i2c-device-list')).toContainText('0x48');
    await expect(page.locator('#i2c-device-list')).toContainText('ADS1115');
    await expect(page.locator('#diag-strip-bus')).toContainText('2 I2C');
    await expect(page.locator('#i2c-sda-pin')).toHaveText('GPIO 27');
    await expect(page.locator('#i2c-scl-pin')).toHaveText('GPIO 22');
    await expect(page.locator('#i2c-frequency')).toHaveText('400 kHz');

    await expect(page.locator('#uart-bus-state')).toHaveText('AKTYWNA');
    await expect(page.locator('#uart-port')).toHaveText('UART0');
    await expect(page.locator('#uart-tx-pin')).toHaveText('GPIO 1');
    await expect(page.locator('#uart-rx-pin')).toHaveText('GPIO 3');
    await expect(page.locator('#uart-baud')).toContainText('115');
    await expect(page.locator('#onewire-data-pin')).toHaveText('GPIO 17');
    await expect(page.locator('#onewire-device-list .bus-device-card')).toHaveCount(1);
    await expect(page.locator('#onewire-device-list')).toContainText('DS18B20');
    await expect(page.locator('#onewire-device-list')).toContainText('28-FF641D871603-5F');

    await page.locator('#bus-scan-refresh').click();
    await expect(page.locator('#bus-scan-status')).toContainText('symulowane magistrale');
});

test('settings selector appears above content and switches all four panels', async ({ page }) => {
    await page.goto('/');
    await loginAsAdmin(page);
    await page.locator('.nav-item[data-target="ustawienia"] > a').click();

    const targets = ['settings-network-panel', 'settings-display-panel', 'settings-clock-panel', 'settings-admin-panel'];
    for (const target of targets) {
        await page.locator(`.settings-nav-item[data-settings-target="${target}"]`).click();
        await expect(page.locator(`#${target}`)).toHaveClass(/active/);
        await expect(page.locator(`#${target}`)).toBeVisible();
    }

    const navBox = await page.locator('.settings-nav-panel').boundingBox();
    const panelBox = await page.locator('#settings-admin-panel').boundingBox();
    expect(navBox).not.toBeNull();
    expect(panelBox).not.toBeNull();
    expect(navBox.y).toBeLessThan(panelBox.y);
});

test('temperature settings are saved through the real panel contract', async ({ page }) => {
    await page.goto('/');
    await loginAsAdmin(page);
    await page.locator('.nav-item[data-target="automatyka"] > a').click();

    await page.locator('#settings-temp-target').fill('25.7');
    await page.locator('#settings-temp-hyst').fill('0.6');
    await page.locator('#save-temperature-btn').click();
    await expect(page.locator('#settings-temp-status')).toContainText('Zapisano');

    const status = await (await page.request.get('/api/status')).json();
    expect(status.temperature.target).toBe(25.7);
    expect(status.temperature.hysteresis).toBe(0.6);
});

test('admin login is responsive and display settings are persisted', async ({ page }) => {
    await page.goto('/');
    await waitForTelemetry(page);
    await page.locator('#admin-login-btn').click();
    await page.locator('#pin-modal-input').fill(DEV_ADMIN_PIN);
    const loginStartedAt = Date.now();
    await page.locator('#pin-modal-submit').click();
    await expect(page.locator('body')).toHaveClass(/role-admin/);
    expect(Date.now() - loginStartedAt).toBeLessThan(1500);

    await page.locator('.nav-item[data-target="ustawienia"] > a').click();
    await page.locator('.settings-nav-item[data-settings-target="settings-display-panel"]').click();
    await page.locator('#settings-display-auto').evaluate((input) => {
        input.checked = false;
        input.dispatchEvent(new Event('change', { bubbles: true }));
    });
    await page.locator('#settings-display-profile').selectOption('timeout_60s');
    await page.locator('#settings-display-brightness').fill('65');
    await page.locator('#save-display-btn').click();
    await expect(page.locator('#settings-display-status')).toContainText('zapisane');

    const status = await (await page.request.get('/api/status')).json();
    expect(status.display.autoBrightness).toBe(false);
    expect(status.display.profile).toBe('timeout_60s');
    expect(status.display.brightness).toBe(65);
});

test('mobile navigation and every view remain usable at phone width', async ({ page }) => {
    await page.setViewportSize({ width: 390, height: 844 });
    await page.goto('/');
    await waitForTelemetry(page);

    await expect(page.locator('#mobile-nav-toggle')).toBeVisible();
    await expect(page.locator('#mobile-nav-toggle')).toHaveAttribute('aria-label', 'Otwórz menu');
    await expect(page.locator('#mobile-current-view')).toHaveText('Pulpit');
    await page.locator('#mobile-nav-toggle').click();
    await expect(page.locator('#mobile-nav-toggle')).toHaveAttribute('aria-label', 'Zamknij menu');
    await expect(page.locator('#mobile-nav-toggle-label')).toHaveText('Zamknij');
    await expect(page.locator('#app-sidebar')).toHaveClass(/mobile-open/);
    await expect(page.locator('#admin-login-btn')).toBeVisible();

    await expect.poll(async () => page.locator('#app-sidebar').evaluate((sidebar) => Math.round(sidebar.getBoundingClientRect().left))).toBe(0);
    const drawerLayout = await page.locator('#app-sidebar').evaluate((sidebar) => {
        const box = sidebar.getBoundingClientRect();
        return {
            position: getComputedStyle(sidebar).position,
            height: Math.round(box.height),
            viewportHeight: window.innerHeight
        };
    });
    expect(drawerLayout.position).toBe('fixed');
    expect(drawerLayout.height).toBe(drawerLayout.viewportHeight);

    await loginAsAdmin(page);

    const targets = {
        dashboard: 'Pulpit',
        wykresy: 'Pomiary',
        przekazniki: 'Moduły',
        automatyka: 'Automatyka modułów',
        harmonogramy: 'Harmonogramy',
        zasilanie: 'Zasilanie',
        logi: 'Logi',
        diag: 'Diagnostyka',
        ustawienia: 'Ustawienia',
        ota: 'OTA'
    };
    for (const [target, label] of Object.entries(targets)) {
        const sidebarOpen = await page.locator('#app-sidebar').evaluate((sidebar) => sidebar.classList.contains('mobile-open'));
        if (!sidebarOpen) await page.locator('#mobile-nav-toggle').click();
        await page.locator(`.nav-item[data-target="${target}"] > a`).click();
        await expect(page.locator(`#${target}`)).toHaveClass(/active/);
        await expect(page.locator(`#${target}`)).toBeVisible();
        await expect(page.locator('#mobile-current-view')).toHaveText(label);
        await expect(page.locator('#mobile-nav-toggle')).toHaveAttribute('aria-label', 'Otwórz menu');

        const layout = await page.evaluate(() => ({
            clientWidth: document.documentElement.clientWidth,
            scrollWidth: document.documentElement.scrollWidth
        }));
        expect(layout.scrollWidth).toBeLessThanOrEqual(layout.clientWidth + 1);
    }

    await page.locator('#mobile-nav-toggle').click();
    await page.locator('.nav-item[data-target="wykresy"] > a').click();
    await expect(page.locator('#wykresy .chart-card')).toBeVisible();
    await expect(page.locator('#wykresy .mini-charts-row')).toBeVisible();

    await page.locator('#mobile-nav-toggle').click();
    await page.locator('.nav-item[data-target="harmonogramy"] > a').click();
    const timelineLayout = await page.locator('#harmonogramy .factory-timeline').evaluate((timeline) => ({
        clientWidth: timeline.clientWidth,
        scrollWidth: timeline.scrollWidth,
        overflowX: getComputedStyle(timeline).overflowX
    }));
    expect(timelineLayout.scrollWidth).toBeGreaterThan(timelineLayout.clientWidth);
    expect(timelineLayout.overflowX).toBe('auto');

    await page.locator('#mobile-nav-toggle').click();
    await page.locator('.nav-item[data-target="ustawienia"] > a').click();
    const settingsNavLayout = await page.locator('#ustawienia .settings-nav-panel').evaluate((navigation) => ({
        clientWidth: navigation.clientWidth,
        scrollWidth: navigation.scrollWidth
    }));
    expect(settingsNavLayout.scrollWidth).toBeGreaterThan(settingsNavLayout.clientWidth);
});

test('mobile shell remains compact at 320px', async ({ page }) => {
    await page.setViewportSize({ width: 320, height: 800 });
    await page.goto('/');
    await waitForTelemetry(page);

    const layout = await page.evaluate(() => ({
        clientWidth: document.documentElement.clientWidth,
        scrollWidth: document.documentElement.scrollWidth,
        topbarHeight: Math.round(document.querySelector('.topbar').getBoundingClientRect().height),
        appbarHeight: Math.round(document.querySelector('#mobile-nav-toggle').getBoundingClientRect().height),
        menuTop: Math.round(document.querySelector('#mobile-nav-toggle').getBoundingClientRect().top)
    }));
    expect(layout.scrollWidth).toBeLessThanOrEqual(layout.clientWidth + 1);
    expect(layout.topbarHeight).toBeLessThanOrEqual(140);
    expect(layout.appbarHeight).toBeLessThanOrEqual(64);
    expect(layout.menuTop).toBeGreaterThanOrEqual(0);
});

test('mobile follows the system color scheme while desktop follows the controller', async ({ page }) => {
    await page.setViewportSize({ width: 390, height: 844 });
    await page.emulateMedia({ colorScheme: 'dark' });
    await page.goto('/');
    await waitForTelemetry(page);

    await expect(page.locator('html')).toHaveAttribute('data-theme', 'dark');
    await expect(page.locator('html')).toHaveAttribute('data-theme-source', 'system');
    await expect(page.locator('#theme-toggle-label')).toHaveText('Auto: ciemny');

    await page.emulateMedia({ colorScheme: 'light' });
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'light');
    await expect(page.locator('#theme-toggle-label')).toHaveText('Auto: jasny');

    await page.setViewportSize({ width: 1200, height: 900 });
    await expect(page.locator('html')).toHaveAttribute('data-theme-source', 'device');
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'light');
    await expect(page.locator('#theme-toggle-label')).toHaveText('Jasny');
});

test('factory schedule remains readable and exposes execution priorities', async ({ page }) => {
    await page.goto('/');
    await loginAsAdmin(page);
    await page.locator('.nav-item[data-target="harmonogramy"] > a').click();

    await expect(page.locator('.factory-phase-legend > span')).toHaveCount(4);
    await expect(page.locator('.schedule-logic-grid > article')).toHaveCount(3);
    await expect(page.locator('.factory-phase-legend')).toContainText('DAYBREAK');
    await expect(page.locator('.factory-phase-legend')).toContainText('NIGHT');
    await expect(page.locator('#schedule-light-profile option')).toHaveCount(4);
    await expect(page.locator('#schedule-plant-light-profile option')).toHaveCount(4);
    await expect(page.locator('#schedule-light-profile')).toHaveValue('cycle');
    await expect(page.locator('#schedule-plant-light-profile')).toHaveValue('cycle');

    const lightRows = page.locator('.factory-row-light');
    await expect(lightRows).toHaveCount(2);
    await expect(lightRows.nth(0)).toContainText('Światło 1');
    await expect(lightRows.nth(1)).toContainText('Światło 2');
    for (let rowIndex = 0; rowIndex < 2; rowIndex += 1) {
        const lightSegments = lightRows.nth(rowIndex).locator('.factory-segment');
        await expect(lightSegments).toHaveCount(4);
        const boxes = await lightSegments.evaluateAll((segments) => segments.map((segment) => {
            const box = segment.getBoundingClientRect();
            return { left: box.left, right: box.right, width: box.width };
        }));
        expect(boxes.every((box) => box.width > 0)).toBe(true);
        for (let index = 0; index < boxes.length - 1; index += 1) {
            expect(boxes[index].right).toBeLessThanOrEqual(boxes[index + 1].left + 1);
        }
    }

    const scalePositions = await page.locator('.factory-scale-track span').evaluateAll((labels) => labels.map((label) => ({
        text: label.textContent.trim(),
        left: label.getBoundingClientRect().left
    })));
    expect(scalePositions.map((label) => label.text)).toEqual(['00:00', '06:00', '10:00', '14:00', '19:00', '22:00', '24:00']);
    expect(scalePositions.every((label, index) => index === 0 || label.left > scalePositions[index - 1].left)).toBe(true);
});

test('module overview reflects the fixed firmware map and opens the wizard', async ({ page }) => {
    await page.goto('/');
    await waitForTelemetry(page);
    await loginAsAdmin(page);
    await page.locator('.nav-item[data-target="przekazniki"] > a').click();

    await expect(page.locator('#module-channel-grid .module-channel-card')).toHaveCount(8);
    await expect(page.locator('#module-overview-i2c')).not.toHaveText('OCZEKUJE');
    await expect(page.locator('#module-map-warning')).toContainText('spójna');
    await page.locator('#module-open-wizard').click();
    await expect(page.locator('#wizard-step-1')).toBeVisible();
    await expect(page.locator('#relays-wizard-card')).toBeInViewport();
});

test('relay wizard tests and saves the firmware map without blocking alerts', async ({ page }) => {
    const dialogs = [];
    page.on('dialog', async (dialog) => {
        dialogs.push(dialog.message());
        await dialog.dismiss();
    });

    await page.goto('/');
    await waitForTelemetry(page);
    await loginAsAdmin(page);
    await page.locator('.nav-item[data-target="przekazniki"] > a').click();
    await page.locator('#module-open-wizard').click();

    await page.locator('#wizard-next-btn').click();
    await expect(page.locator('#wizard-step-2')).toBeVisible();
    await page.locator('#wizard-next-btn').click();
    await expect(page.locator('#wizard-step-3')).toBeVisible();
    await page.locator('#wizard-next-btn').click();
    await expect(page.locator('#wizard-step-4')).toBeVisible();

    await page.locator('.wizard-test-btn[data-state="1"]').first().click();
    await expect(page.locator('#module-map-warning')).toContainText('wykonany');

    await page.locator('#wizard-next-btn').click();
    await expect(page.locator('#wizard-step-5')).toBeVisible();
    await page.locator('#wizard-save-btn').click();
    await expect(page.locator('#module-map-warning')).toContainText('zapisano');
    expect(dialogs).toEqual([]);
});

test('temperature chart keeps values outside the plotted data area', async ({ page }) => {
    await page.goto('/');
    await waitForTelemetry(page);

    await expect(page.locator('#temperature-chart-svg')).toBeVisible();
    await expect(page.locator('#temperature-chart-svg .chart-chip')).toHaveCount(0);
    await expect(page.locator('#temperature-chart-summary .temp-chart-pill-live')).toContainText('Ostatni');
    await expect(page.locator('#temperature-chart-summary .temp-chart-pill-target')).toContainText('Cel');
});
