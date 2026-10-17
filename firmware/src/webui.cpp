#include "webui.h"
#include "config.h"
#include "modbus_scanner.h"
#include "can_scanner.h"
#include "settings.h"
#include "storage.h"
#include "ui.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

extern ScannerSettings g_settings;
extern std::vector<ModbusDevice> g_last_scan;

// CAN state shared with /api/can/*
static std::vector<CanNode> g_last_can_web;
static volatile bool _canBusy = false;

static AsyncWebServer server(WEB_PORT);
static bool _apMode = false;

static void startAP() {
    uint8_t mac[6]; WiFi.macAddress(mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, AP_PASSWORD);
    _apMode = true;
    log_i("AP started: SSID=%s PASS=%s IP=%s", ssid, AP_PASSWORD, WiFi.softAPIP().toString().c_str());
    ui_set_wifi_info(String("AP: ") + ssid + "  " + WiFi.softAPIP().toString());
}

static void startSTA() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_settings.wifiSsid, g_settings.wifiPass);
    log_i("Connecting to WiFi SSID='%s' ...", g_settings.wifiSsid);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
        log_i("WiFi connected: %s", WiFi.localIP().toString().c_str());
        ui_set_wifi_info(String("WiFi: ") + g_settings.wifiSsid + "  " + WiFi.localIP().toString());
    } else {
        log_w("WiFi connect failed, falling back to AP");
        startAP();
    }
}

String web_ip_info() {
    if (_apMode) return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
}

static void handle_status(AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["busy"] = Scanner.busy();
    doc["baud"] = g_settings.baudrate;
    doc["parity"] = g_settings.parity;
    doc["stop"]  = g_settings.stopBits;
    doc["autoBaud"] = g_settings.autoBaud;
    doc["sd"] = storage_sd_available();
    doc["ip"] = web_ip_info();
    JsonArray arr = doc["devices"].to<JsonArray>();
    for (auto &d : g_last_scan) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = d.id; o["baud"] = d.baud;
        o["parity"] = d.parity; o["stop"] = d.stop;
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
}

static void handle_scan(AsyncWebServerRequest *req) {
    if (Scanner.busy()) {
        req->send(409, "application/json", "{\"error\":\"busy\"}");
        return;
    }
    // Scan runs via UI progress callback; here spawn a task
    xTaskCreatePinnedToCore([](void*){
        extern std::vector<ModbusDevice> g_last_scan;
        if (g_settings.autoBaud) {
            uint32_t fb; uint8_t fp, fs;
            g_last_scan = Scanner.scanAutoBaud(g_settings.scanFirst, g_settings.scanLast, fb, fp, fs);
            g_settings.baudrate = fb; g_settings.parity = fp; g_settings.stopBits = fs;
            settings_save(g_settings);
        } else {
            Scanner.applySerial(g_settings.baudrate, g_settings.parity, g_settings.stopBits);
            g_last_scan = Scanner.scanRange(g_settings.scanFirst, g_settings.scanLast);
        }
        ui_set_scan_result((int)g_last_scan.size());
        vTaskDelete(nullptr);
    }, "web_scan", 6144, nullptr, 1, nullptr, 1);
    req->send(202, "application/json", "{\"status\":\"started\"}");
}

static void handle_settings_get(AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["baudrate"] = g_settings.baudrate;
    doc["parity"] = g_settings.parity;
    doc["stopBits"] = g_settings.stopBits;
    doc["scanFirst"] = g_settings.scanFirst;
    doc["scanLast"]  = g_settings.scanLast;
    doc["autoBaud"]  = g_settings.autoBaud;
    doc["logToSd"]   = g_settings.logToSd;
    doc["wifiSsid"]  = g_settings.wifiSsid;
    doc["wifiEnabled"] = g_settings.wifiEnabled;
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
}

static void handle_settings_post(AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        req->send(400, "application/json", "{\"error\":\"json\"}");
        return;
    }
    if (doc["baudrate"].is<uint32_t>()) g_settings.baudrate = doc["baudrate"];
    if (doc["parity"].is<int>())        g_settings.parity   = doc["parity"];
    if (doc["stopBits"].is<int>())      g_settings.stopBits = doc["stopBits"];
    if (doc["scanFirst"].is<int>())     g_settings.scanFirst = doc["scanFirst"];
    if (doc["scanLast"].is<int>())      g_settings.scanLast  = doc["scanLast"];
    if (doc["autoBaud"].is<bool>())     g_settings.autoBaud  = doc["autoBaud"];
    if (doc["logToSd"].is<bool>())      g_settings.logToSd   = doc["logToSd"];
    if (doc["wifiSsid"].is<const char*>()) strncpy(g_settings.wifiSsid, doc["wifiSsid"], sizeof(g_settings.wifiSsid)-1);
    if (doc["wifiPass"].is<const char*>()) strncpy(g_settings.wifiPass, doc["wifiPass"], sizeof(g_settings.wifiPass)-1);
    settings_save(g_settings);
    Scanner.applySerial(g_settings.baudrate, g_settings.parity, g_settings.stopBits);
    req->send(200, "application/json", "{\"ok\":true}");
}

