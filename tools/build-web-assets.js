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
const obsoleteFiles = [
    'vendor/alpine.min.js',
    'vendor/alpine.min.js.gz',
    'vendor/tailwind.min.css',
    'vendor/tailwind.min.css.gz'
];

fs.mkdirSync(otaRoot, { recursive: true });

let removedObsoleteFiles = 0;
for (const relativePath of obsoleteFiles) {
    const obsoletePath = path.resolve(otaRoot, relativePath);
    const otaPrefix = `${path.resolve(otaRoot)}${path.sep}`;
    if (!obsoletePath.startsWith(otaPrefix)) {
        throw new Error(`Nieprawidlowa sciezka przestarzalego assetu: ${relativePath}`);
    }
    if (fs.existsSync(obsoletePath) && fs.statSync(obsoletePath).isFile()) {
        fs.unlinkSync(obsoletePath);
        removedObsoleteFiles += 1;
    }
}

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
        if (compressed.length < 10 || compressed[0] !== 0x1f || compressed[1] !== 0x8b) {
            throw new Error(`Nieprawidlowy wynik kompresji gzip: ${relativePath}`);
        }
        // RFC 1952 dopuszcza 255 jako nieznany system. Normalizacja pola OS
        // usuwa jedyną platformową różnicę między gzipem Windows i Linux.
        compressed[9] = 0xff;
        fs.writeFileSync(`${destinationPath}.gz`, compressed);
        gzipBytes += compressed.length;
    }
}

const savingPercent = sourceBytes > 0
    ? Math.round((1 - gzipBytes / sourceBytes) * 100)
    : 0;
console.log(`Web OTA: ${files.length} plikow, ${sourceBytes} B -> ${gzipBytes} B gzip (${savingPercent}% mniej).`);
if (removedObsoleteFiles > 0) {
    console.log(`Web OTA: usunieto ${removedObsoleteFiles} przestarzale assety vendor.`);
}
