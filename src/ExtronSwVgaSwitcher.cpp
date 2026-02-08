#include "ExtronSwVgaSwitcher.h"
#include "SerialInterface.h"
#include "UartSerial.h"
#include "Logger.h"

ExtronSwVgaSwitcher::ExtronSwVgaSwitcher()
    : _serial(nullptr)
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

ExtronSwVgaSwitcher::~ExtronSwVgaSwitcher() {
    end();
}

void ExtronSwVgaSwitcher::configure(const JsonObject& config) {
    // Read Extron-specific fields from JSON
    uint8_t uartId = config["uartId"] | 1;
    uint8_t txPin = config["txPin"] | 43;
    uint8_t rxPin = config["rxPin"] | 44;
    bool autoSwitch = config["autoSwitch"] | true;

    LOG_DEBUG("ExtronSwVgaSwitcher: Configuring (UART%d, TX=%d, RX=%d, autoSwitch=%d)",
              uartId, txPin, rxPin, autoSwitch);

    // Clean up existing serial
    if (_serial) {
        delete _serial;
        _serial = nullptr;
    }

    // Create UartSerial at 9600 baud for Extron
    _serial = new UartSerial(uartId, rxPin, txPin, 9600);
    _autoSwitchEnabled = autoSwitch;
}

bool ExtronSwVgaSwitcher::begin() {
    if (!_serial) {
        LOG_ERROR("ExtronSwVgaSwitcher: Cannot begin - not configured");
        return false;
    }

    if (!_serial->initTransport()) {
        LOG_ERROR("ExtronSwVgaSwitcher: Failed to initialize serial");
        return false;
    }

    LOG_INFO("ExtronSwVgaSwitcher: Initialized (autoSwitch=%d)", _autoSwitchEnabled);
    return true;
}

void ExtronSwVgaSwitcher::end() {
    if (_serial) {
        delete _serial;
        _serial = nullptr;
    }
}

void ExtronSwVgaSwitcher::update() {
    if (!_serial) {
        return;
    }

    // Read lines from serial and process them
    String line;
    while (_serial->readLine(line)) {
        line.trim();
        if (line.length() > 0) {
            processLine(line);
        }
    }

    // Process signal-based auto-switching
    processAutoSwitch();
}

void ExtronSwVgaSwitcher::processLine(const String& line) {
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

bool ExtronSwVgaSwitcher::isInputMessage(const String& line) {
    // Input messages start with "In" and contain "All" or "Vid"
    // Examples: "In3 All", "In10 Vid", "In1 All"
    return line.startsWith("In") &&
           (line.indexOf("All") > 0 || line.indexOf("Vid") > 0);
}

int ExtronSwVgaSwitcher::parseInputNumber(const String& line) {
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

void ExtronSwVgaSwitcher::onInputChange(InputChangeCallback callback) {
    _inputCallback = callback;
}

void ExtronSwVgaSwitcher::sendCommand(const char* cmd) {
    if (!_serial || !cmd) {
        return;
    }

    LOG_DEBUG("Extron TX: [%s]", cmd);
    _serial->sendData(String(cmd) + "\r\n");
}

std::vector<String> ExtronSwVgaSwitcher::getRecentMessages(int count) {
    std::vector<String> result;
    int start = _recentMessages.size() > count ? _recentMessages.size() - count : 0;

    for (int i = start; i < _recentMessages.size(); i++) {
        result.push_back(_recentMessages[i]);
    }

    return result;
}

void ExtronSwVgaSwitcher::clearRecentMessages() {
    _recentMessages.clear();
}

bool ExtronSwVgaSwitcher::isSigMessage(const String& line) {
    return line.startsWith("Sig ");
}

void ExtronSwVgaSwitcher::parseSigMessage(const String& line) {
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

void ExtronSwVgaSwitcher::processAutoSwitch() {
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
