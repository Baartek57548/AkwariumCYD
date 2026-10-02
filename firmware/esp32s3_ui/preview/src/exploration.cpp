#include "ui_fonts.h"
#include <lvgl.h>

namespace {
lv_obj_t *block(lv_obj_t *p, int x, int y, int w, int h, uint32_t c, int r = 0) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
void txt(lv_obj_t *p, int x, int y, const char *s, const lv_font_t *f, uint32_t c) {
    lv_obj_t *o = lv_label_create(p);
    lv_obj_set_pos(o, x, y);
    lv_label_set_text(o, s);
    lv_obj_set_style_text_font(o, f, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(c), 0);
}
} // namespace
void ui_exploration_init(int variant) {
    lv_obj_t *s = lv_scr_act();
    lv_obj_clean(s);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    if (variant == 1) {
        block(s, 0, 0, 800, 480, 0xF1F2E9);
        txt(s, 24, 16, "AQUA / PRZEKRÓJ", &ui_font_20, 0x173F3E);
        txt(s, 570, 19, "DEMO  •  14:32", &ui_font_16, 0x417472);
        block(s, 24, 64, 752, 1, 0xCDD8CF);
        txt(s, 24, 82, "Akwarium      Urządzenia      Plan dnia      Historia      System", &ui_font_20,
            0x173F3E);
        block(s, 24, 112, 112, 4, 0x157E79);
        txt(s, 24, 145, "01 / WODA", &ui_font_14, 0x417472);
        txt(s, 24, 179, "24,8°", &ui_font_44, 0x173F3E);
        txt(s, 24, 245, "Cel 25,0°C  /  stan stabilny", &ui_font_16, 0x417472);
        block(s, 24, 287, 418, 4, 0x157E79);
        for (int i = 0; i < 25; ++i)
            block(s, 24 + i * 17, 277, i % 6 == 0 ? 2 : 1, i % 6 == 0 ? 10 : 5, 0x417472);
        txt(s, 24, 309, "pH   6,92", &ui_font_24, 0x173F3E);
        txt(s, 246, 309, "EC   460", &ui_font_24, 0x173F3E);
        txt(s, 24, 348, "Pomiar próbny / µS/cm przy 25°C", &ui_font_14, 0x417472);
        block(s, 490, 166, 282, 199, 0xDDE8DF, 8);
        block(s, 508, 205, 244, 142, 0xB7D8CC, 4);
        block(s, 508, 203, 244, 3, 0x157E79);
        block(s, 508, 152, 116, 6, 0x173F3E);
        block(s, 636, 152, 116, 6, 0x8AB2A6);
        txt(s, 528, 250, "MIN: wolny   MAX: limit", &ui_font_16, 0x173F3E);
        txt(s, 509, 366, "Przód: dzień  /  Tył: świt", &ui_font_16, 0x417472);
        block(s, 24, 399, 452, 54, 0x173F3E, 4);
        txt(s, 41, 416, "Karmienie              Serwis", &ui_font_20, 0xF1F2E9);
        txt(s, 515, 415, "18:00 / następna dawka", &ui_font_16, 0x173F3E);
    } else if (variant == 2) {
        block(s, 0, 0, 800, 480, 0x171F38);
        txt(s, 28, 24, "AQUA / RYTM", &ui_font_24, 0xECEBE4);
        txt(s, 612, 28, "DEMO", &ui_font_16, 0xD1AD69);
        lv_obj_t *arc = lv_arc_create(s);
        lv_obj_set_pos(arc, 28, 89);
        lv_obj_set_size(arc, 286, 286);
        lv_arc_set_bg_angles(arc, 0, 360);
        lv_arc_set_angles(arc, 200, 300);
        lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0xD1AD69), LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0x303956), LV_PART_MAIN);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        txt(s, 105, 179, "14:32", &ui_font_44, 0xECEBE4);
        txt(s, 107, 239, "FAZA DNIA", &ui_font_14, 0xD1AD69);
        txt(s, 353, 109, "24,8°C", &ui_font_44, 0xECEBE4);
        txt(s, 355, 166, "Woda  /  cel 25,0°C", &ui_font_16, 0xAAAEC2);
        txt(s, 353, 216, "6,92 pH     460 µS/cm", &ui_font_24, 0xECEBE4);
        block(s, 352, 282, 407, 2, 0xD1AD69);
        txt(s, 355, 303, "18:00   Karmienie", &ui_font_24, 0xD1AD69);
        txt(s, 355, 344, "21:00   Przejście w noc", &ui_font_20, 0xAAAEC2);
        block(s, 28, 410, 744, 56, 0x303956, 28);
        txt(s, 52, 428, "Rytm       Woda       Światło       Obsługa       Panel", &ui_font_20, 0xECEBE4);
    } else {
        block(s, 0, 0, 800, 480, 0xE7EDF2);
        block(s, 0, 0, 152, 480, 0x243748);
        txt(s, 20, 24, "AQUA", &ui_font_24, 0xFFFFFF);
        txt(s, 20, 59, "/ OBIEG", &ui_font_16, 0x8FD5D8);
        const char *nav[] = {"Obieg", "Pomiary", "Automatyka", "Zdarzenia", "Konfiguracja"};
        for (int i = 0; i < 5; ++i)
            txt(s, 18, 138 + i * 58, nav[i], &ui_font_16, i == 0 ? 0x8FD5D8 : 0xBECBD7);
        txt(s, 182, 24, "Obieg wody", &ui_font_24, 0x243748);
        txt(s, 657, 30, "DEMO", &ui_font_16, 0x527A96);
        block(s, 186, 101, 214, 200, 0xC8D9E4, 4);
        txt(s, 212, 133, "AKWARIUM", &ui_font_20, 0x243748);
        txt(s, 216, 185, "24,8°C", &ui_font_44, 0x243748);
        txt(s, 214, 250, "MIN / MAX poprawne", &ui_font_14, 0x527A96);
        block(s, 400, 197, 104, 4, 0x298E96);
        block(s, 500, 137, 230, 110, 0xFFFFFF, 4);
        txt(s, 526, 158, "FILTR  →  POWRÓT", &ui_font_20, 0x243748);
        txt(s, 526, 200, "Wykryto impulsy", &ui_font_16, 0x298E96);
        block(s, 294, 301, 4, 50, 0x298E96);
        block(s, 298, 348, 205, 4, 0x298E96);
        txt(s, 188, 380, "pH 6,92    EC 460 µS/cm", &ui_font_24, 0x243748);
        block(s, 500, 291, 230, 73, 0x243748, 4);
        txt(s, 528, 315, "Karmienie / pauza", &ui_font_20, 0xFFFFFF);
        txt(s, 190, 438, "Stan → przyczyna → działanie", &ui_font_16, 0x527A96);
    }
}
