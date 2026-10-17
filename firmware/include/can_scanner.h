#pragma once
#include <Arduino.h>
#include <vector>
#include <functional>

struct CanNode {
    uint32_t id;          // 11-bit or 29-bit identifier
    bool     extended;    // true = 29-bit
    uint32_t frameCount;  // how many frames observed
    uint8_t  dlc;         // last data length
    uint8_t  lastData[8]; // last payload snapshot
    uint32_t firstSeenMs;
    uint32_t lastSeenMs;
};

using CanScanProgressCb = std::function<void(uint32_t elapsedMs, uint32_t totalMs, size_t uniqueIds, uint32_t frames)>;

class CanScanner {
public:
    bool begin(uint32_t bitrate);                    // install driver
    void end();                                      // uninstall driver
    bool running() const { return _running; }
    uint32_t currentBitrate() const { return _bitrate; }

    // Passive listen: collect unique IDs for durationMs (TWAI_MODE_LISTEN_ONLY)
    std::vector<CanNode> scanPassive(uint32_t durationMs, CanScanProgressCb cb = nullptr);

    // Try all candidate bitrates until frames are received (returns detected bitrate or 0)
    uint32_t autoDetectBitrate(uint32_t perStepMs = 1200);

    // Send a single frame (requires normal mode)
    bool sendFrame(uint32_t id, bool extended, const uint8_t *data, uint8_t dlc, uint32_t timeoutMs = 100);

    // Switch between normal and listen-only (reinstalls driver)
    bool setMode(bool listenOnly);

    String lastError() const { return _lastErr; }
    void abort() { _abort = true; }

private:
    bool     _running = false;
    bool     _listenOnly = true;
    bool     _abort = false;
    uint32_t _bitrate = 0;
    String   _lastErr;
};

extern CanScanner CanScan;
