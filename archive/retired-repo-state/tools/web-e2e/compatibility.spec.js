'use strict';

const { test, expect } = require('@playwright/test');
const AxeBuilder = require('@axe-core/playwright').default;

async function waitForLiveDashboard(page) {
    await page.goto('/');
    await expect(page.locator('#backend-connection-status')).toHaveAttribute(
        'data-state',
        'online'
    );
    await expect(page.locator('#dashboard-temp-current')).not.toHaveText('--.-');
}

test('responsive dashboard remains usable without horizontal overflow', async ({ page }) => {
    const pageErrors = [];
    page.on('pageerror', (error) => pageErrors.push(error.message));
    await waitForLiveDashboard(page);

    const layout = await page.evaluate(() => ({
        viewportWidth: window.innerWidth,
        documentWidth: document.documentElement.scrollWidth,
        mobileToggleVisible:
            document.getElementById('mobile-nav-toggle')?.offsetParent !== null
    }));
    expect(layout.documentWidth).toBeLessThanOrEqual(layout.viewportWidth + 1);

    if (layout.mobileToggleVisible) {
        const toggle = page.locator('#mobile-nav-toggle');
        await toggle.click();
        await expect(toggle).toHaveAttribute('aria-expanded', 'true');
        await expect(page.locator('#app-sidebar')).toHaveClass(/mobile-open/);
        const backdrop = page.locator('#sidebar-backdrop');
        const backdropBox = await backdrop.boundingBox();
        expect(backdropBox).not.toBeNull();
        await page.mouse.click(
            backdropBox.x + backdropBox.width - 2,
            backdropBox.y + 2
        );
        await expect(toggle).toHaveAttribute('aria-expanded', 'false');
    } else {
        await expect(page.locator('#app-sidebar')).toBeVisible();
    }

    expect(pageErrors).toEqual([]);
});

test('critical accessibility rules pass on the live dashboard', async ({ page }) => {
    await waitForLiveDashboard(page);
    const results = await new AxeBuilder({ page })
        .withTags(['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa'])
        .analyze();
    const blocking = results.violations.filter(
        (violation) => violation.impact === 'critical'
    );
    expect(
        blocking,
        blocking.map((violation) => `${violation.id}: ${violation.help}`).join('\n')
    ).toEqual([]);
});

test('HTTP device panel never activates the HTTPS gateway PWA', async ({ page }) => {
    await waitForLiveDashboard(page);
    const gateway = await page.evaluate(() => ({
        enabled: window.AquaCydGatewayPwa?.enabled,
        controlled: Boolean(navigator.serviceWorker?.controller),
        manifest: document.querySelector('link[rel="manifest"]')?.getAttribute('href')
    }));
    expect(gateway.enabled).toBe(false);
    expect(gateway.controlled).toBe(false);
    expect(gateway.manifest).toBeUndefined();
});

test('offline snapshot mode rejects commands without issuing a request', async ({ page }) => {
    let commandRequests = 0;
    page.on('request', (request) => {
        if (request.method() !== 'GET' && request.url().includes('/api/action')) {
            commandRequests += 1;
        }
    });
    await waitForLiveDashboard(page);
    const result = await page.evaluate(async () => {
        document.body.dataset.dataMode = 'offline-snapshot';
        try {
            await sendAction(
                'set_filter',
                { state: '1' },
                { requirePin: false, notifyError: false }
            );
            return { accepted: true, code: '' };
        } catch (error) {
            return { accepted: false, code: error?.code || '' };
        }
    });
    expect(result).toEqual({ accepted: false, code: 'offline_read_only' });
    expect(commandRequests).toBe(0);
});

test('offline snapshot sanitizer strips nested secrets and bounds hostile payloads', async ({ page }) => {
    await waitForLiveDashboard(page);
    const result = await page.evaluate(() => {
        let deep = { value: 'depth-limit-marker' };
        for (let index = 0; index < 12; index += 1) {
            deep = { level: deep };
        }
        const list = Array.from({ length: 300 }, (_, index) => ({
            value: index,
            sessionToken: `list-secret-${index}`
        }));
        const sanitized = window.AquaCydGatewayPwa.sanitizeSnapshot({
            system: {
                uptime: 42,
                sessionToken: 'system-secret',
                nested: {
                    wifiPassword: 'wifi-secret',
                    hmacSecret: 'hmac-secret',
                    authorization: 'Bearer secret',
                    safeState: 'ok'
                },
                deep,
                list
            },
            sensors: {
                ping: 37,
                adminPin: '1234',
                credentials: { user: 'secret' }
            }
        });
        const lightsSanitized = window.AquaCydGatewayPwa.sanitizeSnapshot({
            lights: {
                front: {
                    on: true,
                    accessToken: 'light-secret'
                }
            }
        });
        const wide = window.AquaCydGatewayPwa.sanitizeSnapshot({
            system: {
                wide: Object.fromEntries(
                    Array.from({ length: 300 }, (_, index) => [`metric${index}`, index])
                )
            }
        });
        const encoded = JSON.stringify(sanitized);
        return {
            encoded: `${encoded}\n${JSON.stringify(lightsSanitized)}`,
            ping: sanitized?.sensors?.ping,
            safeState: sanitized?.system?.nested?.safeState,
            frontOn: lightsSanitized?.lights?.front?.on,
            arrayLength: sanitized?.system?.list?.length,
            wideKeys: Object.keys(wide?.system?.wide || {}).length
        };
    });

    expect(result.ping).toBe(37);
    expect(result.safeState).toBe('ok');
    expect(result.frontOn).toBe(true);
    expect(result.arrayLength).toBeLessThanOrEqual(256);
    expect(result.wideKeys).toBeLessThanOrEqual(128);
    expect(result.encoded).not.toContain('secret');
    expect(result.encoded).not.toContain('depth-limit-marker');
    expect(result.encoded).not.toContain('adminPin');
});
