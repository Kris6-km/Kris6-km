#include "display.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvGT911.hpp"
#include "WS_CH32_IO.h"

// Display init mirrors Waveshare's official 09_LVGL_Widgets example.
static Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED, 42 /* CS */, 2 /* SCK */, 1 /* MOSI */, GFX_NOT_DEFINED);

static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    40, 39, 38, 41,                 // DE, VSYNC, HSYNC, PCLK
    46, 3, 8, 18, 17,               // R0..R4
    14, 13, 12, 11, 10, 9,          // G0..G5
    5, 45, 48, 47, 21,              // B0..B4
    1, 10, 8, 50,                   // hsync pol, fp, pw, bp
    1, 10, 8, 20);                  // vsync pol, fp, pw, bp

static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480, 480, rgbpanel, 2, true, bus, GFX_NOT_DEFINED,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

static TouchDrvGT911 GT911;
static bool touchOk = false;
static SemaphoreHandle_t lvgl_mux = nullptr;
static lv_disp_draw_buf_t draw_buf;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
    gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t *)&px->full, a->x2 - a->x1 + 1, a->y2 - a->y1 + 1);
    lv_disp_flush_ready(drv);
}

static void touch_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
    int16_t x[1], y[1];
    if (touchOk && GT911.getPoint(x, y, 1) > 0) {
        // rotation 2 => mirror both axes
        data->point.x = gfx->width() - x[0];
        data->point.y = gfx->height() - y[0];
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

static bool init_touch() {
    for (uint8_t addr : {GT911_SLAVE_ADDRESS_L, GT911_SLAVE_ADDRESS_H}) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() != 0) continue;
        GT911.setPins(-1, -1);
        if (GT911.begin(Wire, addr, TOUCH_I2C_SDA, TOUCH_I2C_SCL)) {
            GT911.setMaxTouchPoint(1);
            return true;
        }
    }
    return false;
}

void display_lock()   { if (lvgl_mux) xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY); }
void display_unlock() { if (lvgl_mux) xSemaphoreGiveRecursive(lvgl_mux); }

void display_init() {
    lvgl_mux = xSemaphoreCreateRecursiveMutex();

    // CH32V003 powers LCD + releases LCD/touch reset
    if (!WS_CH32_IO::begin(Wire, TOUCH_I2C_SDA, TOUCH_I2C_SCL, WS_CH32_IO::DEFAULT_I2C_FREQ, &Serial))
        log_w("CH32V003 IO expander init failed");
    touchOk = init_touch();
    log_i("GT911 touch: %s", touchOk ? "OK" : "not found");

    gfx->begin();
    lv_init();

    size_t px = 480 * 480 / 4;
    auto *b1 = (lv_color_t *)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    auto *b2 = (lv_color_t *)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&draw_buf, b1, b2, px);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 480;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    if (touchOk) {
        static lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = touch_cb;
        lv_indev_drv_register(&indev_drv);
    }
}

void display_tick() {
    display_lock();
    lv_timer_handler();
    display_unlock();
}

void display_backlight(uint8_t pct) {
    WS_CH32_IO::setPwm(Wire, (uint8_t)map(constrain(pct, 0, 100), 0, 100, 0, 255));
}
