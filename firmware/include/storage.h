#pragma once
#include <Arduino.h>

bool storage_begin();
bool storage_sd_available();

// Append CSV line to today's log file (/logs/YYYY-MM-DD.csv)
void storage_log_event(const String &line);

// Export last scan result CSV
void storage_write_scan_csv(const String &csv);
