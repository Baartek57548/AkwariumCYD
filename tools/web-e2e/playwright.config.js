'use strict';

const path = require('node:path');
const { defineConfig } = require('@playwright/test');

const projectRoot = path.resolve(__dirname, '..', '..');

module.exports = defineConfig({
    testDir: __dirname,
    testMatch: '**/*.spec.js',
    timeout: 30000,
    expect: { timeout: 8000 },
    fullyParallel: false,
    workers: 1,
    reporter: [['list']],
    use: {
        baseURL: 'http://127.0.0.1:8000',
        browserName: 'chromium',
        headless: true,
        viewport: { width: 1440, height: 1000 },
        trace: 'retain-on-failure',
        screenshot: 'only-on-failure'
    },
    webServer: {
        command: 'node tools/dev-server/server.js --port 8000',
        cwd: projectRoot,
        url: 'http://127.0.0.1:8000/api/status',
        reuseExistingServer: true,
        timeout: 15000
    }
});
