#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

// Large UI object trees belong in PSRAM; the small draw buffer stays in SRAM.
#define LV_MEM_CUSTOM 1
#ifdef AQUARIUM_UI_HOST_PREVIEW
#define LV_MEM_CUSTOM_INCLUDE <ui_host_heap.h>
#define LV_MEM_CUSTOM_ALLOC(size) ui_host_malloc(size)
#define LV_MEM_CUSTOM_FREE(ptr) ui_host_free(ptr)
#define LV_MEM_CUSTOM_REALLOC(ptr, size) ui_host_realloc(ptr, size)
#else
#define LV_MEM_CUSTOM_INCLUDE <esp_heap_caps.h>
#define LV_MEM_CUSTOM_ALLOC(size) heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE(ptr) heap_caps_free((ptr))
#define LV_MEM_CUSTOM_REALLOC(ptr, size) heap_caps_realloc((ptr), (size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#endif

#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 20
#define LV_TICK_CUSTOM 0
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_IMG_CACHE_DEF_SIZE 0

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 0
#define LV_USE_LIST 0
#define LV_USE_KEYBOARD 1
#define LV_USE_SPINNER 0
#define LV_USE_CHART 1
#define LV_USE_METER 0
#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_LED 0
#define LV_USE_MENU 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0

#endif
