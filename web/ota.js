const OTA_HEADER_BYTES = 512;
const OTA_SIGNATURE_OFFSET = 128;
const OTA_MAX_PACKAGE_BYTES = 1966080 + OTA_HEADER_BYTES;
const OTA_MAGIC = 'AQCYDOTA';
const OTA_PRODUCT_ID = 'aquacyd-cyd';
const OTA_KEY_ID_PATTERN = /^[0-9a-f]{16}$/;
const OTA_VERSION_PATTERN = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
const OTA_COMMIT_PATTERN = /^[0-9a-f]{7,19}$/;
const OTA_TARGET_NAMES = Object.freeze({ 1: 'ili9341', 2: 'st7789' });

let otaUploadInFlight = false;
let validatedOtaPackage = null;

function setOtaUploadReadyState(hasValidFile, fileName = '', detail = '') {
    const uploadBtn = document.getElementById('upload-btn');
    const wrapper = document.getElementById('firmware-upload-wrapper');
    const fileNameLabel = document.getElementById('firmware-file-name');
    if (!uploadBtn) return;

    uploadBtn.disabled = !hasValidFile;
    uploadBtn.textContent = hasValidFile && fileName
        ? `Aktualizuj system (${fileName})`
        : 'Aktualizuj system';
    uploadBtn.classList.toggle('is-ready', hasValidFile);
    uploadBtn.style.opacity = hasValidFile ? '1' : '0.5';
    wrapper?.classList.toggle('is-ready', hasValidFile);

    if (fileNameLabel) {
        fileNameLabel.textContent = hasValidFile && fileName
            ? `Zweryfikowany pakiet: ${fileName}${detail ? ` • ${detail}` : ''}`
            : 'Nie wybrano podpisanego pliku .aqfw';
    }

    setCommandStatus(
        'ota-strip-file',
        hasValidFile && fileName ? fileName : 'Brak pliku',
        hasValidFile
            ? (detail || 'Pakiet gotowy do wysłania')
            : 'Podpisany firmware .aqfw, maks. 1,88 MiB',
        hasValidFile ? 'ok' : 'neutral'
    );
}

function updateOtaPinStatus() {
    const isAdmin = typeof isAdminAuthenticated === 'function' && isAdminAuthenticated();
    setCommandStatus(
        'ota-strip-pin',
        isAdmin ? 'Admin' : 'Gość',
        isAdmin ? 'Bezpieczna sesja administratora aktywna' : 'Zaloguj admina przed uploadem',
        isAdmin ? 'ok' : 'warn'
    );
}

function readCanonicalAscii(bytes, offset, size, pattern, label) {
    const field = bytes.subarray(offset, offset + size);
    const terminator = field.indexOf(0);
    if (terminator <= 0) {
        throw new Error(`${label}: brak poprawnego zakończenia pola.`);
    }
    for (let index = terminator; index < field.length; index += 1) {
        if (field[index] !== 0) {
            throw new Error(`${label}: niekanoniczne wypełnienie pola.`);
        }
    }
    const value = new TextDecoder('ascii', { fatal: true }).decode(field.subarray(0, terminator));
    if (!pattern.test(value)) {
        throw new Error(`${label}: nieprawidłowa wartość.`);
    }
    return value;
}

function readExactAscii(bytes, offset, size, pattern, label) {
    const value = new TextDecoder('ascii', { fatal: true })
        .decode(bytes.subarray(offset, offset + size));
    if (!pattern.test(value)) {
        throw new Error(`${label}: nieprawidłowa wartość.`);
    }
    return value;
}

function bytesToHex(bytes) {
    let output = '';
    for (const value of bytes) {
        output += value.toString(16).padStart(2, '0');
    }
    return output;
}

