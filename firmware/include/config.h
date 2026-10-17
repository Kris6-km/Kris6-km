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

// ---- CAN (TJA1051T) - zarezerwowane, nie używane w tym projekcie ----
#define CAN_TX_PIN          0
#define CAN_RX_PIN          15 // uwaga: współdzielone, ignorujemy w fazie 1

// ---- Touch GT911 (I2C) ----
#define TOUCH_I2C_SDA       15
#define TOUCH_I2C_SCL       7
#define TOUCH_INT_PIN       4
#define TOUCH_RST_PIN       -1  // reset via IO expander/CH32V003
#define TOUCH_I2C_ADDR      0x5D

// ---- Backlight / System helper (CH32V003) ----
#define SYS_HELPER_I2C_ADDR 0x24

// ---- MicroSD (SPI) ----
#define SD_SPI_CS           10
#define SD_SPI_MOSI         11
#define SD_SPI_CLK          12
#define SD_SPI_MISO         13

// ---- Display (ST7701 RGB) handled by ESP32_Display_Panel BSP ----

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
