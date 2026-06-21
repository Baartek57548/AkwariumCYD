/**
 * @file lv_conf.h
 * Configuration file for LVGL v8.3.x on ESP32 CYD
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Enable configuration */
#define LV_CONF_SKIP 0

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1 /* Set to 1 if display colors are inverted/swapped, LovyanGFX handles it if 0 */
#define LV_COLOR_SCREEN_TRANSP 0

/*=========================
   MEMORY SETTINGS
 *=========================*/
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM == 0
    #define LV_MEM_SIZE (120U * 1024U) /* The full dashboard creates many LVGL objects; 80KB was completely exhausted. */
    #define LV_MEM_ADR 0
#else
    #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
    #define LV_MEM_CUSTOM_ALLOC   malloc
    #define LV_MEM_CUSTOM_FREE    free
    #define LV_MEM_CUSTOM_REALLOC realloc
#endif

#define LV_MEM_POOL_RECLAIM_ON_FAIL 1
#define LV_MEM_LIBC_MALLOC 0

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DISP_DEF_REFR_PERIOD 30      /* Refresh period in milliseconds */
#define LV_INDEV_DEF_READ_PERIOD 30     /* Input device read period in milliseconds */
#define LV_TICK_CUSTOM 0

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_IMG_CACHE_DEF_SIZE 4         /* Cache for loaded images from SD card */

/*========================
 *  THEME DEMO & STYLING
 *========================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1         /* Enable modern dark mode for "cyd-web" style */
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

/*======================
 *  FONT USAGE
 *======================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1

#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*======================
 *  WIDGETS
 *======================*/
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       0
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      0
#define LV_USE_LIST       1
#define LV_USE_KEYBOARD   1
#define LV_USE_SPINNER    1

/* Extra layouts/widgets */
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_MSGBOX     0
#define LV_USE_CHART      1
#define LV_USE_METER      0

#endif /*LV_CONF_H*/
