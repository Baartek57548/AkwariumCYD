'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const zlib = require('node:zlib');
const assetContract = require('../../web-assets.json');

const projectRoot = path.resolve(__dirname, '..', '..', '..');
const webRoot = path.join(projectRoot, 'web');
const shippedAssets = assetContract.sourceFiles;

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
    let largestRawAssetBytes = 0;
    let largestGzipAssetBytes = 0;

    for (const relativePath of shippedAssets) {
        const filePath = path.join(webRoot, relativePath);
        assert.equal(fs.existsSync(filePath), true, `Brak assetu wdrożeniowego ${relativePath}`);
        const extension = path.extname(relativePath).toLowerCase();
        const source = fs.readFileSync(filePath);
        const content = assetContract.gzipExtensions.includes(extension)
            ? Buffer.from(source.toString('utf8').replace(/\r\n?/g, '\n'), 'utf8')
            : source;
        rawBytes += content.length;
        largestRawAssetBytes = Math.max(largestRawAssetBytes, content.length);
        if (assetContract.gzipExtensions.includes(extension)) {
            const compressed = zlib.gzipSync(
                content,
                { level: zlib.constants.Z_BEST_COMPRESSION }
            );
            gzipBytes += compressed.length;
            largestGzipAssetBytes = Math.max(
                largestGzipAssetBytes,
                compressed.length
            );
        }
    }

    assert.ok(
        rawBytes < assetContract.budgets.rawBytes,
        `Pakiet raw jest za duży: ${rawBytes} B`
    );
    assert.ok(
        gzipBytes < assetContract.budgets.gzipBytes,
        `Pakiet gzip jest za duży: ${gzipBytes} B`
    );
    assert.ok(
        largestRawAssetBytes < assetContract.budgets.largestRawAssetBytes,
        `Największy asset raw przekracza budżet: ${largestRawAssetBytes} B`
    );
    assert.ok(
        largestGzipAssetBytes < assetContract.budgets.largestGzipAssetBytes,
        `Największy asset gzip przekracza budżet: ${largestGzipAssetBytes} B`
    );
    assert.equal(fs.existsSync(path.join(webRoot, 'vendor', 'alpine.min.js')), false);
    assert.equal(fs.existsSync(path.join(webRoot, 'vendor', 'tailwind.min.css')), false);
});

test('gateway PWA ships installable raster and maskable icons', () => {
    const manifest = JSON.parse(readWebFile('manifest.webmanifest'));
    assert.equal(manifest.display, 'standalone');
    const icons = Array.isArray(manifest.icons) ? manifest.icons : [];
    const required = [
        { size: 192, purpose: 'any' },
        { size: 512, purpose: 'any' },
        { size: 512, purpose: 'maskable' }
    ];

    for (const expectation of required) {
        const declared = icons.find((icon) => (
            icon?.type === 'image/png' &&
            icon?.sizes === `${expectation.size}x${expectation.size}` &&
            String(icon?.purpose || '').split(/\s+/).includes(expectation.purpose)
        ));
        assert.ok(
            declared,
            `Manifest nie zawiera ${expectation.size}px/${expectation.purpose}`
        );
        assert.match(declared.src, /^\/[A-Za-z0-9._/-]+$/);
        const icon = fs.readFileSync(
            path.join(webRoot, declared.src.replace(/^\//, ''))
        );
        assert.deepEqual(
            icon.subarray(0, 8),
            Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])
        );
        assert.equal(icon.subarray(12, 16).toString('ascii'), 'IHDR');
        assert.equal(icon.readUInt32BE(16), expectation.size);
        assert.equal(icon.readUInt32BE(20), expectation.size);
    }
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
