#ifndef AQUABOX_UI_THEME_H
#define AQUABOX_UI_THEME_H
#include <cstdint>

namespace ui_theme {
enum class Role : uint8_t { Background, Card, Header, Navigation, Accent, Count };
constexpr uint8_t kRoleCount = static_cast<uint8_t>(Role::Count);
constexpr uint8_t kPresetCount = 6;
constexpr uint32_t kTimeMarkerLight = 0xD80056;
constexpr uint32_t kTimeMarkerDark = 0xFF5A92;
inline uint32_t timeMarker(bool night) { return night ? kTimeMarkerDark : kTimeMarkerLight; }
struct Settings {
    uint8_t preset[2]{0, 0};
};
// Independent surface palettes avoid inverse-text buttons leaking dark blocks into the light UI.
// Semantic status colors are separate from the blue interaction/navigation hierarchy.
struct Palette {
    uint32_t background, text, muted, border, surface, card;
    uint32_t chrome, chromeText, chromeMuted, chromeActive, chromeBorder, selected;
    uint32_t accent, good, bad, warning, goodTint, badTint, warningTint;
    uint32_t waterBase, waterFill, waterBorder, veil, planLine, planBadge;
    uint32_t navigation, navigationText, navigationMuted, navigationActive, navigationBorder, accentText;
    bool cardOutline;
};
constexpr Palette light{0xCBDCEC, 0x142A45, 0x304E70, 0x7999BC, 0xEAF2FB, 0xF6FAFF, 0xACC6E1,
                        0x142A45, 0x274766, 0x8EB7E1, 0x6689B2, 0x9DC6EF, 0x145596, 0x0C6745,
                        0xA12635, 0x765010, 0xD1EBD8, 0xF5D5DC, 0xF7E8C3, 0xCEE5F7, 0x4E9ACF,
                        0x4F85B1, 0xCBDCEC, 0x1F5BA0, 0xAFCBEA, 0xACC6E1, 0x142A45, 0x274766,
                        0x8EB7E1, 0x6689B2, 0xF0F6FF, false};
constexpr Palette dark{0x0B1728, 0xF0F6FF, 0xBCD0E8, 0x476A91, 0x172E49, 0x203C5C, 0x07111F,
                       0xF0F6FF, 0xAEC6E1, 0x315C88, 0x3B5A79, 0x2C527A, 0xA8D0FF, 0x83E2B3,
                       0xFFAFB4, 0xFFD08A, 0x164938, 0x502A36, 0x4D3924, 0x183C60, 0x4F9DDA,
                       0x82B7E2, 0x050C16, 0xA8D0FF, 0x31577F, 0x07111F, 0xF0F6FF, 0xAEC6E1,
                       0x315C88, 0x3B5A79, 0x142A45, false};
inline const Palette &get(bool night) { return night ? dark : light; }
const char *presetName(uint8_t preset);
const char *presetDescription(uint8_t preset);
uint32_t roleColor(const Settings &settings, bool night, Role role);
uint32_t readableText(uint32_t background);
uint32_t readableForeground(uint32_t foreground, uint32_t background);
Palette resolve(const Settings &settings, bool night);
bool valid(const Settings &settings);
} // namespace ui_theme
#endif
