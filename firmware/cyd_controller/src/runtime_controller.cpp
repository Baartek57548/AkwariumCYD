#include "runtime_controller.h"

#include "config.h"
#include "dev_simulator.h"
#include "events.h"
#include "gui_app.h"
#include "hal_adc.h"
#include "hal_mcp23017.h"
#include "hal_onewire_bus.h"
#include "sensor_calibration.h"
#include "sensor_calibration_store.h"
#include "runtime_safety.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <math.h>
#include <string.h>

extern bool wifi_connected;
extern int wifi_rssi;
extern bool wifi_ota_active;
extern int clock_hour;
extern int clock_minute;
extern int clock_second;

namespace {

constexpr uint32_t IO_TASK_PERIOD_MS = 10U;
constexpr uint32_t TELEMETRY_INTERVAL_MS = 1000U;
constexpr uint32_t MCP_POLL_INTERVAL_MS = 100U;
constexpr uint32_t HAL_REPROBE_INTERVAL_MS = 5000U;
constexpr uint32_t IO_TASK_STACK_BYTES = 8192U;
constexpr UBaseType_t IO_TASK_PRIORITY = 2U;

StaticQueue_t telemetry_queue_storage;
uint8_t telemetry_queue_buffer[sizeof(RuntimeTelemetry)] = {};
QueueHandle_t telemetry_queue = nullptr;
TaskHandle_t io_task_handle = nullptr;

RuntimeTelemetry latest = {};
uint32_t last_telemetry_ms = 0U;
uint32_t last_mcp_poll_ms = 0U;
uint32_t last_hal_reprobe_ms = 0U;
uint32_t telemetry_sequence = 0U;
volatile uint32_t io_heartbeat_ms = 0U;
aquarium::MedianFilter ph_filter;
aquarium::MedianFilter ec_filter;

float ads_raw_to_voltage(int16_t raw) {
    return (static_cast<float>(raw) * 4.096f) / 32768.0f;
}

void publish_sample(SensorId id, float value, bool valid, uint32_t now_ms) {
    SensorSample sample = {};
    sample.id = id;
    sample.value = value;
    sample.timestampMs = now_ms;
    sample.valid = valid;
    events_publish_sample(sample);
}

void refresh_network_snapshot(RuntimeTelemetry &frame) {
    if (!gui_app_lock(20U)) {
        return;
    }
    frame.wifi_state = wifi_ota_active ? 2U : (wifi_connected ? 1U : 0U);
    frame.wifi_rssi = static_cast<int16_t>(wifi_rssi);
    gui_app_unlock();
}

bool developer_mode_snapshot() {
    return gui_app_is_dev_mode();
}

void refresh_temperature_snapshot(RuntimeTelemetry &frame, uint32_t now_ms) {
    HalTemperatureReading temperature = {};
    temperature.celsius =
        (frame.temperature_present || frame.temperature_valid)
            ? frame.temperature_c
            : NAN;
    temperature.present = frame.temperature_present;
    temperature.valid = frame.temperature_valid;
    temperature.stale = !frame.temperature_valid;

    // The HAL leaves this fallback untouched only when another diagnostic
    // operation briefly owns the OneWire mutex.
    hal_temperature_poll(now_ms, &temperature);
    frame.temperature_c = temperature.celsius;
    frame.temperature_present = temperature.present;
    frame.temperature_valid = temperature.valid;
    frame.temperature_stale = temperature.stale;
    frame.temperature_age_ms = temperature.age_ms;
    frame.temperature_error_count = temperature.error_count;
}

void sample_developer_mode(RuntimeTelemetry &frame, uint32_t now_ms) {
    const uint16_t minute_of_day =
        static_cast<uint16_t>(constrain(clock_hour, 0, 23)) * 60U +
        static_cast<uint16_t>(constrain(clock_minute, 0, 59));
    const aquarium::DevSnapshot &sample = aquarium::dev_simulator().step(
        now_ms, minute_of_day, static_cast<uint8_t>(constrain(clock_second, 0, 59)));

    frame.temperature_c = sample.temperatureC;
    frame.temperature_present = true;
    frame.temperature_valid = isfinite(sample.temperatureC);
    frame.temperature_stale = false;
    frame.temperature_age_ms = 0U;
    frame.temperature_error_count = 0U;
    frame.ldr_value = constrain(sample.ldr, 0, 4095);
    frame.ldr_valid = true;
    frame.adc_present = true;
    frame.ph_valid = true;
    frame.ph_raw = sample.phRaw;
    frame.ph_voltage = sample.phVoltage;
    frame.ph_value = sample.ph;
    frame.ec_valid = true;
    frame.ec_raw = sample.ecRaw;
    frame.ec_voltage = sample.ecVoltage;
    frame.ec_value = sample.ecConductivity;
    frame.mcp_present = true;
    frame.mcp_valid = true;
    frame.mcp_state = 0U;
    if (sample.waterLevelHigh) {
        frame.mcp_state |= static_cast<uint16_t>(
            1U << static_cast<uint8_t>(HwConfig::CH_WATER_LEVEL));
    }
    if (sample.leakDetected) {
        frame.mcp_state |= static_cast<uint16_t>(
            1U << static_cast<uint8_t>(HwConfig::CH_LEAK));
    }
    if (sample.flowActive) {
        frame.mcp_state |= static_cast<uint16_t>(
            1U << static_cast<uint8_t>(HwConfig::CH_FLOW_PULSE));
    }
}

void sample_physical_sensors(RuntimeTelemetry &frame, uint32_t now_ms) {
    frame.ldr_value = analogRead(HwConfig::LDR_PIN);
    frame.ldr_valid = frame.ldr_value >= 0 && frame.ldr_value <= 4095;

    refresh_temperature_snapshot(frame, now_ms);

    frame.adc_present = hal_adc_is_present();
    frame.ph_valid = false;
    frame.ec_valid = false;
    if (frame.adc_present) {
        const aquarium::SensorCalibration calibration =
            sensor_calibration_store_snapshot();
        int16_t raw = 0;
        if (hal_adc_read_raw(HwConfig::ADC_PH, &raw)) {
            const int16_t filtered = static_cast<int16_t>(
                lroundf(ph_filter.push(raw)));
            frame.ph_raw = filtered;
            frame.ph_voltage =
                ads_raw_to_voltage(filtered);
            frame.ph_value =
                aquarium::calibrate_ph(filtered, calibration);
            frame.ph_valid =
                ph_filter.size() ==
                    aquarium::kSensorMedianWindow &&
                isfinite(frame.ph_value);
        }
        if (hal_adc_read_raw(HwConfig::ADC_EC, &raw)) {
            const int16_t filtered = static_cast<int16_t>(
                lroundf(ec_filter.push(raw)));
            frame.ec_raw = filtered;
            frame.ec_voltage =
                ads_raw_to_voltage(filtered);
            frame.ec_value = aquarium::calibrate_ec(
                filtered,
                frame.temperature_c,
                frame.temperature_valid,
                calibration);
            frame.ec_valid =
                ec_filter.size() ==
                    aquarium::kSensorMedianWindow &&
                isfinite(frame.ec_value);
        }
    } else {
        ph_filter.reset();
        ec_filter.reset();
    }

    if (static_cast<uint32_t>(now_ms - last_mcp_poll_ms) >= MCP_POLL_INTERVAL_MS) {
        last_mcp_poll_ms = now_ms;
        frame.mcp_present = hal_mcp_is_present();
        frame.mcp_valid = frame.mcp_present && hal_mcp_read_all(&frame.mcp_state);
    }
}

void recover_missing_hardware(uint32_t now_ms) {
    if (static_cast<uint32_t>(now_ms - last_hal_reprobe_ms) < HAL_REPROBE_INTERVAL_MS) {
        return;
    }
    last_hal_reprobe_ms = now_ms;
    if (!hal_mcp_is_present()) {
        hal_mcp_init();
    }
    if (!hal_adc_is_present()) {
        hal_adc_init();
    }
}

void publish_telemetry(RuntimeTelemetry &frame, uint32_t now_ms) {
    frame.sequence = ++telemetry_sequence;
    frame.sampled_ms = now_ms;
    frame.uptime_seconds = static_cast<uint32_t>(
        static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL);
    frame.free_heap_bytes = ESP.getFreeHeap();
    refresh_network_snapshot(frame);

    publish_sample(SensorId::Temp, frame.temperature_c, frame.temperature_valid, now_ms);
    publish_sample(SensorId::Ph, frame.ph_value, frame.ph_valid, now_ms);
    publish_sample(SensorId::Ec, frame.ec_value, frame.ec_valid, now_ms);
    publish_sample(SensorId::Ldr,
                   frame.ldr_valid ? static_cast<float>(frame.ldr_value) : NAN,
                   frame.ldr_valid,
                   now_ms);
    publish_sample(SensorId::Heap, static_cast<float>(frame.free_heap_bytes), true, now_ms);
    publish_sample(SensorId::Uptime, static_cast<float>(frame.uptime_seconds), true, now_ms);

    xQueueOverwrite(telemetry_queue, &frame);
}

void io_task(void *) {
    runtime_safety_register_current_task(RuntimeSafetyTask::Io);
    TickType_t next_wake = xTaskGetTickCount();
    RuntimeTelemetry frame = latest;

    for (;;) {
        const uint32_t now_ms = millis();
        io_heartbeat_ms = now_ms;
        runtime_safety_heartbeat(
            RuntimeSafetyTask::Io, now_ms, ESP.getFreeHeap());
        gui_app_service_background();

        // DS18B20 jest maszyną stanów i musi być serwisowany częściej niż
        // publikowana jest telemetria.
        if (!developer_mode_snapshot()) {
            refresh_temperature_snapshot(frame, now_ms);
        }

        recover_missing_hardware(now_ms);
        if (last_telemetry_ms == 0U ||
            static_cast<uint32_t>(now_ms - last_telemetry_ms) >= TELEMETRY_INTERVAL_MS) {
            last_telemetry_ms = now_ms;
            if (developer_mode_snapshot()) {
                sample_developer_mode(frame, now_ms);
            } else {
                sample_physical_sensors(frame, now_ms);
            }
            publish_telemetry(frame, now_ms);
        }

        vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(IO_TASK_PERIOD_MS));
    }
}

} // namespace