function compareVersions(left, right) {
    const parse = (value) => {
        const match = OTA_VERSION_PATTERN.exec(String(value || ''));
        if (!match) return null;
        return match.slice(1).map((component) => Number(component));
    };
    const leftParts = parse(left);
    const rightParts = parse(right);
    if (!leftParts || !rightParts) {
        throw new Error('Pakiet lub sterownik zgłasza nieprawidłową wersję firmware.');
    }
    for (let index = 0; index < 3; index += 1) {
        if (leftParts[index] < rightParts[index]) return -1;
        if (leftParts[index] > rightParts[index]) return 1;
    }
    return 0;
}

function sha256Fallback(bytes) {
    const constants = new Uint32Array([
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    ]);
    const paddedLength = Math.ceil((bytes.length + 9) / 64) * 64;
    const padded = new Uint8Array(paddedLength);
    padded.set(bytes);
    padded[bytes.length] = 0x80;
    const paddedView = new DataView(padded.buffer);
    const bitLength = bytes.length * 8;
    paddedView.setUint32(paddedLength - 8, Math.floor(bitLength / 0x100000000), false);
    paddedView.setUint32(paddedLength - 4, bitLength >>> 0, false);

    const state = new Uint32Array([
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    ]);
    const words = new Uint32Array(64);
    const rotateRight = (value, bits) => (value >>> bits) | (value << (32 - bits));

    for (let block = 0; block < paddedLength; block += 64) {
        for (let index = 0; index < 16; index += 1) {
            words[index] = paddedView.getUint32(block + index * 4, false);
        }
        for (let index = 16; index < 64; index += 1) {
            const s0 = rotateRight(words[index - 15], 7) ^
                rotateRight(words[index - 15], 18) ^
                (words[index - 15] >>> 3);
            const s1 = rotateRight(words[index - 2], 17) ^
                rotateRight(words[index - 2], 19) ^
                (words[index - 2] >>> 10);
            words[index] = (words[index - 16] + s0 + words[index - 7] + s1) >>> 0;
        }

        let a = state[0];
        let b = state[1];
        let c = state[2];
        let d = state[3];
        let e = state[4];
        let f = state[5];
        let g = state[6];
        let h = state[7];
        for (let index = 0; index < 64; index += 1) {
            const sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const choice = (e & f) ^ (~e & g);
            const temp1 = (h + sum1 + choice + constants[index] + words[index]) >>> 0;
            const sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const majority = (a & b) ^ (a & c) ^ (b & c);
            const temp2 = (sum0 + majority) >>> 0;
            h = g;
            g = f;
            f = e;
            e = (d + temp1) >>> 0;
            d = c;
            c = b;
            b = a;
            a = (temp1 + temp2) >>> 0;
        }
        state[0] = (state[0] + a) >>> 0;
        state[1] = (state[1] + b) >>> 0;
        state[2] = (state[2] + c) >>> 0;
        state[3] = (state[3] + d) >>> 0;
        state[4] = (state[4] + e) >>> 0;
        state[5] = (state[5] + f) >>> 0;
        state[6] = (state[6] + g) >>> 0;
        state[7] = (state[7] + h) >>> 0;
    }

    const digest = new Uint8Array(32);
    const digestView = new DataView(digest.buffer);
    state.forEach((value, index) => digestView.setUint32(index * 4, value, false));
    return digest;
}

async function sha256(bytes) {
    if (globalThis.crypto?.subtle) {
        return new Uint8Array(await globalThis.crypto.subtle.digest('SHA-256', bytes));
    }
    await new Promise((resolve) => setTimeout(resolve, 0));
    return sha256Fallback(bytes);
}

async function fetchOtaCapabilities() {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 5000);
    try {
        const response = await fetch('/api/v2/capabilities', {
            cache: 'no-store',
            signal: controller.signal
        });
        const payload = await response.json();
        const data = payload?.data;
        const ota = data?.ota;
        if (!response.ok || payload?.v !== 2 || !data || !ota) {
            throw new Error('Sterownik nie udostępnił kontraktu bezpiecznego OTA.');
        }
        return { data, ota };
    } catch (error) {
        if (error?.name === 'AbortError') {
            throw new Error('Sterownik nie odpowiedział podczas weryfikacji pakietu.');
        }
        throw error;
    } finally {
        clearTimeout(timeout);
    }
}

