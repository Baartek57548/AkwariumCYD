'use strict';

const path = require('node:path');
const { defineConfig, devices } = require('@playwright/test');

const projectRoot = path.resolve(__dirname, '..', '..');

module.exports = defineConfig({
    testDir: __dirname,
    testMatch: '**/*.spec.js',
    timeout: 30000,
    expect: { timeout: 8000 },
    fullyParallel: false,
    workers: process.env.CI ? 2 : 2,
    reporter: [
        ['list'],
        ['html', { open: 'never', outputFolder: 'playwright-report' }]
    ],
    use: {
        baseURL: 'http://127.0.0.1:8000',
        headless: true,
        trace: 'retain-on-failure',
        screenshot: 'only-on-failure',
        video: 'retain-on-failure'
    },
    projects: [
        {
            name: 'chromium-desktop',
            testMatch: 'panel.spec.js',
            use: {
                ...devices['Desktop Chrome'],
                viewport: { width: 1440, height: 1000 }
            }
        },
        {
            name: 'chromium-mobile',
            testMatch: 'compatibility.spec.js',
            use: { ...devices['Pixel 7'] }
        },
        {
            name: 'webkit-tablet',
            testMatch: 'compatibility.spec.js',
            use: { ...devices['iPad Pro 11'] }
        },
        {
            name: 'firefox-desktop',
            testMatch: 'compatibility.spec.js',
            use: {
                ...devices['Desktop Firefox'],
                viewport: { width: 1280, height: 900 }
            }
        },
        {
            name: 'webkit-desktop',
            testMatch: 'compatibility.spec.js',
            use: {
                ...devices['Desktop Safari'],
                viewport: { width: 1280, height: 900 }
            }
        }
    ],
    webServer: {
        command: 'node tools/dev-server/server.js --port 8000',
        cwd: projectRoot,
        url: 'http://127.0.0.1:8000/api/status',
        reuseExistingServer: true,
        timeout: 15000
    }
});
