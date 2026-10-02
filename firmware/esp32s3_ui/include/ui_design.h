#ifndef AQUABOX_UI_DESIGN_H
#define AQUABOX_UI_DESIGN_H
#include "ui_fonts.h"

namespace ui_design {
// The 800x480 composition uses a 4 px rhythm. Keep the existing information
// density; these tokens describe recurring decisions rather than scaling it.
namespace space {
constexpr int xs = 4;
constexpr int sm = 8;
constexpr int md = 12;
constexpr int lg = 16;
constexpr int xl = 24;
constexpr int xxl = 32;
}
namespace radius {
constexpr int control = 6;
constexpr int card = 10;
constexpr int dialog = 12;
}
namespace typography {
inline const lv_font_t *caption() { return &ui_font_14; }
inline const lv_font_t *body() { return &ui_font_16; }
inline const lv_font_t *section() { return &ui_font_20; }
inline const lv_font_t *title() { return &ui_font_24; }
inline const lv_font_t *measurement() { return &ui_font_44; }
}
namespace motion {
constexpr int fast = 160;
constexpr int normal = 220;
constexpr int slow = 280;
}
constexpr int minimumTouchHeight = 44;
constexpr int pressedZoom = 250; // LVGL's 256 is 100%; feedback is about 97.7%.
constexpr int bodyInset = 24;
} // namespace ui_design

#endif