async function inspectFirmwarePackage(file) {
    if (!file) {
        throw new Error('Nie wybrano pliku .aqfw.');
    }
    if (!String(file.name || '').toLowerCase().endsWith('.aqfw')) {
        throw new Error('Plik musi mieć rozszerzenie .aqfw.');
    }
    if (Number(file.size) <= OTA_HEADER_BYTES) {
        throw new Error('Pakiet firmware jest pusty lub ucięty.');
    }
    if (Number(file.size) > OTA_MAX_PACKAGE_BYTES) {
        throw new Error('Pakiet przekracza pojemność partycji OTA sterownika.');
    }

    const header = new Uint8Array(await file.slice(0, OTA_HEADER_BYTES).arrayBuffer());
    const view = new DataView(header.buffer, header.byteOffset, header.byteLength);
    const magic = new TextDecoder('ascii').decode(header.subarray(0, 8));
    if (magic !== OTA_MAGIC) throw new Error('Nieprawidłowy format pakietu AquaCYD.');
    if (view.getUint16(8, true) !== 1 || view.getUint16(10, true) !== OTA_HEADER_BYTES) {
        throw new Error('Ta wersja formatu .aqfw nie jest obsługiwana.');
    }
    if (header[12] !== 1 || view.getUint16(14, true) !== 0 ||
        view.getUint16(126, true) !== 0) {
        throw new Error('Pakiet używa nieobsługiwanego algorytmu lub flag.');
    }
    const target = OTA_TARGET_NAMES[header[13]];
    if (!target) throw new Error('Pakiet nie określa obsługiwanego wariantu ekranu.');

    const imageBytes = view.getUint32(16, true);
    const securityVersion = view.getUint32(20, true);
    const expectedDigest = bytesToHex(header.subarray(24, 56));
    const version = readCanonicalAscii(
        header, 56, 16, OTA_VERSION_PATTERN, 'Wersja firmware'
    );
    const productId = readCanonicalAscii(
        header, 72, 16, /^[a-z0-9-]+$/, 'Identyfikator produktu'
    );
    const keyId = readExactAscii(
        header, 88, 16, OTA_KEY_ID_PATTERN, 'Identyfikator klucza'
    );
    const commit = readCanonicalAscii(
        header, 104, 20, OTA_COMMIT_PATTERN, 'Commit'
    );
    const minimumBootloaderVersion = view.getUint16(124, true);
    const signature = header.subarray(OTA_SIGNATURE_OFFSET, OTA_HEADER_BYTES);
    if (!signature.some((value) => value !== 0)) {
        throw new Error('Pakiet nie zawiera podpisu wydawcy.');
    }
    if (imageBytes !== file.size - OTA_HEADER_BYTES) {
        throw new Error('Rozmiar obrazu nie zgadza się z podpisanymi metadanymi.');
    }
    if (imageBytes < 4096 || imageBytes % 4096 !== 0) {
        throw new Error('Obraz nie ma formatu gotowego dla Secure Boot v2.');
    }

    const payload = new Uint8Array(await file.slice(OTA_HEADER_BYTES).arrayBuffer());
    if (payload[payload.length - 4096] !== 0xe7) {
        throw new Error('Brak bloku podpisu Secure Boot v2 w obrazie firmware.');
    }
    const actualDigest = bytesToHex(await sha256(payload));
    if (actualDigest !== expectedDigest) {
        throw new Error('Suma SHA-256 obrazu nie zgadza się z pakietem.');
    }

    const capabilities = await fetchOtaCapabilities();
    const ota = capabilities.ota;
    if (productId !== OTA_PRODUCT_ID || ota.productId !== productId) {
        throw new Error('Pakiet jest przeznaczony dla innego produktu.');
    }
    if (ota.target !== target) {
        throw new Error(`Pakiet ${target} nie pasuje do sterownika ${ota.target || 'unknown'}.`);
    }
    if (ota.keyId !== keyId) {
        throw new Error('Pakiet został podpisany niezaufanym kluczem wydawcy.');
    }
    if (imageBytes > Number(ota.updatePartitionBytes || 0)) {
        throw new Error('Obraz nie mieści się w partycji aktualizacji sterownika.');
    }
    if (minimumBootloaderVersion > Number(ota.bootloaderVersion || 0)) {
        throw new Error('Bootloader sterownika jest zbyt stary dla tego pakietu.');
    }
    if (securityVersion < Number(ota.minimumSecurityVersion || 0)) {
        throw new Error('Pakiet został zablokowany przez ochronę anti-rollback.');
    }
    if (compareVersions(version, capabilities.data.firmwareVersion) < 0) {
        throw new Error('Downgrade firmware jest zablokowany.');
    }

    return {
        file,
        target,
        version,
        securityVersion,
        imageBytes,
        keyId,
        commit,
        digest: actualDigest
    };
}

