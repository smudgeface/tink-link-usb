#include "extron_sw_vga.h"
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

    Serial.printf("Extron SW VGA initialized on TX:GPIO%d, RX:GPIO%d at %lu baud\n",
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
    Serial.printf("Extron RX: [%s]\n", line.c_str());

    if (isInputMessage(line)) {
        int input = parseInputNumber(line);
        if (input > 0) {
            _currentInput = input;
            Serial.printf("Extron input changed to: %d\n", input);

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
        Serial.printf("Extron TX: [%s]\n", cmd);
        ExtronSerial.print(cmd);
        ExtronSerial.print("\r\n");
    }
}
