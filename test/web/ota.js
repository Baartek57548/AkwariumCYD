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
}

function initOTA() {
    const fileInput = document.getElementById('firmware-file');
    const uploadBtn = document.getElementById('upload-btn');
    const wrapper = document.getElementById('firmware-upload-wrapper');

    if (!fileInput || !uploadBtn) {
        return;
    }

    setOtaUploadReadyState(false);

    fileInput.addEventListener('change', (event) => {
        if (event.target.files.length > 0) {
            const file = event.target.files[0];
            if (file.name.endsWith('.bin')) {
                setOtaUploadReadyState(true, file.name);
            } else {
                alert('Prosze wybrac poprawny plik firmware z rozszerzeniem .bin');
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

function simulateOTA() {
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

    const selectedFileName = firmwareFile.files[0]?.name || '';

    const formData = new FormData();
    formData.append('update', firmwareFile.files[0]);

    progressContainer.style.display = 'block';
    btn.disabled = true;

    const xhr = new XMLHttpRequest();
    xhr.open('POST', API_OTA, true);

    xhr.upload.onprogress = function (event) {
        if (!event.lengthComputable) return;
        const progress = Math.min(100, Math.round((event.loaded / event.total) * 100));
        fill.style.width = `${progress}%`;
        percentTxt.textContent = `${progress}%`;
    };

    xhr.onload = function () {
        if (xhr.status >= 200 && xhr.status < 300) {
            btn.textContent = 'Wgrano pakiet OTA';
            btn.style.backgroundColor = 'var(--success-color)';

            setTimeout(() => {
                alert('Aktualizacja zakonczona pomyslnie. Urzadzenie zrestartuje sie za chwile.');
                progressContainer.style.display = 'none';
                fill.style.width = '0%';
                percentTxt.textContent = '0%';
                btn.style.backgroundColor = '';
                if (firmwareFile) {
                    firmwareFile.value = '';
                }
                setOtaUploadReadyState(false);
            }, 1000);
        }
    };

    xhr.onerror = function () {
        btn.textContent = 'Blad sieci OTA';
        btn.style.backgroundColor = 'var(--danger-color)';
        alert('Blad polaczenia podczas OTA.');
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
