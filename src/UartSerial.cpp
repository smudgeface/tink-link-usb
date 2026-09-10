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

        if (c == '\n' || c == '\r') {
            // CR or LF marks end of line (empty lines from CR+LF pairs are skipped)
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
