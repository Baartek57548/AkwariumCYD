'use strict';

const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');

const projectRoot = path.resolve(__dirname, '..');
const webRoot = path.join(projectRoot, 'web');
const otaRoot = path.join(projectRoot, 'sdcard', 'aq', 'ota');
const files = [
    'index.html',
    'settings.html',
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
    'theme.js',
    'vendor/alpine.min.js',
    'vendor/tailwind.min.css'
];

fs.mkdirSync(otaRoot, { recursive: true });

let sourceBytes = 0;
let gzipBytes = 0;
for (const relativePath of files) {
    const sourcePath = path.join(webRoot, relativePath);
    const destinationPath = path.join(otaRoot, relativePath);
    if (!fs.existsSync(sourcePath) || !fs.statSync(sourcePath).isFile()) {
        throw new Error(`Brak wymaganego assetu web: ${relativePath}`);
    }

    fs.mkdirSync(path.dirname(destinationPath), { recursive: true });
    const content = fs.readFileSync(sourcePath);
    fs.writeFileSync(destinationPath, content);
    sourceBytes += content.length;

    if (/\.(?:html|css|js)$/i.test(relativePath)) {
        const compressed = zlib.gzipSync(content, { level: zlib.constants.Z_BEST_COMPRESSION });
        fs.writeFileSync(`${destinationPath}.gz`, compressed);
        gzipBytes += compressed.length;
    }
}

const savingPercent = sourceBytes > 0
    ? Math.round((1 - gzipBytes / sourceBytes) * 100)
    : 0;
console.log(`Web OTA: ${files.length} plikow, ${sourceBytes} B -> ${gzipBytes} B gzip (${savingPercent}% mniej).`);
