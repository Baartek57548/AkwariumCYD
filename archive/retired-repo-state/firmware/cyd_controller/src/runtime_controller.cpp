#include "runtime_controller.h"

#include "config.h"
#include "dev_simulator.h"
#include "events.h"
#include "gui_app.h"
#include "runtime_safety.h"
#include "sensor_calibration_store.h"
#include "uart_client.h"

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
constexpr uint32_t IO_TASK_STACK_BYTES = 10240U;
constexpr UBaseType_t IO_TASK_PRIORITY = 2U;

StaticQueue_t telemetry_queue_storage;
uint8_t telemetry_queue_buffer[sizeof(RuntimeTelemetry)] = {};
QueueHandle_t telemetry_queue = nullptr;
TaskHandle_t io_task_handle = nullptr;

RuntimeTelemetry latest = {};
uint32_t last_telemetry_ms = 0U;
uint32_t telemetry_sequence = 0U;
volatile uint32_t io_heartbeat_ms = 0U;

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
    frame.water_level_max_valid = true;
    if (!sample.waterLevelMaxReached) {
        frame.mcp_state |= static_cast<uint16_t>(
            1U << static_cast<uint8_t>(HwConfig::CH_WATER_LEVEL_MAX));
    }
    if (sample.waterLevelHigh) {
        frame.mcp_state |= static_cast<uint16_t>(
            1U << static_cast<uint8_t>(HwConfig::CH_WATER_LEVEL));
    }
    if (sample.flowActive) {
        frame.mcp_state |= static_cast<uint16_t>(
            1U << static_cast<uint8_t>(HwConfig::CH_FLOW_PULSE));
    }
}

void sample_hmi_telemetry(RuntimeTelemetry &frame) {
    frame.ldr_value = analogRead(HwConfig::LDR_PIN);
    frame.ldr_valid = frame.ldr_value >= 0 && frame.ldr_value <= 4095;

    if (g_uart_client.isControllerOnline()) {
        const auto &t = g_uart_client.getLatestTelemetry();
        frame.temperature_c = t.temperature;
        frame.temperature_present = true;
        frame.temperature_valid = (t.temperature > -50.0f && t.temperature < 80.0f);
        frame.temperature_stale = false;
        frame.temperature_age_ms = 0;
        frame.temperature_error_count = 0;

        frame.adc_present = true;
        frame.ph_value = t.ph;
        frame.ph_valid = (t.ph > 0.0f && t.ph < 14.0f);
        frame.ec_value = t.ec;
        frame.ec_valid = (t.ec >= 0.0f);

        uint8_t inputs = 0;
        // mcp_state zachowuje fizyczną polaryzację Portu B. Pływak aktywny
        // zwiera wejście do GND, zatem aktywny MIN/MAX zapisuje bit jako 0.
        if (!t.water_level_max_reached) inputs |= (1U << 1);
        if (!t.water_level_low) inputs |= (1U << 2);

        frame.mcp_present = true;
        // Starszy WeMos nadal udostępnia MIN. Jawnie nieudany odczyt nowego
        // firmware unieważnia całą próbkę GPIOB, ale brak pól MAX już nie.
        frame.mcp_valid = !t.water_level_max_present || t.water_level_max_valid;
        frame.mcp_state = static_cast<uint16_t>(t.relay_states) |
                          (static_cast<uint16_t>(inputs) << 8);
        frame.water_level_max_valid = t.water_level_max_valid;
    } else {
        frame.temperature_present = false;
        frame.temperature_valid = false;
        frame.temperature_c = NAN;
        frame.temperature_stale = true;
        frame.temperature_age_ms = 0;
        frame.temperature_error_count = 0;

        frame.adc_present = false;
        frame.ph_valid = false;
        frame.ph_value = NAN;
        frame.ec_valid = false;
        frame.ec_value = NAN;

        frame.mcp_present = false;
        frame.mcp_valid = false;
        frame.mcp_state = 0U;
        frame.water_level_max_valid = false;
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
    RuntimeTelemetry frame = latest;

    for (;;) {
        const uint32_t now_ms = millis();
        g_uart_client.update();
        gui_app_service_background();
        io_heartbeat_ms = now_ms;
        runtime_safety_heartbeat(
            RuntimeSafetyTask::Io, now_ms, ESP.getFreeHeap());

        if (last_telemetry_ms == 0U ||
            static_cast<uint32_t>(now_ms - last_telemetry_ms) >= TELEMETRY_INTERVAL_MS) {
            last_telemetry_ms = now_ms;
            if (developer_mode_snapshot()) {
                sample_developer_mode(frame, now_ms);
            } else {
                sample_hmi_telemetry(frame);
            }
            publish_telemetry(frame, now_ms);
        }

        vTaskDelay(pdMS_TO_TICKS(IO_TASK_PERIOD_MS));
    }
}

} // namespace

bool runtime_controller_prepare_hardware(void) {
    g_uart_client.begin();

    pinMode(HwConfig::LDR_PIN, INPUT);
    analogRead(HwConfig::LDR_PIN);
    analogSetPinAttenuation(HwConfig::LDR_PIN, ADC_11db);

    sensor_calibration_store_initialize();

    AQUARIUM_LOG_PRINTLN("HAL: Terminal HMI gotowy (UART0, LDR, Audio, SD). Brak lokalnych przekaznikow.");
    return true;
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
        AQUARIUM_LOG_PRINTLN("RUNTIME: nie mozna utworzyc kolejki telemetrii.");
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
        AQUARIUM_LOG_PRINTLN("RUNTIME: nie mozna uruchomic zadania Core 0.");
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