function initOTA() {
    const fileInput = document.getElementById('firmware-file');
    const uploadBtn = document.getElementById('upload-btn');
    const wrapper = document.getElementById('firmware-upload-wrapper');
    if (!fileInput || !uploadBtn) return;

    setOtaUploadReadyState(false);
    updateOtaPinStatus();

    fileInput.addEventListener('change', async (event) => {
        validatedOtaPackage = null;
        setOtaUploadReadyState(false);
        const file = event.target.files?.[0];
        if (!file) return;
        setElementBusy(uploadBtn, true);
        try {
            const inspected = await inspectFirmwarePackage(file);
            if (event.target.files?.[0] !== file) return;
            validatedOtaPackage = inspected;
            setOtaUploadReadyState(
                true,
                file.name,
                `v${inspected.version} • ${inspected.target} • SHA-256 OK`
            );
            showToast(
                'Pakiet sprawdzony',
                'Target, wersja, rozmiar i SHA-256 są poprawne. Podpis sprawdzi sterownik.',
                'success',
                4800
            );
        } catch (error) {
            showToast(
                'Pakiet odrzucony',
                error?.message || 'Nie można zweryfikować pakietu firmware.',
                'error',
                6500
            );
            event.target.value = '';
            setOtaUploadReadyState(false);
        } finally {
            setElementBusy(uploadBtn, false);
        }
    });

    wrapper?.addEventListener('click', () => fileInput.click());
    wrapper?.addEventListener('keydown', (event) => {
        if (event.key === 'Enter' || event.key === ' ') {
            event.preventDefault();
            fileInput.click();
        }
    });
}

