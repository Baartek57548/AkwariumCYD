#include "ui_theme.h"

namespace ui_theme {
namespace {
// Every curated preset has a light and dark variant. Status colors retain
// their meaning independently of the selected decorative palette.
constexpr uint32_t presetColors[kPresetCount][2][kRoleCount] = {
    {{0xAFC3D8, 0xFFFFFF, 0x718BA8, 0x4F7092, 0x064A91},
     {0x050C17, 0x274C72, 0x020711, 0x020711, 0x83C0FF}},
    {{0xA4C2A4, 0xF7FFF3, 0x668D68, 0x527957, 0x145522},
     {0x030D07, 0x1B482B, 0x010603, 0x010603, 0x8EFF91}},
    {{0x9FC6D4, 0xF2FCFF, 0x5D8FA2, 0x48788B, 0x075473},
     {0x04111B, 0x1C4962, 0x020912, 0x020912, 0x79DFFF}},
    {{0xD9B5B2, 0xFFF8F4, 0xA56D6B, 0x8A5657, 0x81313A},
     {0x190B0E, 0x60323D, 0x0A0406, 0x0A0406, 0xFFAAA5}},
    {{0xB9C1CB, 0xF8FAFD, 0x75818F, 0x5C6978, 0x293A50},
     {0x090D13, 0x3B4655, 0x030509, 0x030509, 0xD0DFFF}},
    {{0xD3BC94, 0xFFF8E8, 0xA1814D, 0x846638, 0x5E3C05},
     {0x120D04, 0x5D421D, 0x070501, 0x070501, 0xFFD17A}}};

uint32_t mix(uint32_t first, uint32_t second, unsigned secondPercent) {
    const unsigned firstPercent = 100u - secondPercent;
    const uint32_t red = (((first >> 16) & 255u) * firstPercent +
                          ((second >> 16) & 255u) * secondPercent + 50u) / 100u;
    const uint32_t green = (((first >> 8) & 255u) * firstPercent +
                            ((second >> 8) & 255u) * secondPercent + 50u) / 100u;
    const uint32_t blue = ((first & 255u) * firstPercent + (second & 255u) * secondPercent + 50u) / 100u;
    return (red << 16) | (green << 8) | blue;
}

uint32_t luminance(uint32_t color) {
    // Gamma-corrected lookup avoids pow() for every label refresh on the ESP32-S3.
    static const uint16_t linear[256] = {
        0,     3,     6,     9,    12,    15,    18,    21,    24,    27,    30,    33,    37,    40,    44,    48,
       52,    56,    60,    65,    70,    75,    80,    86,    91,    97,   103,   110,   116,   123,   130,   137,
      144,   152,   160,   168,   176,   185,   194,   203,   212,   222,   232,   242,   252,   262,   273,   284,
      296,   307,   319,   331,   343,   356,   369,   382,   395,   409,   423,   437,   452,   467,   482,   497,
      513,   529,   545,   561,   578,   595,   612,   630,   648,   666,   685,   704,   723,   742,   762,   782,
      802,   823,   844,   865,   887,   908,   931,   953,   976,   999,  1022,  1046,  1070,  1095,  1119,  1144,
     1170,  1195,  1221,  1248,  1274,  1301,  1329,  1356,  1384,  1413,  1441,  1470,  1500,  1529,  1559,  1590,
     1620,  1651,  1683,  1714,  1746,  1779,  1812,  1845,  1878,  1912,  1946,  1981,  2016,  2051,  2086,  2122,
     2159,  2195,  2232,  2270,  2307,  2346,  2384,  2423,  2462,  2502,  2542,  2582,  2623,  2664,  2705,  2747,
     2789,  2831,  2874,  2918,  2961,  3005,  3050,  3095,  3140,  3185,  3231,  3278,  3325,  3372,  3419,  3467,
     3515,  3564,  3613,  3663,  3712,  3763,  3813,  3864,  3916,  3968,  4020,  4072,  4125,  4179,  4233,  4287,
     4342,  4397,  4452,  4508,  4564,  4621,  4678,  4735,  4793,  4851,  4910,  4969,  5029,  5089,  5149,  5210,
     5271,  5333,  5395,  5457,  5520,  5583,  5647,  5711,  5776,  5841,  5906,  5972,  6038,  6105,  6172,  6240,
     6308,  6376,  6445,  6514,  6584,  6654,  6724,  6795,  6867,  6939,  7011,  7084,  7157,  7231,  7305,  7379,
     7454,  7529,  7605,  7682,  7758,  7835,  7913,  7991,  8070,  8148,  8228,  8308,  8388,  8469,  8550,  8632,
     8714,  8796,  8879,  8963,  9047,  9131,  9216,  9301,  9387,  9473,  9560,  9647,  9734,  9823,  9911, 10000
    };
    const uint32_t red = linear[(color >> 16) & 255u];
    const uint32_t green = linear[(color >> 8) & 255u];
    const uint32_t blue = linear[color & 255u];
    return (red * 2126u + green * 7152u + blue * 722u + 5000u) / 10000u;
}

uint32_t contrast(uint32_t first, uint32_t second) {
    const uint32_t a = luminance(first), b = luminance(second);
    const uint32_t high = a > b ? a : b, low = a > b ? b : a;
    return ((high + 500u) * 1000u) / (low + 500u);
}

uint32_t readableMuted(uint32_t background, uint32_t text) {
    const uint32_t quieter = mix(text, background, 12);
    return contrast(quieter, background) >= 4500u ? quieter : text;
}
} // namespace

const char *presetName(uint8_t preset) {
    static const char *names[] = {"AquaBox", "Matrix", "Ocean", "Koral", "Grafit", "Bursztyn"};
    return preset < kPresetCount ? names[preset] : names[0];
}

const char *presetDescription(uint8_t preset) {
    static const char *descriptions[] = {"Klasyczny błękit", "Zielona energia", "Morski turkus",
                                         "Ciepły koral", "Spokojna szarość", "Złote odcienie"};
    return descriptions[preset < kPresetCount ? preset : 0];
}

uint32_t roleColor(const Settings &settings, bool night, Role role) {
    const unsigned index = static_cast<unsigned>(role);
    if (index >= kRoleCount)
        return 0;
    const unsigned mode = night ? 1u : 0u;
    const unsigned preset = settings.preset[mode] < kPresetCount ? settings.preset[mode] : 0u;
    return presetColors[preset][mode][index];
}

uint32_t readableText(uint32_t background) {
    const uint32_t preferred = contrast(0x142A45, background) >= contrast(0xF0F6FF, background)
                                   ? 0x142A45 : 0xF0F6FF;
    if (contrast(preferred, background) >= 4600u)
        return preferred;
    // Near middle luminance neither brand color necessarily reaches 4.5:1.
    // A near-black nonzero value also remains distinct from the UI's default-color sentinel.
    return contrast(0x010101, background) >= contrast(0xFFFFFF, background)
               ? 0x010101 : 0xFFFFFF;
}

uint32_t readableForeground(uint32_t foreground, uint32_t background) {
    return contrast(foreground, background) >= 4600u ? foreground : readableText(background);
}

Palette resolve(const Settings &settings, bool night) {
    Palette palette = get(night);
    const unsigned mode = night ? 1u : 0u;
    if (settings.preset[mode] >= kPresetCount)
        return palette;
    palette.background = roleColor(settings, night, Role::Background);
    palette.card = roleColor(settings, night, Role::Card);
    palette.chrome = roleColor(settings, night, Role::Header);
    palette.navigation = roleColor(settings, night, Role::Navigation);
    palette.accent = roleColor(settings, night, Role::Accent);
    palette.accentText = readableText(palette.accent);
    palette.text = readableText(palette.background);
    palette.muted = readableMuted(palette.background, palette.text);
    palette.surface = mix(palette.background, palette.card, 60);
    palette.cardOutline = contrast(palette.background, palette.card) < 1200u;
    palette.border = mix(palette.background, palette.accent, night ? 42 : 31);
    if (contrast(palette.border, palette.background) < 1500u)
        palette.border = mix(palette.background, palette.text, 38);
    palette.selected = mix(palette.card, palette.accent, night ? 26 : 32);
    if (contrast(palette.selected, palette.card) < 1150u)
        palette.selected = mix(palette.card, readableText(palette.card), 20);
    palette.chromeText = readableText(palette.chrome);
    palette.chromeMuted = readableMuted(palette.chrome, palette.chromeText);
    palette.chromeActive = mix(palette.chrome, palette.accent, 18);
    palette.chromeBorder = mix(palette.chrome, palette.accent, 34);
    if (contrast(palette.chromeActive, palette.chrome) < 1150u)
        palette.chromeActive = mix(palette.chrome, palette.chromeText, 20);
    palette.navigationText = readableText(palette.navigation);
    palette.navigationMuted = readableMuted(palette.navigation, palette.navigationText);
    palette.navigationActive = mix(palette.navigation, palette.accent, 18);
    palette.navigationBorder = mix(palette.navigation, palette.accent, 34);
    if (contrast(palette.navigationActive, palette.navigation) < 1150u)
        palette.navigationActive = mix(palette.navigation, palette.navigationText, 20);
    palette.planLine = palette.accent;
    palette.planBadge = mix(palette.card, palette.accent, 16);
    palette.waterBase = mix(palette.card, palette.accent, night ? 10 : 8);
    palette.waterFill = mix(palette.card, palette.accent, night ? 65 : 60);
    palette.waterBorder = readableForeground(palette.accent, palette.waterBase);
    return palette;
}

bool valid(const Settings &settings) {
    return settings.preset[0] < kPresetCount && settings.preset[1] < kPresetCount;
}
} // namespace ui_theme
