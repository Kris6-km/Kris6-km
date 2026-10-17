#pragma once
#include <Arduino.h>
#include <vector>
#include <functional>
#include "settings.h"

struct ModbusDevice {
    uint8_t  id;
    uint32_t baud;
    uint8_t  parity;
    uint8_t  stop;
    uint32_t firstSeenMs;
    String   note;
};

struct RegisterValue {
    uint16_t address;
    uint16_t value;
    bool     ok;
};

enum RegType {
    REG_HOLDING = 3,
    REG_INPUT   = 4,
    REG_COIL    = 1,
    REG_DISCRETE= 2,
};

using ScanProgressCb = std::function<void(uint8_t currentId, uint8_t firstId, uint8_t lastId, int foundCount)>;

class ModbusScanner {
public:
    void begin();

    // Reconfigure UART2 with given serial params
    void applySerial(uint32_t baud, uint8_t parity, uint8_t stop);

    // Scan single range at current serial config
    std::vector<ModbusDevice> scanRange(uint8_t first, uint8_t last, ScanProgressCb cb = nullptr);

    // Try all baudrate candidates, returns the best (most devices) config and its devices
    std::vector<ModbusDevice> scanAutoBaud(uint8_t first, uint8_t last,
                                            uint32_t &foundBaud, uint8_t &foundParity, uint8_t &foundStop,
                                            ScanProgressCb cb = nullptr);

    // Read N consecutive registers of given type from slave
    std::vector<RegisterValue> readRegisters(uint8_t slaveId, RegType type, uint16_t startAddr, uint16_t count);

    // Last error string from the underlying library
    String lastError() const { return _lastErr; }

    bool busy() const { return _busy; }
    void abort() { _abort = true; }

private:
    bool    _busy = false;
    bool    _abort = false;
    String  _lastErr;
    uint32_t _currentBaud = 9600;
    uint8_t  _currentParity = 0;
    uint8_t  _currentStop = 1;

    bool probeSlave(uint8_t id);
};

extern ModbusScanner Scanner;
