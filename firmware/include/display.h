#pragma once
#include <Arduino.h>

void display_init();
void display_tick();     // call in loop()
void display_backlight(uint8_t pct); // 0..100

// Convenience accessors for UI code
void display_lock();
void display_unlock();
