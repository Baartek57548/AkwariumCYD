#include "ui_io.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#ifdef AQUARIUM_UI_HOST_PREVIEW
namespace ui_io {
namespace {
Completion pending{};
bool ready = false;
uint32_t completionAt = 0, nextDelay = 120;
} // namespace
bool start() { return true; }
bool displayCore() { return true; }
ui_runtime::Result submit(Operation operation, const ui_runtime::Configuration *config,
                          const ui_runtime::DateTime *clock) {
    if (ready)
        return {false, "Zapis trwa / spróbuj ponownie"};
    ui_runtime::Result result{false, "Nieprawidłowa operacja"};
    if (operation == Operation::Save && config)
        result = ui_runtime::save(*config);
    else if (operation == Operation::Reset)
        result = ui_runtime::resetStorage();
    else if (operation == Operation::Clock && clock)
        result = ui_runtime::setDateTime(*clock);
    pending.operation = operation;
    pending.ok = result.ok;
    std::snprintf(pending.message, sizeof(pending.message), "%s", result.message);
    ready = true;
    completionAt = millis() + nextDelay;
    nextDelay = 120;
    return {true, "Zapisywanie"};
}
bool take(Completion &completion) {
    if (!ready || static_cast<int32_t>(millis() - completionAt) < 0)
        return false;
    completion = pending;
    ready = false;
    return true;
}
bool videoService() { return true; }
ui_status::WirelessObservation radio() { return {}; }
void networkDetails(char *, size_t) {}
void publishNetwork(const char *) {}
void resetPreview() {
    ready = false;
    nextDelay = 120;
}
void delayNextCompletion(uint32_t milliseconds) { nextDelay = milliseconds; }
} // namespace ui_io
#else
#include "panel_hal.h"
#include <WiFi.h>
#include <atomic>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
namespace ui_io {
namespace {
struct Request {
    Operation operation;
    ui_runtime::Configuration config;
    ui_runtime::DateTime clock;
};
constexpr unsigned kQueueDepth = 4;
StaticQueue_t requestControl, completionControl;
uint8_t requestStorage[kQueueDepth * sizeof(Request)];
uint8_t completionStorage[kQueueDepth * sizeof(Completion)];
QueueHandle_t requests = nullptr, completions = nullptr;
StaticTask_t taskControl;
TaskHandle_t workerTask = nullptr;
StackType_t taskStack[8192 / sizeof(StackType_t)];
enum class Flash : uint8_t { Idle, Requested, Paused, Written, Resumed, Failed };
std::atomic<Flash> flash{Flash::Idle};
bool videoReady = true;
uint32_t videoRetryAt = 0;
portMUX_TYPE cacheMux = portMUX_INITIALIZER_UNLOCKED;
ui_status::WirelessObservation observation;
char details[192] = "Wi-Fi wyłączone\nUART: niepodłączony";
char networkState[96] = "Wi-Fi wyłączone";
void observe() {
    ui_status::WirelessObservation next;
    wifi_mode_t mode = WIFI_MODE_NULL;
    const esp_err_t result = esp_wifi_get_mode(&mode);
    next.available = result == ESP_OK || result == ESP_ERR_WIFI_NOT_INIT;
    next.enabled = result == ESP_OK && mode != WIFI_MODE_NULL;
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        wifi_ap_record_t ap{};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            next.stationConnected = true;
            next.rssi = ap.rssi;
            std::memcpy(next.stationName, ap.ssid, 32);
        }
    }
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        wifi_sta_list_t stations{};
        next.apActive = esp_wifi_ap_get_sta_list(&stations) == ESP_OK;
        next.clients = static_cast<uint8_t>(stations.num);
        wifi_config_t cfg{};
        if (next.apActive && esp_wifi_get_config(WIFI_IF_AP, &cfg) == ESP_OK)
            std::memcpy(next.apName, cfg.ap.ssid, 32);
    }
    const IPAddress ip = next.apActive ? WiFi.softAPIP() : WiFi.localIP();
    char text[192];
    std::snprintf(text, sizeof(text), "%s\nIP: %u.%u.%u.%u\nUART: niepodłączony", networkState, ip[0],
                  ip[1], ip[2], ip[3]);
    portENTER_CRITICAL(&cacheMux);
    observation = next;
    std::memcpy(details, text, sizeof(details));
    portEXIT_CRITICAL(&cacheMux);
}
bool waitFlash(Flash desired, uint32_t timeout) {
    const uint32_t start = millis();
    while (flash.load() != desired) {
        if (flash.load() == Flash::Failed || millis() - start >= timeout)
            return false;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
}
void worker(void *) {
    Request request;
    uint32_t lastObservation = 0;
    for (;;) {
        if (xQueueReceive(requests, &request, pdMS_TO_TICKS(10)) == pdTRUE) {
            ui_runtime::Result result{false, "Operacja nie została wykonana"};
            const bool writesFlash =
                request.operation == Operation::Save || request.operation == Operation::Reset;
            bool paused = false;
            if (writesFlash) {
                flash.store(Flash::Requested);
                paused = waitFlash(Flash::Paused, 2000);
                if (!paused) {
                    Flash expected = Flash::Requested;
                    if (!flash.compare_exchange_strong(expected, Flash::Idle))
                        paused = expected == Flash::Paused;
                }
            }
            if (!writesFlash || paused) {
                switch (request.operation) {
                case Operation::Save:
                    result = ui_runtime::save(request.config);
                    break;
                case Operation::Reset:
                    result = ui_runtime::resetStorage();
                    break;
                case Operation::Network:
                    result = ui_runtime::applyNetwork(request.config.prefs);
                    break;
                case Operation::Clock:
                    result = ui_runtime::setDateTime(request.clock);
                    break;
                case Operation::Sync:
                    result = ui_runtime::syncTime();
                    break;
                case Operation::Verify:
                    result = ui_runtime::verifyFirmware();
                    break;
                }
            } else
                result = {false, "Nie można zatrzymać RGB / zapis nie został wykonany"};
            if (paused) {
                flash.store(Flash::Written);
                if (!waitFlash(Flash::Resumed, 2000) && result.ok)
                    result = {true, "Zapisano / obraz wymaga ponownego uruchomienia"};
            }
            if (writesFlash)
                flash.store(Flash::Idle);
            Completion completion;
            completion.operation = request.operation;
            completion.ok = result.ok;
            std::snprintf(completion.message, sizeof(completion.message), "%s", result.message);
            // Back-pressure preserves every completion; the display task never waits on this queue.
            xQueueSend(completions, &completion, portMAX_DELAY);
        }
        ui_runtime::serviceIo();
        ui_runtime::serviceSolarIo();
        if (millis() - lastObservation >= 250) {
            lastObservation = millis();
            observe();
        }
    }
}
} // namespace
bool start() {
    if (requests)
        return workerTask != nullptr;
    requests = xQueueCreateStatic(kQueueDepth, sizeof(Request), requestStorage, &requestControl);
    completions =
        xQueueCreateStatic(kQueueDepth, sizeof(Completion), completionStorage, &completionControl);
    if (!requests || !completions)
        return false;
    workerTask = xTaskCreateStaticPinnedToCore(worker, "aquabox-io", sizeof(taskStack), nullptr, 2,
                                               taskStack, &taskControl, 0);
    return workerTask != nullptr;
}
bool displayCore() { return xPortGetCoreID() == 1; }
ui_runtime::Result submit(Operation operation, const ui_runtime::Configuration *config,
                          const ui_runtime::DateTime *clock) {
    if (!requests || ((operation == Operation::Save || operation == Operation::Network) && !config) ||
        (operation == Operation::Clock && !clock))
        return {false, "Usługa komunikacji niedostępna"};
    Request request{};
    request.operation = operation;
    if (config)
        request.config = *config;
    if (clock)
        request.clock = *clock;
    if (xQueueSend(requests, &request, 0) != pdTRUE)
        return {false, "Kolejka zajęta / spróbuj ponownie"};
    return {true, "Operacja w toku"};
}
bool take(Completion &completion) {
    return completions && xQueueReceive(completions, &completion, 0) == pdTRUE;
}
bool videoService() {
    const Flash state = flash.load();
    if (state == Flash::Requested) {
        const bool stopped = panel_hal_begin_flash_write();
        Flash expected = Flash::Requested;
        if (!flash.compare_exchange_strong(expected, stopped ? Flash::Paused : Flash::Failed) && stopped)
            videoReady = panel_hal_end_flash_write();
        return false;
    }
    if (state == Flash::Written) {
        videoReady = panel_hal_end_flash_write();
        videoRetryAt = millis();
        flash.store(Flash::Resumed);
        return false;
    }
    if (state == Flash::Idle && !videoReady && millis() - videoRetryAt >= 1000) {
        videoRetryAt = millis();
        videoReady = panel_hal_end_flash_write();
    }
    return videoReady && (state == Flash::Idle || state == Flash::Resumed);
}
ui_status::WirelessObservation radio() {
    portENTER_CRITICAL(&cacheMux);
    const auto copy = observation;
    portEXIT_CRITICAL(&cacheMux);
    return copy;
}
void networkDetails(char *text, size_t capacity) {
    if (!text || !capacity)
        return;
    char copy[192];
    portENTER_CRITICAL(&cacheMux);
    std::memcpy(copy, details, sizeof(copy));
    portEXIT_CRITICAL(&cacheMux);
    std::snprintf(text, capacity, "%s", copy);
}
void publishNetwork(const char *message) {
    std::snprintf(networkState, sizeof(networkState), "%s", message);
}
} // namespace ui_io
#endif