static void handle_read_regs(AsyncWebServerRequest *req) {
    if (!req->hasParam("id") || !req->hasParam("type") ||
        !req->hasParam("start") || !req->hasParam("count")) {
        req->send(400, "application/json", "{\"error\":\"missing params\"}");
        return;
    }
    uint8_t id = req->getParam("id")->value().toInt();
    String type = req->getParam("type")->value();
    uint16_t start = req->getParam("start")->value().toInt();
    uint16_t count = req->getParam("count")->value().toInt();
    RegType rt = REG_HOLDING;
    if (type == "input") rt = REG_INPUT;
    else if (type == "coil") rt = REG_COIL;
    else if (type == "discrete") rt = REG_DISCRETE;

    auto rv = Scanner.readRegisters(id, rt, start, count);
    JsonDocument doc;
    doc["id"] = id; doc["type"] = type; doc["start"] = start; doc["count"] = count;
    JsonArray arr = doc["values"].to<JsonArray>();
    for (auto &v : rv) {
        JsonObject o = arr.add<JsonObject>();
        o["addr"] = v.address;
        o["value"] = v.value;
        o["ok"] = v.ok;
    }
    if (rv.empty() || !rv[0].ok) {
        doc["error"] = Scanner.lastError();
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
}

void web_begin() {
    if (!g_settings.wifiEnabled) {
        log_i("WiFi disabled");
        ui_set_wifi_info("WiFi: wyłączone");
        return;
    }
    if (strlen(g_settings.wifiSsid) > 0) startSTA();
    else startAP();

    if (!LittleFS.begin(true)) log_w("LittleFS mount failed");

    // Serve static webui from LittleFS /www
    server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

    server.on("/api/status", HTTP_GET, handle_status);
    server.on("/api/scan",   HTTP_POST, handle_scan);
    server.on("/api/settings", HTTP_GET, handle_settings_get);
    server.on("/api/settings", HTTP_POST,
              [](AsyncWebServerRequest *){}, nullptr, handle_settings_post);
    server.on("/api/read",   HTTP_GET, handle_read_regs);

    // ---------- CAN ----------
    server.on("/api/can/status", HTTP_GET, [](AsyncWebServerRequest *req){
        JsonDocument doc;
        doc["busy"]    = _canBusy;
        doc["bitrate"] = g_settings.canBitrate;
        doc["autobaud"]= g_settings.canAutoBaud;
        doc["scanMs"]  = g_settings.canScanMs;
        JsonArray arr = doc["nodes"].to<JsonArray>();
        for (auto &n : g_last_can_web) {
            JsonObject o = arr.add<JsonObject>();
            o["id"] = n.id;
            o["extended"] = n.extended;
            o["dlc"] = n.dlc;
            o["frames"] = n.frameCount;
            char hex[20];
            char *p = hex;
            for (int i = 0; i < n.dlc && i < 8; ++i) p += sprintf(p, "%02X ", n.lastData[i]);
            if (p > hex) *(p-1) = 0;
            o["data"] = hex;
        }
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    server.on("/api/can/scan", HTTP_POST,
        [](AsyncWebServerRequest *req){},
        nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t){
            if (_canBusy) { req->send(409, "application/json", "{\"error\":\"busy\"}"); return; }
            JsonDocument doc;
            if (len > 0 && deserializeJson(doc, data, len) == DeserializationError::Ok) {
                if (doc["bitrate"].is<uint32_t>()) g_settings.canBitrate  = doc["bitrate"];
                if (doc["autobaud"].is<bool>())    g_settings.canAutoBaud = doc["autobaud"];
                if (doc["scanMs"].is<uint32_t>())  g_settings.canScanMs   = doc["scanMs"];
                settings_save(g_settings);
            }
            _canBusy = true;
            xTaskCreatePinnedToCore([](void*){
                uint32_t br = g_settings.canBitrate;
                if (g_settings.canAutoBaud) {
                    br = CanScan.autoDetectBitrate(1200);
                    if (br == 0) { _canBusy = false; vTaskDelete(nullptr); return; }
                    g_settings.canBitrate = br;
                    settings_save(g_settings);
                } else {
                    CanScan.setMode(true);
                    if (!CanScan.begin(br)) { _canBusy = false; vTaskDelete(nullptr); return; }
                }
                g_last_can_web = CanScan.scanPassive(g_settings.canScanMs);
                CanScan.end();
                _canBusy = false;
                vTaskDelete(nullptr);
            }, "can_web", 6144, nullptr, 1, nullptr, 1);
            req->send(202, "application/json", "{\"status\":\"started\"}");
        });

    server.on("/api/can/send", HTTP_POST,
        [](AsyncWebServerRequest *req){},
        nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t){
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"json\"}"); return;
            }
            uint32_t id   = doc["id"]       | 0;
            bool ext      = doc["extended"] | false;
            String hex    = doc["data"].as<String>();
            uint32_t br   = doc["bitrate"]  | g_settings.canBitrate;

            uint8_t buf[8] = {0};
            uint8_t dlc = 0;
            hex.replace(" ", "");
            for (size_t i = 0; i + 1 < hex.length() && dlc < 8; i += 2) {
                buf[dlc++] = (uint8_t)strtol(hex.substring(i, i+2).c_str(), nullptr, 16);
            }
            CanScan.setMode(false);
            if (!CanScan.begin(br)) {
                req->send(500, "application/json",
                    (String("{\"error\":\"") + CanScan.lastError() + "\"}").c_str());
                return;
            }
            bool ok = CanScan.sendFrame(id, ext, buf, dlc, 500);
            String err = CanScan.lastError();
            CanScan.end();
            CanScan.setMode(true); // revert
            if (!ok) {
                req->send(500, "application/json",
                    (String("{\"error\":\"") + err + "\"}").c_str());
                return;
            }
            req->send(200, "application/json", "{\"ok\":true}");
        });

    server.onNotFound([](AsyncWebServerRequest *r){
        r->send(404, "application/json", "{\"error\":\"not found\"}");
    });

    server.begin();
    log_i("Web server started on port %d", WEB_PORT);
}
