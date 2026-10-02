#ifndef AQUABOX_HISTORY_VIEW_H
#define AQUABOX_HISTORY_VIEW_H
#include "ui_theme.h"
#include <lvgl.h>
namespace ui_history_view {
struct Colors {
    uint32_t background, surface, text, muted, grid, good, warning, bad;
    uint32_t normalBand, warningCard, badBand, badCard, selected;
    ui_theme::Palette theme;
};
enum class Command : uint8_t { Sensor, ClearHistory, ClearLog };
using Handler = void (*)(Command, unsigned);
void create(lv_obj_t *parent, unsigned sensor, const Colors &colors, Handler handler);
void refresh();
void reset();
void resetSession();
void captureScroll();
void latest();
#ifdef AQUARIUM_UI_HOST_PREVIEW
bool previewInspection(bool expanded, unsigned layers, unsigned seconds);
bool previewFocus(unsigned kind);
unsigned previewLayers();
uint32_t previewSelection();
#endif
} // namespace ui_history_view
#endif
