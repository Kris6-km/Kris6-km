#include "display.h"
#include "config.h"
#include <Arduino.h>
#include <lvgl.h>
#include <ESP_Panel_Library.h>

// LVGL display driver glue for Waveshare ESP32-S3-Touch-LCD-4 using ESP32_Display_Panel.
// Board config and pins are taken from the library's board profile selected via
// ESP_PANEL_USE_SUPPORTED_BOARD at build time (handled by lib). If not auto-detected
// at compile, user can switch to manual init.

static ESP_Panel *panel = nullptr;
static SemaphoreHandle_t lvgl_mux = nullptr;

#define LCD_H_RES 480
#define LCD_V_RES 480

// LVGL draw buffer (double-buffered, in PSRAM)
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    panel->getLcd()->drawBitmap(area->x1, area->y1,
                                area->x2 - area->x1 + 1,
                                area->y2 - area->y1 + 1,
                                (const uint8_t *)color_p);
    lv_disp_flush_ready(drv);
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    if (!panel || !panel->getTouch()) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    panel->getTouch()->readData();
    bool touched = panel->getTouch()->getTouchState();
    if (touched) {
        TouchPoint p = panel->getTouch()->getPoint();
        data->point.x = p.x;
        data->point.y = p.y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void display_lock()   { if (lvgl_mux) xSemaphoreTake(lvgl_mux, portMAX_DELAY); }
void display_unlock() { if (lvgl_mux) xSemaphoreGive(lvgl_mux); }

void display_init() {
    lvgl_mux = xSemaphoreCreateMutex();

    panel = new ESP_Panel();
    panel->init();
#if LVGL_PORT_AVOID_TEAR
    auto lcd_bus = panel->getLcd()->getBus();
    if (lcd_bus->getType() == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<ESP_PanelBus_RGB *>(lcd_bus)->configRgbFrameBufferNumber(3);
    }
#endif
    panel->begin();

    lv_init();

    size_t buf_px = LCD_H_RES * 80;
    buf1 = (lv_color_t *) heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buf2 = (lv_color_t *) heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_px);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 0;
    lv_disp_drv_register(&disp_drv);

    if (panel->getTouch()) {
        static lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = lvgl_touch_cb;
        lv_indev_drv_register(&indev_drv);
    }

    log_i("Display + LVGL ready (%dx%d)", LCD_H_RES, LCD_V_RES);
}

void display_tick() {
    display_lock();
    lv_timer_handler();
    display_unlock();
}

void display_backlight(uint8_t pct) {
    if (!panel) return;
    if (panel->getBacklight()) {
        panel->getBacklight()->setBrightness(pct);
    }
}