bool runtime_controller_prepare_hardware(void) {
    pinMode(HwConfig::LDR_PIN, INPUT);
    analogRead(HwConfig::LDR_PIN);
    analogSetPinAttenuation(HwConfig::LDR_PIN, ADC_11db);

    const bool mcp_ok = hal_mcp_init();
    if (mcp_ok) {
        hal_mcp_all_relays_safe();
    }
    const bool adc_ok = hal_adc_init();
    const bool temperature_ok = hal_temperature_init();
    const bool calibration_ok = sensor_calibration_store_initialize();

    Serial.printf("HAL: MCP23017=%s ADS1115=%s DS18B20=%s CAL=%s\n",
                  mcp_ok ? "OK" : "BRAK",
                  adc_ok ? "OK" : "BRAK",
                  temperature_ok ? "OK" : "BRAK",
                  calibration_ok ? "OK" : "BLAD");
    return mcp_ok || adc_ok || temperature_ok;
}

uint32_t runtime_controller_last_heartbeat_ms(void) {
    return io_heartbeat_ms;
}

bool runtime_controller_start(void) {
    if (io_task_handle != nullptr) {
        return true;
    }
    if (telemetry_queue == nullptr) {
        telemetry_queue = xQueueCreateStatic(
            1U,
            sizeof(RuntimeTelemetry),
            telemetry_queue_buffer,
            &telemetry_queue_storage);
    }
    if (telemetry_queue == nullptr) {
        Serial.println("RUNTIME: nie można utworzyć kolejki telemetrii.");
        return false;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(
        io_task,
        "aquarium_io",
        IO_TASK_STACK_BYTES,
        nullptr,
        IO_TASK_PRIORITY,
        &io_task_handle,
        0);
    if (created != pdPASS) {
        io_task_handle = nullptr;
        Serial.println("RUNTIME: nie można uruchomić zadania Core 0.");
        return false;
    }
    return true;
}

bool runtime_controller_take_latest(RuntimeTelemetry *out) {
    return out != nullptr &&
           telemetry_queue != nullptr &&
           xQueueReceive(telemetry_queue, out, 0U) == pdTRUE;
}

bool runtime_controller_is_healthy(uint32_t now_ms,
                                   uint32_t maximum_heartbeat_age_ms) {
    if (io_task_handle == nullptr ||
        maximum_heartbeat_age_ms == 0U ||
        io_heartbeat_ms == 0U ||
        last_telemetry_ms == 0U) {
        return false;
    }
    const eTaskState state = eTaskGetState(io_task_handle);
    if (state == eDeleted || state == eInvalid) {
        return false;
    }
    return static_cast<uint32_t>(now_ms - io_heartbeat_ms) <=
               maximum_heartbeat_age_ms &&
           static_cast<uint32_t>(now_ms - last_telemetry_ms) <=
               maximum_heartbeat_age_ms;
}
