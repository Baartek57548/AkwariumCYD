'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const zlib = require('node:zlib');

const projectRoot = path.resolve(__dirname, '..', '..', '..');
const webRoot = path.join(projectRoot, 'web');
const shippedAssets = [
    'index.html',
    'settings.html',
    'theme-bootstrap.js',
    'style.css',
    'charts.css',
    'app-core.js',
    'app-init.js',
    'chart-engine.js',
    'charts.js',
    'dashboard.js',
    'logs.js',
    'ota.js',
    'relays-wizard.js',
    'schedules.js',
    'settings.js',
    'theme.js'
];

function readWebFile(relativePath) {
    return fs.readFileSync(path.join(webRoot, relativePath), 'utf8');
}

test('web shell uses only local scripts and a CSP without inline JavaScript', () => {
    const indexHtml = readWebFile('index.html');
    const csp = indexHtml.match(/http-equiv="Content-Security-Policy"\s+content="([^"]+)"/i)?.[1] || '';
    const scriptDirective = csp.split(';').map((entry) => entry.trim())
        .find((entry) => entry.startsWith('script-src')) || '';

    assert.match(scriptDirective, /^script-src\s+'self'(?:\s|$)/);
    assert.doesNotMatch(scriptDirective, /unsafe-inline|unsafe-eval/);
    assert.doesNotMatch(indexHtml, /<script(?![^>]*\bsrc=)[^>]*>/i);
    assert.doesNotMatch(indexHtml, /<(?:script|link)[^>]+(?:src|href)="https?:\/\//i);

    const referencedAssets = [...indexHtml.matchAll(/(?:src|href)="([^"]+\.(?:js|css))(?:\?[^"]*)?"/gi)]
        .map((match) => match[1]);
    assert.ok(referencedAssets.length >= 10);
    for (const asset of referencedAssets) {
        assert.equal(fs.existsSync(path.join(webRoot, asset)), true, `Brak lokalnego assetu ${asset}`);
    }
});

test('shipped web bundle stays within the embedded gzip budget', () => {
    let rawBytes = 0;
    let gzipBytes = 0;

    for (const relativePath of shippedAssets) {
        const filePath = path.join(webRoot, relativePath);
        assert.equal(fs.existsSync(filePath), true, `Brak assetu wdrożeniowego ${relativePath}`);
        const content = fs.readFileSync(filePath);
        rawBytes += content.length;
        gzipBytes += zlib.gzipSync(content, { level: zlib.constants.Z_BEST_COMPRESSION }).length;
    }

    assert.ok(rawBytes < 800 * 1024, `Pakiet raw jest za duży: ${rawBytes} B`);
    assert.ok(gzipBytes < 160 * 1024, `Pakiet gzip jest za duży: ${gzipBytes} B`);
    assert.equal(fs.existsSync(path.join(webRoot, 'vendor', 'alpine.min.js')), false);
    assert.equal(fs.existsSync(path.join(webRoot, 'vendor', 'tailwind.min.css')), false);
});

test('application scripts do not redeclare top-level functions in one file', () => {
    const scriptFiles = shippedAssets.filter((relativePath) => relativePath.endsWith('.js'));
    for (const relativePath of scriptFiles) {
        const source = readWebFile(relativePath);
        const names = [...source.matchAll(/^function\s+([A-Za-z_$][\w$]*)\s*\(/gm)]
            .map((match) => match[1]);
        const duplicates = [...new Set(names.filter((name, index) => names.indexOf(name) !== index))];
        assert.deepEqual(duplicates, [], `${relativePath} zawiera zduplikowane deklaracje`);
    }
});
