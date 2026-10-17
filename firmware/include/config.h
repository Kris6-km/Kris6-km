#pragma once
// =========================================================
// Board: Waveshare ESP32-S3-Touch-LCD-4 (480x480 RGB, GT911)
// Hardware pin map
// =========================================================

// ---- RS485 (SP3485, auto direction) ----
#define RS485_UART_NUM      2
#define RS485_RX_PIN        43
#define RS485_TX_PIN        44
// SP3485 switches direction automatically; no DE/RE pin
#define RS485_DE_PIN        -1

// ---- CAN (TJA1051T via ESP32 TWAI) ----
#define CAN_TX_PIN          6
#define CAN_RX_PIN          0

// Default bitrates to try during auto-detect (bps)
#define CAN_DEFAULT_BITRATE 500000
static const uint32_t CAN_BITRATE_CANDIDATES[] = {
    500000, 250000, 125000, 1000000, 100000, 50000
};
#define CAN_BITRATE_COUNT (sizeof(CAN_BITRATE_CANDIDATES)/sizeof(uint32_t))

// ---- Touch GT911 + CH32V003 helper (shared I2C) ----
#define TOUCH_I2C_SDA       15
#define TOUCH_I2C_SCL       7

// ---- MicroSD (SD_MMC 1-bit, per Waveshare 10_LVGL_SD) ----
#define SD_MMC_CLK          2
#define SD_MMC_CMD          1
#define SD_MMC_D0           4

// ---- Display (ST7701 RGB) pins live in display.cpp ----

// =========================================================
// Modbus defaults
// =========================================================
#define MB_DEFAULT_BAUD     9600
#define MB_DEFAULT_CONFIG   SERIAL_8N1
#define MB_SCAN_FIRST_ID    1
#define MB_SCAN_LAST_ID     247
#define MB_SCAN_TIMEOUT_MS  120     // per-slave probe timeout
#define MB_READ_TIMEOUT_MS  500

// Auto-detect baudrate candidates
static const uint32_t MB_BAUD_CANDIDATES[] = {
    9600, 19200, 38400, 57600, 115200, 4800
};
#define MB_BAUD_CANDIDATES_COUNT (sizeof(MB_BAUD_CANDIDATES)/sizeof(uint32_t))

// =========================================================
// WiFi / Web
// =========================================================
#define AP_SSID_PREFIX      "ModbusScanner-"
#define AP_PASSWORD         "modbus1234"       // >= 8 chars
#define WEB_PORT            80

// =========================================================
// Logging
// =========================================================
#define LOG_FILE_DIR        "/logs"
