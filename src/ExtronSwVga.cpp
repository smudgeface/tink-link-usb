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
    , _autoSwitchEnabled(false)
    , _signalWasLost(false)
    , _numSigInputs(0)
    , _sigChangeTime(0)
{
    memset(_lastSigState, 0, sizeof(_lastSigState));
    memset(_stableSigState, 0, sizeof(_stableSigState));
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

    // Process signal-based auto-switching
    processAutoSwitch();
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
    } else if (isSigMessage(line)) {
        parseSigMessage(line);
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

bool ExtronSwVga::isSigMessage(const String& line) {
    return line.startsWith("Sig ");
}

void ExtronSwVga::parseSigMessage(const String& line) {
    // Parse "Sig 0 1 0 0" -> array of 0/1 values for each input
    int newState[MAX_SIG_INPUTS];
    int count = 0;

    for (unsigned int i = 4; i < line.length() && count < MAX_SIG_INPUTS; i++) {
        char c = line.charAt(i);
        if (c == '0' || c == '1') {
            newState[count++] = c - '0';
        }
    }

    if (count == 0) return;

    // Check if this differs from the most recently received state
    bool changed = (count != _numSigInputs);
    if (!changed) {
        for (int i = 0; i < count; i++) {
            if (newState[i] != _lastSigState[i]) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        memcpy(_lastSigState, newState, sizeof(int) * count);
        _numSigInputs = count;
        _sigChangeTime = millis();
    }
}

void ExtronSwVga::processAutoSwitch() {
    if (!_autoSwitchEnabled || _numSigInputs == 0 || _sigChangeTime == 0) return;

    // Check if pending signal state differs from stable
    bool changed = false;
    for (int i = 0; i < _numSigInputs; i++) {
        if (_lastSigState[i] != _stableSigState[i]) {
            changed = true;
            break;
        }
    }

    if (!changed) return;

    // Wait for debounce period
    if (millis() - _sigChangeTime < SIG_DEBOUNCE_MS) return;

    // Debounce complete - update stable state
    memcpy(_stableSigState, _lastSigState, sizeof(int) * _numSigInputs);

    // Find highest active input (1-based)
    int highestActive = 0;
    for (int i = _numSigInputs - 1; i >= 0; i--) {
        if (_stableSigState[i] == 1) {
            highestActive = i + 1;
            break;
        }
    }

    if (highestActive == 0) {
        // All signals lost - don't switch, but remember for when signal returns
        _signalWasLost = true;
        LOG_DEBUG("Extron: All signals lost - keeping current input %d", _currentInput);
        return;
    }

    if (highestActive == _currentInput && !_signalWasLost) return;

    if (highestActive == _currentInput && _signalWasLost) {
        // Signal restored on current input after loss - re-trigger callback
        // so RT4K auto-wake and profile load can run
        _signalWasLost = false;
        LOG_INFO("Extron: Signal restored on current input %d - re-triggering", highestActive);
        if (_inputCallback) {
            _inputCallback(highestActive);
        }
        return;
    }

    // Different input - send switch command
    _signalWasLost = false;
    LOG_INFO("Extron: Signal detected on input %d - auto-switching", highestActive);
    String cmd = String(highestActive) + "!";
    sendCommand(cmd.c_str());
}
