'use strict';

const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const { minifyGatewayPwa } = require('./minify-gateway-pwa');
const { renderPwaIcons } = require('./render-pwa-icons');

const projectRoot = path.resolve(__dirname, '..');
const webRoot = path.join(projectRoot, 'web');
const otaRoot = path.join(projectRoot, 'sdcard', 'aq', 'ota');
const assetContract = require('./web-assets.json');
const files = assetContract.sourceFiles;
const gzipExtensions = new Set(assetContract.gzipExtensions);
const obsoleteFiles = [
    'aquacyd-icon.svg',
    'aquacyd-icon.svg.gz',
    'vendor/alpine.min.js',
    'vendor/alpine.min.js.gz',
    'vendor/tailwind.min.css',
    'vendor/tailwind.min.css.gz'
];

minifyGatewayPwa(projectRoot);
renderPwaIcons(webRoot);
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
    const extension = path.extname(relativePath).toLowerCase();
    const source = fs.readFileSync(sourcePath);
    const content = gzipExtensions.has(extension)
        ? Buffer.from(source.toString('utf8').replace(/\r\n?/g, '\n'), 'utf8')
        : source;
    fs.writeFileSync(destinationPath, content);
    sourceBytes += content.length;

    if (gzipExtensions.has(extension)) {
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
