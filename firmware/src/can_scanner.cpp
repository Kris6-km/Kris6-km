#include "can_scanner.h"
#include "config.h"
#include <driver/twai.h>

CanScanner CanScan;

// Pick ESP-IDF TWAI timing config for a given bitrate.
static bool fillTiming(uint32_t bitrate, twai_timing_config_t &t) {
    switch (bitrate) {
        case 1000000: t = TWAI_TIMING_CONFIG_1MBITS();  return true;
        case 500000:  t = TWAI_TIMING_CONFIG_500KBITS();return true;
        case 250000:  t = TWAI_TIMING_CONFIG_250KBITS();return true;
        case 125000:  t = TWAI_TIMING_CONFIG_125KBITS();return true;
        case 100000:  t = TWAI_TIMING_CONFIG_100KBITS();return true;
        case 50000:   t = TWAI_TIMING_CONFIG_50KBITS(); return true;
        case 25000:   t = TWAI_TIMING_CONFIG_25KBITS(); return true;
        default: return false;
    }
}

bool CanScanner::begin(uint32_t bitrate) {
    end(); // ensure clean state

    twai_timing_config_t t_cfg;
    if (!fillTiming(bitrate, t_cfg)) {
        _lastErr = "unsupported bitrate";
        return false;
    }
    twai_general_config_t g_cfg = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN,
        _listenOnly ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);
    g_cfg.rx_queue_len = 64;
    g_cfg.tx_queue_len = 16;
    twai_filter_config_t f_cfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_cfg, &t_cfg, &f_cfg);
    if (err != ESP_OK) {
        _lastErr = String("twai_driver_install=") + esp_err_to_name(err);
        return false;
    }
    err = twai_start();
    if (err != ESP_OK) {
        _lastErr = String("twai_start=") + esp_err_to_name(err);
        twai_driver_uninstall();
        return false;
    }
    _bitrate = bitrate;
    _running = true;
    log_i("TWAI started: %u bps, TX=%d RX=%d mode=%s",
          bitrate, CAN_TX_PIN, CAN_RX_PIN, _listenOnly ? "LISTEN_ONLY" : "NORMAL");
    return true;
}

void CanScanner::end() {
    if (_running) {
        twai_stop();
        twai_driver_uninstall();
        _running = false;
    }
}

bool CanScanner::setMode(bool listenOnly) {
    _listenOnly = listenOnly;
    if (_running) return begin(_bitrate);
    return true;
}

std::vector<CanNode> CanScanner::scanPassive(uint32_t durationMs, CanScanProgressCb cb) {
    std::vector<CanNode> nodes;
    if (!_running) {
        _lastErr = "driver not running";
        return nodes;
    }
    _abort = false;
    uint32_t start = millis();
    uint32_t frames = 0;

    while (!_abort && millis() - start < durationMs) {
        twai_message_t msg;
        esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(50));
        if (err == ESP_OK) {
            frames++;
            uint32_t id = msg.identifier;
            bool ext = msg.extd;
            auto it = std::find_if(nodes.begin(), nodes.end(),
                [&](const CanNode &n){ return n.id == id && n.extended == ext; });
            if (it == nodes.end()) {
                CanNode n{};
                n.id = id; n.extended = ext;
                n.frameCount = 1;
                n.dlc = msg.data_length_code;
                memcpy(n.lastData, msg.data, msg.data_length_code);
                n.firstSeenMs = millis();
                n.lastSeenMs  = millis();
                nodes.push_back(n);
            } else {
                it->frameCount++;
                it->dlc = msg.data_length_code;
                memcpy(it->lastData, msg.data, msg.data_length_code);
                it->lastSeenMs = millis();
            }
        }
        if (cb) cb(millis() - start, durationMs, nodes.size(), frames);
    }
    // Sort by frame count desc for nicer display
    std::sort(nodes.begin(), nodes.end(),
        [](const CanNode &a, const CanNode &b){ return a.frameCount > b.frameCount; });
    return nodes;
}

uint32_t CanScanner::autoDetectBitrate(uint32_t perStepMs) {
    _abort = false;
    for (size_t i = 0; i < CAN_BITRATE_COUNT; ++i) {
        if (_abort) return 0;
        uint32_t b = CAN_BITRATE_CANDIDATES[i];
        _listenOnly = true;
        if (!begin(b)) continue;
        log_i("[can-auto] trying %u bps", b);

        uint32_t start = millis();
        uint32_t frames = 0;
        twai_status_info_t st;
        while (millis() - start < perStepMs) {
            twai_message_t msg;
            if (twai_receive(&msg, pdMS_TO_TICKS(50)) == ESP_OK) {
                frames++;
            }
            twai_get_status_info(&st);
            // if bus-off or many errors, bitrate is wrong -> move on
            if (st.state == TWAI_STATE_BUS_OFF) break;
        }
        end();
        if (frames > 0) {
            log_i("[can-auto] detected %u bps (%u frames)", b, frames);
            // Keep driver installed at detected bitrate for subsequent scan
            if (begin(b)) return b;
            return b;
        }
    }
    _lastErr = "no CAN traffic detected";
    return 0;
}

bool CanScanner::sendFrame(uint32_t id, bool extended, const uint8_t *data, uint8_t dlc, uint32_t timeoutMs) {
    if (!_running) { _lastErr = "driver not running"; return false; }
    if (_listenOnly) { _lastErr = "listen-only mode"; return false; }
    if (dlc > 8) dlc = 8;
    twai_message_t msg = {};
    msg.identifier = id;
    msg.extd = extended ? 1 : 0;
    msg.data_length_code = dlc;
    memcpy(msg.data, data, dlc);
    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(timeoutMs));
    if (err != ESP_OK) {
        _lastErr = String("twai_transmit=") + esp_err_to_name(err);
        return false;
    }
    return true;
}
