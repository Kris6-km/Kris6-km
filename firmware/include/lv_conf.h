#pragma once
// Minimal LVGL v8 config for ESP32-S3 480x480 RGB panel.
// Full options list omitted; defaults are fine for this project.

#define LV_CONF_SKIP 0
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

// Use PSRAM for LVGL buffers via custom malloc hooks (set in display.cpp)
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "esp_heap_caps.h"
#define LV_MEM_CUSTOM_ALLOC(size)      heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE(p)          heap_caps_free(p)
#define LV_MEM_CUSTOM_REALLOC(p, size) heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "esp_timer.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR ((esp_timer_get_time() / 1000LL))

// Enable commonly used widgets
#define LV_USE_LABEL   1
#define LV_USE_BTN     1
#define LV_USE_BTNMATRIX 1
#define LV_USE_LIST    1
#define LV_USE_TABLE   1
#define LV_USE_TABVIEW 1
#define LV_USE_DROPDOWN 1
#define LV_USE_SLIDER  1
#define LV_USE_SWITCH  1
#define LV_USE_BAR     1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1
#define LV_USE_MSGBOX   1
#define LV_USE_SPINNER  1
#define LV_USE_CHECKBOX 1
#define LV_USE_ROLLER   1

// Themes
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

#define LV_USE_FS_STDIO 0
#define LV_USE_LOG      0
