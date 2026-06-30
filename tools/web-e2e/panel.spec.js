'use strict';

const { expect, test } = require('@playwright/test');

async function waitForTelemetry(page) {
    await expect(page.locator('#dashboard-temp-current')).not.toHaveText('--.-');
    await expect(page.locator('#dashboard-ph-current')).not.toHaveText('--');
    await expect(page.locator('#dashboard-ec-current')).not.toHaveText('--');
    await expect(page.locator('#dashboard-ldr-current')).not.toHaveText('--');
}

async function loginAsAdmin(page) {
    await page.locator('#admin-login-btn').click();
    await expect(page.locator('#pin-modal')).toBeVisible();
    await page.locator('#pin-modal-input').fill('1234');
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

test('mobile navigation remains usable at phone width', async ({ page }) => {
    await page.setViewportSize({ width: 390, height: 844 });
    await page.goto('/');
    await waitForTelemetry(page);

    await expect(page.locator('#mobile-nav-toggle')).toBeVisible();
    await page.locator('#mobile-nav-toggle').click();
    await expect(page.locator('#app-sidebar')).toHaveClass(/mobile-open/);
    await page.locator('.nav-item[data-target="wykresy"] > a').click();
    await expect(page.locator('#wykresy')).toHaveClass(/active/);
    await expect(page.locator('#wykresy')).toBeVisible();
});
