#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "display.h"
#include "ui.h"
#include "modbus_scanner.h"
#include "storage.h"
#include "webui.h"

ScannerSettings g_settings;
extern std::vector<ModbusDevice> g_last_scan;
std::vector<ModbusDevice> g_last_scan;

void setup() {
    Serial.begin(115200);
    delay(300);
    log_i("=== Modbus RS485 Scanner for Waveshare ESP32-S3-Touch-LCD-4 ===");

    settings_begin();
    settings_load(g_settings);

    storage_begin();

    Scanner.begin();
    Scanner.applySerial(g_settings.baudrate, g_settings.parity, g_settings.stopBits);

    display_init();
    display_backlight(90);
    ui_init();

    // WiFi + Web (non-blocking long tasks moved to separate task)
    xTaskCreatePinnedToCore([](void*){
        web_begin();
        vTaskDelete(nullptr);
    }, "web_init", 6144, nullptr, 1, nullptr, 0);
}

void loop() {
    display_tick();
    ui_tick();
    delay(5);
}
