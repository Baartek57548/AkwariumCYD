#include "sensor_calibration_store.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

namespace {

constexpr char CALIBRATION_NAMESPACE[] = "aq_cal";
constexpr char CALIBRATION_KEY[] = "record";
constexpr uint32_t CALIBRATION_MAGIC = 0x314C4143UL; // CAL1

struct __attribute__((packed)) PersistentCalibration {
    uint32_t magic;
    aquarium::SensorCalibration calibration;
    uint32_t crc32;
};

StaticSemaphore_t calibration_mutex_storage;
SemaphoreHandle_t calibration_mutex =
    xSemaphoreCreateMutexStatic(&calibration_mutex_storage);
aquarium::SensorCalibration active_calibration =
    aquarium::default_sensor_calibration();

uint32_t crc32_bytes(const void *buffer, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
    for (size_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U
                      ? (crc >> 1U) ^ 0xEDB88320UL
                      : crc >> 1U;
        }
    }
    return ~crc;
}

uint32_t record_crc(const PersistentCalibration &record) {
    return crc32_bytes(
        &record, sizeof(record) - sizeof(record.crc32));
}

bool lock_calibration() {
    return calibration_mutex != nullptr &&
           xSemaphoreTake(calibration_mutex, pdMS_TO_TICKS(100U)) == pdTRUE;
}

bool persist(const aquarium::SensorCalibration &calibration) {
    PersistentCalibration record = {};
    record.magic = CALIBRATION_MAGIC;
    record.calibration = calibration;
    record.crc32 = record_crc(record);

    Preferences storage;
    if (!storage.begin(CALIBRATION_NAMESPACE, false)) {
        return false;
    }
    const bool saved =
        storage.putBytes(CALIBRATION_KEY, &record, sizeof(record)) ==
        sizeof(record);
    storage.end();
    return saved;
}

} // namespace

bool sensor_calibration_store_initialize(void) {
    aquarium::SensorCalibration loaded =
        aquarium::default_sensor_calibration();
    bool valid = false;

    Preferences storage;
    if (storage.begin(CALIBRATION_NAMESPACE, true)) {
        PersistentCalibration record = {};
        const size_t bytes =
            storage.getBytes(CALIBRATION_KEY, &record, sizeof(record));
        storage.end();
        valid = bytes == sizeof(record) &&
                record.magic == CALIBRATION_MAGIC &&
                record.crc32 == record_crc(record) &&
                aquarium::validate_sensor_calibration(record.calibration);
        if (valid) {
            loaded = record.calibration;
        }
    }

    if (!lock_calibration()) {
        return false;
    }
    active_calibration = loaded;
    xSemaphoreGive(calibration_mutex);

    if (!valid && !persist(loaded)) {
        Serial.println("CALIBRATION: cannot persist defaults.");
        return false;
    }
    Serial.printf(
        "CALIBRATION: %s pH[%d..%d] EC[%d=>%.1f uS/cm].\n",
        valid ? "loaded" : "defaults",
        static_cast<int>(loaded.ph_low_raw),
        static_cast<int>(loaded.ph_high_raw),
        static_cast<int>(loaded.ec_reference_raw),
        static_cast<double>(loaded.ec_reference_us_cm));
    return true;
}

aquarium::SensorCalibration sensor_calibration_store_snapshot(void) {
    aquarium::SensorCalibration snapshot =
        aquarium::default_sensor_calibration();
    if (!lock_calibration()) {
        return snapshot;
    }
    snapshot = active_calibration;
    xSemaphoreGive(calibration_mutex);
    return snapshot;
}

bool sensor_calibration_store_save(
    const aquarium::SensorCalibration &calibration) {
    if (!aquarium::validate_sensor_calibration(calibration) ||
        !lock_calibration()) {
        return false;
    }
    // Keep NVS and the in-memory snapshot in one critical section. A failed
    // durable write must never expose a value that disappears after reboot,
    // and a mutex timeout must never leave NVS newer than active RAM.
    const bool saved = persist(calibration);
    if (saved) {
        active_calibration = calibration;
    }
    xSemaphoreGive(calibration_mutex);
    return saved;
}

bool sensor_calibration_store_save_ph(int16_t low_raw,
                                      float low_reference,
                                      int16_t high_raw,
                                      float high_reference) {
    if (!lock_calibration()) {
        return false;
    }
    aquarium::SensorCalibration calibration = active_calibration;
    calibration.ph_low_raw = low_raw;
    calibration.ph_low_reference = low_reference;
    calibration.ph_high_raw = high_raw;
    calibration.ph_high_reference = high_reference;
    const bool saved =
        aquarium::validate_sensor_calibration(calibration) &&
        persist(calibration);
    if (saved) {
        active_calibration = calibration;
    }
    xSemaphoreGive(calibration_mutex);
    return saved;
}

bool sensor_calibration_store_save_ec(int16_t reference_raw,
                                      float reference_us_cm,
                                      float temperature_coefficient,
                                      float reference_temperature_c) {
    if (!lock_calibration()) {
        return false;
    }
    aquarium::SensorCalibration calibration = active_calibration;
    calibration.ec_reference_raw = reference_raw;
    calibration.ec_reference_us_cm = reference_us_cm;
    calibration.ec_temperature_coefficient =
        temperature_coefficient;
    calibration.ec_reference_temperature_c =
        reference_temperature_c;
    const bool saved =
        aquarium::validate_sensor_calibration(calibration) &&
        persist(calibration);
    if (saved) {
        active_calibration = calibration;
    }
    xSemaphoreGive(calibration_mutex);
    return saved;
}

bool sensor_calibration_store_reset_defaults(void) {
    return sensor_calibration_store_save(
        aquarium::default_sensor_calibration());
}
