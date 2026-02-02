#include "ExtronSwVga.h"
#include "Logger.h"
#include <HardwareSerial.h>

// Use UART1 for Extron communication
static HardwareSerial ExtronSerial(1);

ExtronSwVga::ExtronSwVga(uint8_t txPin, uint8_t rxPin, uint32_t baud)
    : _txPin(txPin)
    , _rxPin(rxPin)
    , _baud(baud)
    , _currentInput(0)
    , _inputCallback(nullptr)
{
}

ExtronSwVga::~ExtronSwVga() {
    end();
}

bool ExtronSwVga::begin() {
    // Configure UART1 with specified pins
    ExtronSerial.begin(_baud, SERIAL_8N1, _rxPin, _txPin);

    LOG_INFO("Extron SW VGA initialized on TX:GPIO%d, RX:GPIO%d at %lu baud",
             _txPin, _rxPin, _baud);
    return true;
}

void ExtronSwVga::end() {
    ExtronSerial.end();
}

void ExtronSwVga::update() {
    // Read available data and process line by line
    while (ExtronSerial.available()) {
        int c = ExtronSerial.read();
        if (c < 0) {
            break;
        }

        char ch = static_cast<char>(c);

        if (ch == '\n') {
            // End of line - process it
            _lineBuffer.trim();
            if (_lineBuffer.length() > 0) {
                processLine(_lineBuffer);
            }
            _lineBuffer = "";
        } else if (ch != '\r') {
            // Accumulate characters (ignore CR)
            _lineBuffer += ch;
        }
    }
}

void ExtronSwVga::processLine(const String& line) {
    LOG_DEBUG("Extron RX: [%s]", line.c_str());

    // Store in recent messages buffer
    _recentMessages.push_back(line);
    if (_recentMessages.size() > MAX_RECENT_MESSAGES) {
        _recentMessages.erase(_recentMessages.begin());
    }

    if (isInputMessage(line)) {
        int input = parseInputNumber(line);
        if (input > 0) {
            _currentInput = input;
            LOG_INFO("Extron input changed to: %d", input);

            if (_inputCallback) {
                _inputCallback(input);
            }
        }
    }
}

bool ExtronSwVga::isInputMessage(const String& line) {
    // Input messages start with "In" and contain "All" or "Vid"
    // Examples: "In3 All", "In10 Vid", "In1 All"
    return line.startsWith("In") &&
           (line.indexOf("All") > 0 || line.indexOf("Vid") > 0);
}

int ExtronSwVga::parseInputNumber(const String& line) {
    // Input number is between "In" (pos 2) and first space
    // "In3 All" -> 3
    // "In10 Vid" -> 10
    int spaceIdx = line.indexOf(' ');
    if (spaceIdx <= 2) {
        return -1;
    }

    String numStr = line.substring(2, spaceIdx);
    return numStr.toInt();
}

void ExtronSwVga::onInputChange(InputChangeCallback callback) {
    _inputCallback = callback;
}

void ExtronSwVga::sendCommand(const char* cmd) {
    if (cmd) {
        LOG_DEBUG("Extron TX: [%s]", cmd);
        ExtronSerial.print(cmd);
        ExtronSerial.print("\r\n");
    }
}

std::vector<String> ExtronSwVga::getRecentMessages(int count) {
    std::vector<String> result;
    int start = _recentMessages.size() > count ? _recentMessages.size() - count : 0;

    for (int i = start; i < _recentMessages.size(); i++) {
        result.push_back(_recentMessages[i]);
    }

    return result;
}

void ExtronSwVga::clearRecentMessages() {
    _recentMessages.clear();
}
