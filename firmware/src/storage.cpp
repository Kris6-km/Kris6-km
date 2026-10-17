#include "storage.h"
#include "config.h"
#include <SD_MMC.h>
#include <time.h>

static bool _sdOk = false;

// Must be called AFTER display_init(): GPIO1/2 are shared with the ST7701 init SPI.
bool storage_begin() {
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    _sdOk = SD_MMC.begin("/sdcard", true);  // 1-bit mode
    if (_sdOk) {
        if (!SD_MMC.exists(LOG_FILE_DIR)) SD_MMC.mkdir(LOG_FILE_DIR);
        log_i("SD mounted, size=%llu MB", SD_MMC.cardSize() / (1024ULL * 1024ULL));
    } else {
        log_w("SD not available");
    }
    return _sdOk;
}

bool storage_sd_available() { return _sdOk; }

static String todayStamp() {
    time_t now = time(nullptr);
    if (now < 1700000000) return "nodate";
    struct tm tmv;
    localtime_r(&now, &tmv);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
    return String(buf);
}

void storage_log_event(const String &line) {
    if (!_sdOk) return;
    File f = SD_MMC.open(String(LOG_FILE_DIR) + "/" + todayStamp() + ".csv", FILE_APPEND);
    if (!f) return;
    f.printf("%lu,%s\n", (unsigned long)millis(), line.c_str());
    f.close();
}

void storage_write_scan_csv(const String &csv) {
    if (!_sdOk) return;
    File f = SD_MMC.open(String(LOG_FILE_DIR) + "/scan-" + String(millis()) + ".csv", FILE_WRITE);
    if (!f) return;
    f.print(csv);
    f.close();
}
