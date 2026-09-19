#include "UartSerial.h"
#include "Logger.h"

UartSerial::UartSerial(uint8_t uartNum, uint8_t rxPin, uint8_t txPin, uint32_t baud)
    : _hwSerial(uartNum)
    , _rxPin(rxPin)
    , _txPin(txPin)
    , _baud(baud)
    , _initialized(false)
{
}

UartSerial::~UartSerial() {
    end();
}

bool UartSerial::initTransport() {
    LOG_DEBUG("UartSerial: Initializing UART (RX=%d, TX=%d, baud=%d)",
              _rxPin, _txPin, _baud);
    _hwSerial.begin(_baud, SERIAL_8N1, _rxPin, _txPin);
    _initialized = true;
    return true;
}

bool UartSerial::setBaudRate(uint32_t baud) {
    if (baud < MIN_BAUD || baud > MAX_BAUD) {
        return false;
    }

    _baud = baud;
    if (_initialized) {
        _hwSerial.updateBaudRate(baud);
    }
    LOG_DEBUG("UartSerial: Baud rate set to %lu", (unsigned long)baud);
    return true;
}

void UartSerial::update() {
    // No-op: HardwareSerial is interrupt-driven
}

bool UartSerial::isConnected() const {
    return _initialized;
}

bool UartSerial::sendData(const String& data) {
    if (!_initialized) {
        LOG_WARN("UartSerial: Cannot send - not initialized");
        return false;
    }

    size_t written = _hwSerial.print(data);
    return written == data.length();
}

bool UartSerial::readLine(String& line) {
    if (!_initialized) {
        return false;
    }

    // Read available characters and accumulate in buffer
    while (_hwSerial.available()) {
        char c = _hwSerial.read();

        if (c == '\n' || c == '\r' || c == '\0') {
            // CR, LF or NUL marks end of line (empty lines are skipped). NUL: the
            // RT4K's power-down line break reads as a run of NUL bytes.
            if (_lineBuffer.length() > 0) {
                line = _lineBuffer;
                _lineBuffer = "";
                return true;
            }
        } else {
            _lineBuffer += c;
            if (_lineBuffer.length() >= MAX_LINE_LENGTH) {
                // Overlong or unterminated data - hand it over as-is
                line = _lineBuffer;
                _lineBuffer = "";
                return true;
            }
        }
    }

    return false;
}

size_t UartSerial::read(uint8_t* buf, size_t maxLen) {
    if (!_initialized) return 0;

    size_t count = 0;
    while (count < maxLen && _hwSerial.available()) {
        buf[count++] = (uint8_t)_hwSerial.read();
    }
    return count;
}

size_t UartSerial::write(const uint8_t* data, size_t length) {
    if (!_initialized) return 0;
    return _hwSerial.write(data, length);
}

size_t UartSerial::writeAvailable() const {
    if (!_initialized) return 0;
    return const_cast<HardwareSerial&>(_hwSerial).availableForWrite();
}

size_t UartSerial::available() const {
    if (!_initialized) {
        return 0;
    }
    // HardwareSerial::available() is not const, so we need to cast
    return const_cast<HardwareSerial&>(_hwSerial).available();
}

void UartSerial::end() {
    if (_initialized) {
        _hwSerial.end();
        _initialized = false;
        _lineBuffer = "";
    }
}
