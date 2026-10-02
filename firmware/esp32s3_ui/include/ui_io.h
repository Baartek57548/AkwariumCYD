#ifndef AQUABOX_UI_IO_H
#define AQUABOX_UI_IO_H
#include "ui_runtime.h"
#include "ui_status.h"
namespace ui_io {
enum class Operation : uint8_t { Save, Reset, Network, Clock, Sync, Verify };
struct Completion {
    Operation operation;
    bool ok;
    char message[160];
};
bool start();
bool displayCore();
ui_runtime::Result submit(Operation operation, const ui_runtime::Configuration *config = nullptr,
                          const ui_runtime::DateTime *clock = nullptr);
bool take(Completion &completion);
bool videoService();
ui_status::WirelessObservation radio();
void networkDetails(char *text, size_t capacity);
void publishNetwork(const char *message);
#ifdef AQUARIUM_UI_HOST_PREVIEW
void resetPreview();
void delayNextCompletion(uint32_t milliseconds);
#endif
} // namespace ui_io
#endif
