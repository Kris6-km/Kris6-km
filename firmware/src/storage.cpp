#include "storage.h"
#include "config.h"
#include <SPI.h>
#include <SD.h>
#include <time.h>

static bool _sdOk = false;

bool storage_begin() {
    SPI.begin(SD_SPI_CLK, SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_CS);
    _sdOk = SD.begin(SD_SPI_CS, SPI, 20000000);
    if (_sdOk) {
        if (!SD.exists(LOG_FILE_DIR)) SD.mkdir(LOG_FILE_DIR);
        log_i("SD mounted, size=%llu MB", SD.cardSize() / (1024ULL * 1024ULL));
    } else {
        log_w("SD not available");
    }
    return _sdOk;
}

bool storage_sd_available() { return _sdOk; }

static String todayStamp() {
    time_t now = time(nullptr);
    if (now < 1700000000) {
        // RTC not set; use uptime-based filename
        uint32_t s = millis() / 1000;
        char buf[32];
        snprintf(buf, sizeof(buf), "uptime-%lu", (unsigned long)s / 86400);
        return String(buf);
    }
    struct tm tmv;
    localtime_r(&now, &tmv);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
    return String(buf);
}

void storage_log_event(const String &line) {
    if (!_sdOk) return;
    String path = String(LOG_FILE_DIR) + "/" + todayStamp() + ".csv";
    File f = SD.open(path, FILE_APPEND);
    if (!f) return;
    f.print(millis());
    f.print(',');
    f.println(line);
    f.close();
}

void storage_write_scan_csv(const String &csv) {
    if (!_sdOk) return;
    String path = String(LOG_FILE_DIR) + "/scan-" + String(millis()) + ".csv";
    File f = SD.open(path, FILE_WRITE);
    if (!f) return;
    f.print(csv);
    f.close();
}
