#include "settings.h"
#include <Preferences.h>
#include "config.h"

static Preferences prefs;

void settings_begin() {
    prefs.begin("mbscan", false);
}

void settings_load(ScannerSettings &s) {
    s.baudrate = prefs.getUInt("baud", MB_DEFAULT_BAUD);
    s.parity   = prefs.getUChar("parity", 0);
    s.stopBits = prefs.getUChar("stop", 1);
    s.scanFirst= prefs.getUChar("first", MB_SCAN_FIRST_ID);
    s.scanLast = prefs.getUChar("last",  MB_SCAN_LAST_ID);
    s.autoBaud = prefs.getBool("autobd", false);
    s.logToSd  = prefs.getBool("log", true);
    s.wifiEnabled = prefs.getBool("wifi", true);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    strncpy(s.wifiSsid, ssid.c_str(), sizeof(s.wifiSsid) - 1);
    strncpy(s.wifiPass, pass.c_str(), sizeof(s.wifiPass) - 1);
}

void settings_save(const ScannerSettings &s) {
    prefs.putUInt("baud", s.baudrate);
    prefs.putUChar("parity", s.parity);
    prefs.putUChar("stop", s.stopBits);
    prefs.putUChar("first", s.scanFirst);
    prefs.putUChar("last",  s.scanLast);
    prefs.putBool("autobd", s.autoBaud);
    prefs.putBool("log", s.logToSd);
    prefs.putBool("wifi", s.wifiEnabled);
    prefs.putString("ssid", s.wifiSsid);
    prefs.putString("pass", s.wifiPass);
}
