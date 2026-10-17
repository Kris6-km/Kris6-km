#pragma once
#include <Arduino.h>

/**
 * Settings persisted in NVS/Preferences.
 */
struct ScannerSettings {
    uint32_t baudrate = 9600;
    uint8_t  parity   = 0;        // 0=N, 1=E, 2=O
    uint8_t  stopBits = 1;        // 1 or 2
    uint8_t  scanFirst = 1;
    uint8_t  scanLast  = 247;
    bool     autoBaud  = false;
    bool     logToSd   = true;

    char     wifiSsid[33] = "";
    char     wifiPass[65] = "";
    bool     wifiEnabled  = true;
};

void settings_begin();
void settings_load(ScannerSettings &s);
void settings_save(const ScannerSettings &s);

uint32_t settings_serial_config();   // returns SERIAL_8N1 etc from current settings
void     settings_apply_serial_config(uint32_t baud, uint8_t parity, uint8_t stop);
