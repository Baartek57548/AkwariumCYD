'use strict';

const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');

const PNG_SIGNATURE = Buffer.from([
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a
]);
const PALETTE = Buffer.from([
    0x07, 0x10, 0x19,
    0x0b, 0x17, 0x24,
    0x22, 0xd3, 0xee,
    0x25, 0x63, 0xeb,
    0x0e, 0xa5, 0xe9,
    0x34, 0xd3, 0x99,
    0xd1, 0xfa, 0xe5
]);

const crcTable = new Uint32Array(256);
for (let value = 0; value < crcTable.length; value += 1) {
    let crc = value;
    for (let bit = 0; bit < 8; bit += 1) {
        crc = (crc & 1) !== 0
            ? 0xedb88320 ^ (crc >>> 1)
            : crc >>> 1;
    }
    crcTable[value] = crc >>> 0;
}

function crc32(buffer) {
    let crc = 0xffffffff;
    for (const byte of buffer) {
        crc = crcTable[(crc ^ byte) & 0xff] ^ (crc >>> 8);
    }
    return (crc ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
    const typeBytes = Buffer.from(type, 'ascii');
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length, 0);
    const checksum = Buffer.alloc(4);
    checksum.writeUInt32BE(
        crc32(Buffer.concat([typeBytes, data])),
        0
    );
    return Buffer.concat([length, typeBytes, data, checksum]);
}

function drawCircle(pixels, size, centerX, centerY, radius, color) {
    const minimumX = Math.max(0, Math.floor(centerX - radius));
    const maximumX = Math.min(size - 1, Math.ceil(centerX + radius));
    const minimumY = Math.max(0, Math.floor(centerY - radius));
    const maximumY = Math.min(size - 1, Math.ceil(centerY + radius));
    const squaredRadius = radius * radius;
    for (let y = minimumY; y <= maximumY; y += 1) {
        for (let x = minimumX; x <= maximumX; x += 1) {
            const deltaX = x + 0.5 - centerX;
            const deltaY = y + 0.5 - centerY;
            if (deltaX * deltaX + deltaY * deltaY <= squaredRadius) {
                pixels[y * size + x] = color;
            }
        }
    }
}

function renderIndexedIcon(size) {
    const scale = size / 512;
    const pixels = Buffer.alloc(size * size, 0);

    for (let y = 0; y < size; y += 1) {
        const diagonalBoundary = size * 0.72 - y * 0.42;
        for (let x = 0; x < size; x += 1) {
            if (x < diagonalBoundary) {
                pixels[y * size + x] = 1;
            }
        }
    }

    const left = Math.round(82 * scale);
    const right = Math.round(430 * scale);
    const waveBands = [
        { center: 235, amplitude: 17, halfHeight: 31, color: 2, phase: 0 },
        { center: 338, amplitude: 17, halfHeight: 29, color: 4, phase: Math.PI }
    ];
    for (const band of waveBands) {
        for (let x = left; x < right; x += 1) {
            const logicalX = x / scale;
            const center = (
                band.center +
                band.amplitude * Math.sin((logicalX - 82) * Math.PI / 80 + band.phase)
            ) * scale;
            const top = Math.max(0, Math.floor(center - band.halfHeight * scale));
            const bottom = Math.min(size - 1, Math.ceil(center + band.halfHeight * scale));
            for (let y = top; y <= bottom; y += 1) {
                pixels[y * size + x] = band.color;
            }
        }
    }

    const accentStart = Math.round(278 * scale);
    for (let x = left; x < right; x += 1) {
        if (x < accentStart) {
            continue;
        }
        const logicalX = x / scale;
        const center = (
            235 + 17 * Math.sin((logicalX - 82) * Math.PI / 80)
        ) * scale;
        const top = Math.max(0, Math.floor(center - 31 * scale));
        const bottom = Math.min(size - 1, Math.ceil(center + 31 * scale));
        for (let y = top; y <= bottom; y += 1) {
            pixels[y * size + x] = 3;
        }
    }

    drawCircle(
        pixels,
        size,
        376 * scale,
        133 * scale,
        45 * scale,
        5
    );
    drawCircle(
        pixels,
        size,
        376 * scale,
        133 * scale,
        19 * scale,
        6
    );

    const scanlines = Buffer.alloc((size + 1) * size);
    for (let y = 0; y < size; y += 1) {
        const rowOffset = y * (size + 1);
        scanlines[rowOffset] = 0;
        pixels.copy(scanlines, rowOffset + 1, y * size, (y + 1) * size);
    }

    const header = Buffer.alloc(13);
    header.writeUInt32BE(size, 0);
    header.writeUInt32BE(size, 4);
    header[8] = 8;
    header[9] = 3;
    header[10] = 0;
    header[11] = 0;
    header[12] = 0;
    return Buffer.concat([
        PNG_SIGNATURE,
        pngChunk('IHDR', header),
        pngChunk('PLTE', PALETTE),
        pngChunk('IDAT', zlib.deflateSync(scanlines, { level: 9 })),
        pngChunk('IEND', Buffer.alloc(0))
    ]);
}

function renderPwaIcons(webRoot) {
    for (const size of [192, 512]) {
        const output = path.join(webRoot, `aquacyd-icon-${size}.png`);
        fs.writeFileSync(output, renderIndexedIcon(size));
    }
}

if (require.main === module) {
    renderPwaIcons(path.resolve(__dirname, '..', 'web'));
}

module.exports = { renderPwaIcons };