async function uploadFirmwarePackage() {
    const progressContainer = document.getElementById('ota-progress');
    const fill = document.getElementById('ota-fill');
    const percentTxt = document.getElementById('ota-percent');
    const btn = document.getElementById('upload-btn');
    const firmwareInput = document.getElementById('firmware-file');
    if (!progressContainer || !fill || !percentTxt || !btn || otaUploadInFlight) return;

    const selectedFile = firmwareInput?.files?.[0];
    if (!selectedFile || validatedOtaPackage?.file !== selectedFile) {
        showToast(
            'Brak zweryfikowanego pakietu',
            'Wybierz poprawny, podpisany plik firmware .aqfw.',
            'warning',
            4800
        );
        return;
    }

    if (typeof isAdminAuthenticated === 'function' && !isAdminAuthenticated()) {
        try {
            await loginAsAdmin();
        } catch (error) {
            showToast(
                'Wymagane logowanie',
                error?.message || 'Zaloguj administratora przed aktualizacją OTA.',
                'warning',
                4800
            );
            return;
        }
    }
    const sessionToken = typeof getAdminTokenForRequest === 'function'
        ? getAdminTokenForRequest()
        : '';
    if (!/^[0-9a-f]{32}$/i.test(sessionToken)) {
        showToast(
            'Brak bezpiecznej sesji',
            'Sesja administratora wygasła. Zaloguj się ponownie.',
            'error',
            4800
        );
        return;
    }

    const selectedFileName = selectedFile.name;
    const formData = new FormData();
    formData.append('firmware', selectedFile, selectedFileName);
    progressContainer.style.display = 'block';
    progressContainer.setAttribute('aria-valuenow', '0');
    progressContainer.setAttribute('aria-valuetext', 'Rozpoczynanie bezpiecznego OTA');
    otaUploadInFlight = true;
    setElementBusy(btn, true);

    const xhr = new XMLHttpRequest();
    let uploadSucceeded = false;
    xhr.timeout = 180000;
    xhr.open('POST', API_OTA, true);
    xhr.setRequestHeader('X-AquaCYD-Session', sessionToken);

    xhr.upload.onprogress = (event) => {
        if (!event.lengthComputable) return;
        const progress = Math.min(99, Math.round((event.loaded / event.total) * 99));
        fill.style.width = `${progress}%`;
        percentTxt.textContent = `${progress}%`;
        progressContainer.setAttribute('aria-valuenow', String(progress));
        progressContainer.setAttribute('aria-valuetext', `Wysłano ${progress}% pakietu firmware`);
    };

    xhr.onload = () => {
        let responsePayload = null;
        try {
            responsePayload = JSON.parse(xhr.responseText || '{}');
        } catch (_) {
            responsePayload = null;
        }
        if (xhr.status >= 200 && xhr.status < 300 && responsePayload?.ok === true) {
            uploadSucceeded = true;
            fill.style.width = '100%';
            percentTxt.textContent = '100%';
            btn.textContent = 'Pakiet zweryfikowany';
            btn.style.backgroundColor = 'var(--success-color)';
            progressContainer.setAttribute('aria-valuenow', '100');
            progressContainer.setAttribute('aria-valuetext', 'Podpis firmware został zweryfikowany');
            showToast(
                'Bezpieczna aktualizacja gotowa',
                responsePayload.message || 'Sterownik zweryfikował podpis i uruchomi się ponownie.',
                'success',
                7000
            );
            return;
        }
        if (xhr.status === 401 && typeof logoutAdmin === 'function') {
            logoutAdmin();
        }
        btn.textContent = 'Błąd OTA';
        btn.style.backgroundColor = 'var(--danger-color)';
        progressContainer.setAttribute('aria-valuetext', 'Bezpieczna aktualizacja nie powiodła się');
        showToast(
            'Pakiet odrzucony przez sterownik',
            responsePayload?.message || 'Podpis, target lub polityka OTA są nieprawidłowe.',
            'error',
            7000
        );
    };

    xhr.onerror = () => {
        btn.textContent = 'Błąd sieci OTA';
        btn.style.backgroundColor = 'var(--danger-color)';
        progressContainer.setAttribute('aria-valuetext', 'Błąd połączenia podczas OTA');
        showToast(
            'Błąd sieci OTA',
            'Połączenie zostało przerwane. Sterownik nie aktywuje niepełnego obrazu.',
            'error',
            7000
        );
    };

    xhr.ontimeout = () => {
        btn.textContent = 'Timeout OTA';
        btn.style.backgroundColor = 'var(--danger-color)';
        progressContainer.setAttribute('aria-valuetext', 'Przekroczono czas aktualizacji');
        showToast(
            'Timeout OTA',
            'Przekroczono trzyminutowy limit wysyłania firmware.',
            'error',
            7000
        );
    };

    xhr.onloadend = () => {
        otaUploadInFlight = false;
        setElementBusy(btn, false);
        if (uploadSucceeded) {
            validatedOtaPackage = null;
            if (firmwareInput) firmwareInput.value = '';
            setTimeout(() => {
                progressContainer.style.display = 'none';
                fill.style.width = '0%';
                percentTxt.textContent = '0%';
                btn.style.backgroundColor = '';
                setOtaUploadReadyState(false);
            }, 1200);
        } else {
            setOtaUploadReadyState(
                true,
                selectedFileName,
                `v${validatedOtaPackage.version} • ponów bezpiecznie`
            );
        }
    };

    xhr.send(formData);
}
