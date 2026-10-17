#pragma once
#include <Arduino.h>

void ui_init();
void ui_tick();

// Notify UI from scanner thread
void ui_set_scan_progress(uint8_t id, uint8_t first, uint8_t last, int found);
void ui_set_scan_result(int count);
void ui_set_status(const String &text);
void ui_set_wifi_info(const String &info);
