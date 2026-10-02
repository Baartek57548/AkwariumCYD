'use strict';

const fs = require('node:fs');
const path = require('node:path');
const terser = require('terser');

function minifyGatewayPwa(projectRoot) {
    const sourcePath = path.join(projectRoot, 'web', 'gateway-pwa.source.js');
    const outputPath = path.join(projectRoot, 'web', 'gateway-pwa.js');
    const source = fs.readFileSync(sourcePath, 'utf8');
    const result = terser.minify_sync(
        { 'gateway-pwa.source.js': source },
        {
            compress: {
                passes: 2,
                unsafe: false
            },
            mangle: true,
            ecma: 2020,
            format: {
                ascii_only: false,
                comments: false
            }
        }
    );
    if (typeof result.code !== 'string' || result.code.length === 0) {
        throw new Error('Terser did not produce the gateway PWA asset.');
    }
    fs.writeFileSync(outputPath, `${result.code}\n`, 'utf8');
}

function main() {
    const projectRoot = path.resolve(__dirname, '..');
    minifyGatewayPwa(projectRoot);
}

if (require.main === module) {
    try {
        main();
    } catch (error) {
        console.error(error);
        process.exitCode = 1;
    }
}

module.exports = { minifyGatewayPwa };
