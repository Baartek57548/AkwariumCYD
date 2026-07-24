const MAX_OTA_FILE_BYTES = 4 * 1024 * 1024;

function setOtaUploadReadyState(hasValidFile, fileName = '') {
    const uploadBtn = document.getElementById('upload-btn');
    const wrapper = document.getElementById('firmware-upload-wrapper');
    const fileNameLabel = document.getElementById('firmware-file-name');
    if (!uploadBtn) return;

    uploadBtn.disabled = !hasValidFile;
    uploadBtn.textContent = hasValidFile && fileName ? `Aktualizuj System (${fileName})` : 'Aktualizuj System';
    uploadBtn.classList.toggle('is-ready', hasValidFile);
    uploadBtn.style.opacity = hasValidFile ? '1' : '0.5';

    if (wrapper) {
        wrapper.classList.toggle('is-ready', hasValidFile);
    }

    if (fileNameLabel) {
        fileNameLabel.textContent = hasValidFile && fileName ? `Wybrany plik: ${fileName}` : 'Nie wybrano pliku .bin';
    }

    setCommandStatus(
        'ota-strip-file',
        hasValidFile && fileName ? fileName : 'Brak pliku',
        hasValidFile ? 'Pakiet gotowy do wyslania' : 'Akceptowany firmware .bin do 4 MB',
        hasValidFile ? 'ok' : 'neutral'
    );
}

function updateOtaPinStatus() {
    const isAdmin = typeof isAdminAuthenticated === 'function' && isAdminAuthenticated();
    setCommandStatus(
        'ota-strip-pin',
        isAdmin ? 'Admin' : 'Gość',
        isAdmin ? 'Dostęp administracyjny aktywny' : 'Zaloguj admina przed uploadem',
        isAdmin ? 'ok' : 'warn'
    );
}

function validateFirmwareFile(file) {
    if (!file) {
        return 'Nie wybrano pliku .bin';
    }
    if (!String(file.name || '').toLowerCase().endsWith('.bin')) {
        return 'Plik musi mieć rozszerzenie .bin';
    }
    if (Number(file.size) <= 0) {
        return 'Plik firmware jest pusty';
    }
    if (Number(file.size) > MAX_OTA_FILE_BYTES) {
        return 'Plik przekracza limit 4 MB';
    }
    return '';
}

function initOTA() {
    const fileInput = document.getElementById('firmware-file');
    const uploadBtn = document.getElementById('upload-btn');
    const wrapper = document.getElementById('firmware-upload-wrapper');

    if (!fileInput || !uploadBtn) {
        return;
    }

    setOtaUploadReadyState(false);
    updateOtaPinStatus();

    fileInput.addEventListener('change', (event) => {
        if (event.target.files.length > 0) {
            const file = event.target.files[0];
            const validationError = validateFirmwareFile(file);
            if (!validationError) {
                setOtaUploadReadyState(true, file.name);
            } else {
                alert(validationError);
                setOtaUploadReadyState(false);
                event.target.value = '';
            }
        } else {
            setOtaUploadReadyState(false);
        }
    });

    wrapper?.addEventListener('click', () => {
        fileInput.click();
    });
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

    if (!progressContainer || !fill || !percentTxt || !btn) return;

    const firmwareFile = document.getElementById('firmware-file');
    if (!firmwareFile || !firmwareFile.files || firmwareFile.files.length === 0) {
        alert('Najpierw wybierz plik .bin.');
        return;
    }

    const selectedFile = firmwareFile.files[0];
    const validationError = validateFirmwareFile(selectedFile);
    if (validationError) {
        alert(validationError);
        setOtaUploadReadyState(false);
        firmwareFile.value = '';
        return;
    }

    if (typeof isAdminAuthenticated === 'function' && !isAdminAuthenticated()) {
        try {
            await loginAsAdmin();
        } catch (error) {
            alert(error?.message || 'Logowanie admina wymagane przed aktualizacją OTA.');
            return;
        }
    }

    const servicePin = typeof getAdminPinForRequest === 'function' ? getAdminPinForRequest() : '';
    if (!servicePin) {
        alert('Dostęp admina jest nieaktywny. Zaloguj admina ponownie.');
        return;
    }

    const selectedFileName = selectedFile?.name || '';

    const formData = new FormData();
    formData.append('firmware', selectedFile, selectedFileName || 'firmware.bin');

    progressContainer.style.display = 'block';
    progressContainer.setAttribute('aria-valuenow', '0');
    progressContainer.setAttribute('aria-valuetext', 'Rozpoczynanie wysyłania firmware');
    btn.disabled = true;

    const xhr = new XMLHttpRequest();
    xhr.timeout = 120000;
    xhr.open('POST', `${API_OTA}?pin=${encodeURIComponent(servicePin)}`, true);

    xhr.upload.onprogress = function (event) {
        if (!event.lengthComputable) return;
        const progress = Math.min(100, Math.round((event.loaded / event.total) * 100));
        fill.style.width = `${progress}%`;
        percentTxt.textContent = `${progress}%`;
        progressContainer.setAttribute('aria-valuenow', String(progress));
        progressContainer.setAttribute('aria-valuetext', `Wysłano ${progress}% pakietu firmware`);
    };

    xhr.onload = function () {
        if (xhr.status >= 200 && xhr.status < 300) {
            btn.textContent = 'Wgrano pakiet OTA';
            btn.style.backgroundColor = 'var(--success-color)';
            progressContainer.setAttribute('aria-valuenow', '100');
            progressContainer.setAttribute('aria-valuetext', 'Pakiet firmware został wgrany');

            setTimeout(() => {
                alert('Aktualizacja zakończona pomyślnie. Urządzenie zrestartuje się za chwilę.');
                progressContainer.style.display = 'none';
                fill.style.width = '0%';
                percentTxt.textContent = '0%';
                progressContainer.setAttribute('aria-valuenow', '0');
                progressContainer.removeAttribute('aria-valuetext');
                btn.style.backgroundColor = '';
                if (firmwareFile) {
                    firmwareFile.value = '';
                }
                setOtaUploadReadyState(false);
            }, 1000);
        } else {
            btn.textContent = 'Blad OTA';
            btn.style.backgroundColor = 'var(--danger-color)';
            progressContainer.setAttribute('aria-valuetext', 'Aktualizacja OTA nie powiodła się');
            alert(xhr.responseText || 'Aktualizacja OTA nie powiodła się.');
        }
    };

    xhr.onerror = function () {
        btn.textContent = 'Blad sieci OTA';
        btn.style.backgroundColor = 'var(--danger-color)';
        progressContainer.setAttribute('aria-valuetext', 'Błąd połączenia podczas aktualizacji OTA');
        alert('Blad polaczenia podczas OTA.');
    };

    xhr.ontimeout = function () {
        btn.textContent = 'Timeout OTA';
        btn.style.backgroundColor = 'var(--danger-color)';
        progressContainer.setAttribute('aria-valuetext', 'Przekroczono czas wysyłania firmware');
        alert('Przekroczono limit czasu uploadu OTA.');
    };

    xhr.onloadend = function () {
        if (selectedFileName) {
            setOtaUploadReadyState(true, selectedFileName);
        } else {
            setOtaUploadReadyState(false);
        }
    };

    xhr.send(formData);
}
