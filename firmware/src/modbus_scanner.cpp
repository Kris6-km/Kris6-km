#include "modbus_scanner.h"
#include "config.h"
#include <ModbusRTU.h>

// emelianov/modbus-esp8266 Modbus RTU master
static ModbusRTU mb;
static HardwareSerial RSSerial(RS485_UART_NUM);

ModbusScanner Scanner;

// Convert settings (baud, parity, stop) to Arduino SerialConfig
static uint32_t makeSerialConfig(uint8_t parity, uint8_t stop) {
    // parity: 0=N 1=E 2=O ; stop: 1 or 2
    if (stop == 2) {
        if (parity == 1) return SERIAL_8E2;
        if (parity == 2) return SERIAL_8O2;
        return SERIAL_8N2;
    }
    if (parity == 1) return SERIAL_8E1;
    if (parity == 2) return SERIAL_8O1;
    return SERIAL_8N1;
}

void ModbusScanner::begin() {
    _currentBaud = MB_DEFAULT_BAUD;
    _currentParity = 0;
    _currentStop = 1;
    RSSerial.begin(_currentBaud, makeSerialConfig(_currentParity, _currentStop),
                   RS485_RX_PIN, RS485_TX_PIN);
    mb.begin(&RSSerial);
    mb.master();
    log_i("Modbus RTU master on UART%d @ %u baud (RX=%d TX=%d)",
          RS485_UART_NUM, _currentBaud, RS485_RX_PIN, RS485_TX_PIN);
}

void ModbusScanner::applySerial(uint32_t baud, uint8_t parity, uint8_t stop) {
    _currentBaud = baud;
    _currentParity = parity;
    _currentStop = stop;
    RSSerial.updateBaudRate(baud);
    RSSerial.end();
    delay(10);
    RSSerial.begin(baud, makeSerialConfig(parity, stop), RS485_RX_PIN, RS485_TX_PIN);
}

// Blocking helper: run mb.task() until transaction finishes or times out
static bool waitForTransaction(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (mb.slave()) {
        mb.task();
        if (millis() - start > timeoutMs) return false;
        delay(1);
    }
    return true;
}

// Probe a slave by reading 1 holding register @ addr 0.
// A valid Modbus exception (ILLEGAL_DATA_ADDRESS, etc.) still proves presence.
bool ModbusScanner::probeSlave(uint8_t id) {
    volatile Modbus::ResultCode lastRc = Modbus::EX_SUCCESS;
    volatile bool haveResponse = false;

    auto cb = [&](Modbus::ResultCode rc, uint16_t, void*) -> bool {
        lastRc = rc;
        haveResponse = true;
        return true;
    };

    uint16_t tmp = 0;
    if (!mb.readHreg(id, 0, &tmp, 1, cb)) {
        return false;
    }
    if (!waitForTransaction(MB_SCAN_TIMEOUT_MS)) {
        // Timeout -> device absent
        return false;
    }
    if (!haveResponse) return false;

    // Any valid response (including Modbus exception) proves device presence.
    // Only pure timeouts (EX_TIMEOUT) indicate missing slave.
    if (lastRc == Modbus::EX_TIMEOUT || lastRc == Modbus::EX_CONNECTION_LOST) {
        return false;
    }
    return true;
}

std::vector<ModbusDevice> ModbusScanner::scanRange(uint8_t first, uint8_t last, ScanProgressCb cb) {
    std::vector<ModbusDevice> found;
    if (first < 1) first = 1;
    if (last > 247) last = 247;
    if (last < first) return found;

    _busy = true;
    _abort = false;

    for (uint16_t id = first; id <= last; ++id) {
        if (_abort) break;
        if (cb) cb((uint8_t)id, first, last, (int)found.size());

        if (probeSlave((uint8_t)id)) {
            ModbusDevice d{};
            d.id = (uint8_t)id;
            d.baud = _currentBaud;
            d.parity = _currentParity;
            d.stop = _currentStop;
            d.firstSeenMs = millis();
            found.push_back(d);
            log_i("Found Modbus device #%u @ %u baud", id, _currentBaud);
        }
        delay(5); // inter-frame gap
    }
    if (cb) cb(last, first, last, (int)found.size());
    _busy = false;
    return found;
}

std::vector<ModbusDevice> ModbusScanner::scanAutoBaud(uint8_t first, uint8_t last,
                                                      uint32_t &foundBaud, uint8_t &foundParity, uint8_t &foundStop,
                                                      ScanProgressCb cb) {
    std::vector<ModbusDevice> best;
    foundBaud = _currentBaud; foundParity = _currentParity; foundStop = _currentStop;

    for (size_t i = 0; i < MB_BAUD_CANDIDATES_COUNT; ++i) {
        if (_abort) break;
        uint32_t baud = MB_BAUD_CANDIDATES[i];
        applySerial(baud, 0, 1); // Try N81 for each candidate
        log_i("[auto-baud] trying %u 8N1", baud);
        auto devs = scanRange(first, last, cb);
        if (devs.size() > best.size()) {
            best = devs;
            foundBaud = baud; foundParity = 0; foundStop = 1;
            if (best.size() >= 1) {
                // Early-stop once we find something — speeds up detection
                break;
            }
        }
    }
    // Apply the winning config
    applySerial(foundBaud, foundParity, foundStop);
    return best;
}

std::vector<RegisterValue> ModbusScanner::readRegisters(uint8_t slaveId, RegType type,
                                                        uint16_t startAddr, uint16_t count) {
    std::vector<RegisterValue> out;
    if (count == 0 || count > 125) return out;

    uint16_t data[125] = {0};
    volatile Modbus::ResultCode rc = Modbus::EX_SUCCESS;
    volatile bool done = false;

    auto cb = [&](Modbus::ResultCode r, uint16_t, void*) -> bool {
        rc = r;
        done = true;
        return true;
    };

    bool started = false;
    switch (type) {
        case REG_HOLDING:  started = mb.readHreg (slaveId, startAddr, data, count, cb); break;
        case REG_INPUT:    started = mb.readIreg (slaveId, startAddr, data, count, cb); break;
        case REG_COIL:     started = mb.readCoil (slaveId, startAddr, (bool*)data, count, cb); break;
        case REG_DISCRETE: started = mb.readIsts (slaveId, startAddr, (bool*)data, count, cb); break;
    }
    if (!started) {
        _lastErr = "readRegisters: start failed";
        return out;
    }
    if (!waitForTransaction(MB_READ_TIMEOUT_MS)) {
        _lastErr = "timeout";
        return out;
    }

    bool ok = (rc == Modbus::EX_SUCCESS);
    if (!ok) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Modbus exception 0x%02X", (int)rc);
        _lastErr = buf;
    }

    for (uint16_t i = 0; i < count; ++i) {
        RegisterValue v;
        v.address = startAddr + i;
        if (type == REG_COIL || type == REG_DISCRETE) {
            uint8_t *b = (uint8_t*)data;
            v.value = b[i] ? 1 : 0;
        } else {
            v.value = data[i];
        }
        v.ok = ok;
        out.push_back(v);
    }
    return out;
}
