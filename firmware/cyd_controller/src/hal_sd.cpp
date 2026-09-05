#include "hal_sd.h"

#include "config.h"

#include <SD.h>
#include <SPI.h>

namespace {

SPIClass sd_spi(VSPI);
bool sd_mounted = false;
uint32_t last_mount_attempt_ms = 0U;
constexpr uint32_t SD_MOUNT_COOLDOWN_MS = 5000U;

StaticSemaphore_t sd_mutex_storage;
SemaphoreHandle_t sd_mutex = nullptr;

bool ensure_sd_mutex(void) {
    if (sd_mutex != nullptr) {
        return true;
    }
    sd_mutex = xSemaphoreCreateRecursiveMutexStatic(&sd_mutex_storage);
    return sd_mutex != nullptr;
}

} // namespace

bool hal_sd_lock(uint32_t timeout_ms) {
    if (!ensure_sd_mutex()) {
        return false;
    }
    return xSemaphoreTakeRecursive(sd_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void hal_sd_unlock(void) {
    if (sd_mutex != nullptr) {
        xSemaphoreGiveRecursive(sd_mutex);
    }
}

void hal_sd_unmount(void) {
    if (hal_sd_lock(1000U)) {
        if (sd_mounted) {
            SD.end();
            sd_mounted = false;
            last_mount_attempt_ms = millis();
            Serial.println("SD: unmounted.");
        }
        hal_sd_unlock();
    }
}

void hal_sd_notify_io_error(void) {
    // Transient error: do not unmount card to keep existing filesystem state intact
}

bool hal_sd_check_health(void) {
    return sd_mounted;
}

bool hal_sd_init(void)
{
    if (!ensure_sd_mutex()) {
        return false;
    }

    if (!hal_sd_lock(1000U)) {
        return false;
    }

    if (sd_mounted) {
        hal_sd_unlock();
        return true;
    }

    const uint32_t now_ms = millis();
    if (last_mount_attempt_ms != 0U && (now_ms - last_mount_attempt_ms) < SD_MOUNT_COOLDOWN_MS) {
        hal_sd_unlock();
        return false;
    }
    last_mount_attempt_ms = now_ms;

    pinMode(HwConfig::SdCard::CS_PIN, OUTPUT);
    digitalWrite(HwConfig::SdCard::CS_PIN, HIGH);

    sd_spi.begin(HwConfig::SdCard::SCLK_PIN,
                 HwConfig::SdCard::MISO_PIN,
                 HwConfig::SdCard::MOSI_PIN,
                 HwConfig::SdCard::CS_PIN);

    sd_mounted = SD.begin(HwConfig::SdCard::CS_PIN,
                          sd_spi,
                          HwConfig::SdCard::SPI_FREQUENCY_HZ,
                          "/sd",
                          5,
                          false);

    if (!sd_mounted) {
        Serial.println("SD: mount failed; boot will continue without SD assets.");
        hal_sd_unlock();
        return false;
    }

    const uint8_t card_type = SD.cardType();
    if (card_type == CARD_NONE) {
        sd_mounted = false;
        Serial.println("SD: no card detected after mount.");
        SD.end();
        hal_sd_unlock();
        return false;
    }

    Serial.printf("SD: mounted, type=%u, size=%llu MB\n",
                  static_cast<unsigned>(card_type),
                  static_cast<unsigned long long>(SD.cardSize() / (1024ULL * 1024ULL)));
    hal_sd_unlock();
    return true;
}

bool hal_sd_is_mounted(void)
{
    return sd_mounted;
}

uint64_t hal_sd_card_size_bytes(void)
{
    if (!sd_mounted) {
        return 0ULL;
    }
    if (hal_sd_lock(200U)) {
        const uint64_t sz = sd_mounted ? SD.cardSize() : 0ULL;
        hal_sd_unlock();
        return sz;
    }
    return 0ULL;
}
