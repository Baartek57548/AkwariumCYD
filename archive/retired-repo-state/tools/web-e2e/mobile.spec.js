'use strict';

const { test, expect } = require('@playwright/test');

async function openPanel(page, theme) {
    await page.addInitScript((preference) => {
        localStorage.setItem('aqPortalThemePreference', preference);
    }, theme);
    await page.goto('/');
    await expect(page.locator('#backend-connection-status')).toHaveAttribute('data-state', 'online');
    await expect(page.locator('#dashboard-temp-current')).not.toHaveText('--.-');
    await expect(page.locator('html')).toHaveAttribute('data-theme', theme);
}

async function navigate(page, target) {
    if (await page.locator('#mobile-nav-toggle').getAttribute('aria-expanded') !== 'true') {
        await page.locator('#mobile-nav-toggle').click();
    }
    await page.locator(`.nav-item[data-target="${target}"] a`).click();
    await expect(page.locator(`#${target}`)).toHaveClass(/active/);
    await expect(page.locator('#mobile-nav-toggle')).toHaveAttribute('aria-expanded', 'false');
}

async function expectNoOverflow(page) {
    const overflow = await page.evaluate(() => document.documentElement.scrollWidth - innerWidth);
    expect(overflow).toBeLessThanOrEqual(1);
}

for (const theme of ['light', 'dark']) {
    test(`${theme}: readable readings and working navigation at 320, 390 and 430px`, async ({ page }) => {
        const errors = [];
        page.on('pageerror', (error) => errors.push(error.message));
        await openPanel(page, theme);
        for (const width of [320, 390, 430]) {
            await page.setViewportSize({ width, height: 844 });
            await expectNoOverflow(page);
            for (const [selector, minimum] of [
                ['#dashboard-temp-current', 44],
                ['#dashboard-ph-current', 22],
                ['#dashboard-ec-current', 22],
                ['.telemetry-sub', 14]
            ]) {
                const size = await page.locator(selector).first().evaluate((element) => parseFloat(getComputedStyle(element).fontSize));
                expect(size, selector).toBeGreaterThanOrEqual(minimum);
            }
            await navigate(page, 'wykresy');
            await expect(page.locator('#history-tbody tr').first()).toBeVisible();
            await expectNoOverflow(page);
            const cells = await page.locator('#history-tbody').evaluate((tbody) =>
                [...tbody.querySelector('tr').cells].map((element) => ({
                    label: element.dataset.label,
                    size: parseFloat(getComputedStyle(element).fontSize),
                    right: element.getBoundingClientRect().right
                })));
            expect(cells).toHaveLength(5);
            for (const cell of cells) {
                expect(cell.label).toBeTruthy();
                expect(cell.size).toBeGreaterThanOrEqual(16);
                expect(cell.right).toBeLessThanOrEqual(width + 1);
            }
            // The simulator has less than 24h of history; unavailable ranges
            // are intentionally disabled until enough measurements exist.
            await page.locator('#range-selector [data-range="1h"]').click();
            await expect(page.locator('#range-selector [data-range="1h"]')).toHaveClass(/active/);
            await page.locator('#fullscreen-btn').click();
            await expect(page.locator('#fullscreen-overlay')).toBeVisible();
            await page.locator('#fullscreen-close-btn').click();
            await expect(page.locator('#fullscreen-overlay')).toBeHidden();
            await navigate(page, 'dashboard');
        }
        expect(errors).toEqual([]);
    });
}

test('phone admin PIN and forms retain touch-sized controls', async ({ page }) => {
    await openPanel(page, 'light');
    await page.locator('#mobile-nav-toggle').click();
    await page.locator('#admin-login-btn').click();
    await page.locator('#pin-modal-input').fill(process.env.CYD_DEV_PIN || '1234');
    await page.locator('#pin-modal-submit').click();
    await expect(page.locator('body')).toHaveClass(/role-admin/);
    await navigate(page, 'automatyka');
    const sizes = await page.locator('#automatyka input[type="number"]:visible').evaluateAll((inputs) =>
        inputs.map((input) => ({
            font: parseFloat(getComputedStyle(input).fontSize),
            height: input.getBoundingClientRect().height
        })));
    expect(sizes.length).toBeGreaterThan(0);
    for (const size of sizes) {
        expect(size.font).toBeGreaterThanOrEqual(16);
        expect(size.height).toBeGreaterThanOrEqual(48);
    }
    await expectNoOverflow(page);
});
