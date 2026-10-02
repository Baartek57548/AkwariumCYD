#include "ui_theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
double luminance(uint32_t color) {
    double channels[3]{};
    for (int index = 0; index < 3; ++index) {
        const double channel = ((color >> (16 - index * 8)) & 255u) / 255.0;
        channels[index] = channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    }
    return channels[0] * 0.2126 + channels[1] * 0.7152 + channels[2] * 0.0722;
}
double contrast(uint32_t a, uint32_t b) {
    const double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}
} // namespace
bool ui_theme_checks() {
    unsigned checks = 0, failures = 0;
    auto require = [&](bool condition, const char *name) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "FAIL THEME: %s\n", name);
        }
    };
    for (bool night : {false, true}) {
        const auto &p = ui_theme::get(night);
        for (uint32_t fill : {p.background, p.surface, p.card, p.selected, p.chrome, p.chromeActive}) {
            require(contrast(p.text, fill) >= 4.5, "normal text contrast");
        }
        require(night ? luminance(p.card) < 0.12 : luminance(p.card) > 0.8,
                "cards stay in their intended brightness family");
        require(contrast(p.background, p.card) >= (night ? 1.5 : 1.3),
                "cards separate from the page");
        require(contrast(p.background, p.surface) >= 1.2, "secondary surfaces separate from the page");
        require(contrast(p.card, p.selected) >= (night ? 1.3 : 1.5),
                "selected controls separate from cards");
        require(contrast(p.border, p.background) >= 2.0, "dividers remain visible");
        require(contrast(p.muted, p.background) >= 4.5, "secondary text contrast");
        require(contrast(p.muted, p.card) >= 4.5 && contrast(p.muted, p.surface) >= 4.5,
                "secondary text remains legible on cards");
        require(contrast(p.chromeText, p.chrome) >= 4.5, "header/footer contrast");
        require(contrast(p.chromeMuted, p.chrome) >= 4.5, "secondary header/footer contrast");
        require(contrast(p.good, p.goodTint) >= 4.5, "success text contrast");
        require(contrast(p.bad, p.badTint) >= 4.5, "alarm text contrast");
        require(contrast(p.warning, p.warningTint) >= 4.5, "warning text contrast");
        require(contrast(p.accent, p.background) >= 4.5, "blue action contrast");
    }
    ui_theme::Settings colors{};
    for (uint8_t preset = 0; preset < ui_theme::kPresetCount; ++preset) {
        colors.preset[0] = colors.preset[1] = preset;
        require(ui_theme::valid(colors), "named preset is valid for both modes");
        for (bool night : {false, true}) {
            const auto palette = ui_theme::resolve(colors, night);
            for (uint32_t fill : {palette.background, palette.surface, palette.card, palette.selected}) {
                require(contrast(palette.text, fill) >= 4.5, "preset content text remains readable");
                require(contrast(ui_theme::readableForeground(palette.muted, fill), fill) >= 4.5,
                        "preset secondary text remains readable");
            }
            require(night ? luminance(palette.card) < 0.12 : luminance(palette.card) > 0.8,
                    "all named variants respect the selected light/dark mode");
            require(contrast(palette.background, palette.card) >= (night ? 1.8 : 1.6),
                    "preset cards remain distinct from the page");
            require(contrast(palette.background, palette.surface) >= 1.35,
                    "preset secondary surface remains distinct from the page");
            require(contrast(palette.card, palette.selected) >= 1.5,
                    "preset selected controls remain distinct from cards");
            require(contrast(palette.navigationText, palette.navigation) >= 4.5,
                    "navigation text follows its independent surface");
            require(contrast(palette.chromeText, palette.chrome) >= 4.5,
                    "header text follows its independent surface");
            require(contrast(palette.accentText, palette.accent) >= 4.5,
                    "brand text remains readable over each accent");
            require(contrast(palette.accent, palette.background) >= 4.5,
                    "preset action text remains readable");
            require(palette.good == ui_theme::get(night).good &&
                        palette.bad == ui_theme::get(night).bad &&
                        palette.warning == ui_theme::get(night).warning,
                    "status colors retain their meaning in every preset");
        }
    }
    colors = ui_theme::Settings{};
    for (bool night : {false, true}) {
        const auto palette = ui_theme::resolve(colors, night);
        require(palette.background == ui_theme::roleColor(colors, night, ui_theme::Role::Background) &&
                    palette.card == ui_theme::roleColor(colors, night, ui_theme::Role::Card) &&
                    palette.chrome == ui_theme::roleColor(colors, night, ui_theme::Role::Header) &&
                    palette.navigation == ui_theme::roleColor(colors, night, ui_theme::Role::Navigation),
                "AquaBox uses its own day and night palette");
        require(palette.background != ui_theme::get(night).background,
                "AquaBox has the stronger contrast palette");
    }
    colors.preset[0] = 2;
    colors.preset[1] = 1;
    require(ui_theme::resolve(colors, false).background == 0x9FC6D4 &&
                ui_theme::resolve(colors, true).background == 0x030D07,
            "light Ocean and dark Matrix resolve independently");
    colors.preset[1] = 5;
    require(ui_theme::resolve(colors, false).background == 0x9FC6D4,
            "changing dark preset leaves the light palette unchanged");
    for (unsigned mode = 0; mode < 2; ++mode) {
        colors = ui_theme::Settings{};
        colors.preset[mode] = ui_theme::kPresetCount;
        require(!ui_theme::valid(colors), "unknown preset is rejected in each mode");
        require(ui_theme::resolve(colors, mode != 0).background == ui_theme::get(mode != 0).background,
                "invalid palette IDs resolve safely to the original panel");
    }
    std::printf("THEME %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return !failures;
}
