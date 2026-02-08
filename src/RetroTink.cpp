#include "RetroTink.h"
#include "SerialInterface.h"
#ifndef NO_USB_HOST
#include "UsbHostSerial.h"
#endif
#include "UartSerial.h"
#include "Logger.h"

RetroTink::RetroTink()
    : _serial(nullptr)
    , _lastCommand("")
    , _powerMgmtMode(PowerManagementMode::FULL)
    , _powerState(RT4KPowerState::UNKNOWN)
    , _pendingCommand("")
    , _bootWaitStart(0)
    , _lastSvsInput(0)
    , _svsKeepAliveTime(0)
    , _svsKeepAlivePending(false)
{
}

RetroTink::~RetroTink() {
    if (_serial) {
        delete _serial;
        _serial = nullptr;
    }
}

void RetroTink::configure(const JsonObject& config) {
    // Read serial mode configuration
    String serialMode = config["serialMode"] | "usb";

    // Read power management mode
    String pmMode = config["powerManagementMode"] | "full";
    if (pmMode == "off") {
        _powerMgmtMode = PowerManagementMode::OFF;
        _powerState = RT4KPowerState::ON;  // Assume always on
    } else if (pmMode == "simple") {
        _powerMgmtMode = PowerManagementMode::SIMPLE;
        _powerState = RT4KPowerState::UNKNOWN;
    } else {
        _powerMgmtMode = PowerManagementMode::FULL;
        _powerState = RT4KPowerState::UNKNOWN;
    }

    LOG_DEBUG("RetroTink: Power management mode: %s", pmMode.c_str());

    // Clean up existing serial
    if (_serial) {
        delete _serial;
        _serial = nullptr;
    }

    // Create appropriate serial interface based on mode
    if (serialMode == "uart") {
        // UART mode - read UART configuration
        uint8_t uartId = config["uartId"] | 2;
        uint8_t txPin = config["txPin"] | 17;
        uint8_t rxPin = config["rxPin"] | 18;

        LOG_DEBUG("RetroTink: Configuring UART mode (UART%d, TX=%d, RX=%d)",
                  uartId, txPin, rxPin);

        // RetroTink uses 115200 baud
        _serial = new UartSerial(uartId, rxPin, txPin, 115200);
    } else {
        // USB mode (default)
#ifndef NO_USB_HOST
        LOG_DEBUG("RetroTink: Configuring USB Host mode");
        _serial = new UsbHostSerial();
#else
        LOG_ERROR("RetroTink: USB Host not available on this platform. Use serialMode=uart.");
#endif
    }
}

bool RetroTink::begin() {
    if (!_serial) {
        LOG_ERROR("RetroTink: Cannot begin - not configured");
        return false;
    }

    if (!_serial->initTransport()) {
        LOG_ERROR("RetroTink: Failed to initialize serial");
        return false;
    }

    LOG_INFO("RetroTink: Controller initialized");
    return true;
}

void RetroTink::update() {
    if (!_serial) return;

    // Process USB Host events
    _serial->update();

    // Read and parse incoming serial data from RT4K
    processIncomingData();

    // Handle pending operations (boot timeout, SVS keep-alive)
    processPendingOperations();
}

void RetroTink::addTrigger(const TriggerMapping& trigger) {
    _triggers.push_back(trigger);

    const char* modeStr = (trigger.mode == TriggerMapping::SVS) ? "SVS" : "Remote";
    LOG_DEBUG("RetroTink: Added trigger - input %d -> profile %d (%s)",
              trigger.switcherInput, trigger.profile, modeStr);
}

void RetroTink::clearTriggers() {
    _triggers.clear();
    LOG_DEBUG("RetroTink: All triggers cleared");
}

void RetroTink::onSwitcherInputChange(int input) {
    const TriggerMapping* trigger = findTrigger(input);

    if (!trigger) {
        LOG_DEBUG("RetroTink: No trigger defined for input %d", input);
        return;
    }

    String command = generateCommand(*trigger);

    // OFF mode: no power management, send immediately
    if (_powerMgmtMode == PowerManagementMode::OFF) {
        sendCommand(command);
        LOG_INFO("RetroTink: Input %d triggered -> %s", input, command.c_str());

        if (trigger->mode == TriggerMapping::SVS) {
            _lastSvsInput = trigger->profile;
            _svsKeepAliveTime = millis();
            _svsKeepAlivePending = true;
        }
        return;
    }

    // SIMPLE mode: first time sends "pwr on" + waits 15s, then always immediate
    if (_powerMgmtMode == PowerManagementMode::SIMPLE) {
        if (_powerState == RT4KPowerState::UNKNOWN) {
            // First input change - send pwr on and wait for boot
            LOG_INFO("RetroTink: First input change (simple mode) - sending pwr on and waiting %lu ms",
                     BOOT_TIMEOUT_MS);
            sendCommand("pwr on");
            _powerState = RT4KPowerState::BOOTING;
            _pendingCommand = command;
            _bootWaitStart = millis();

            if (trigger->mode == TriggerMapping::SVS) {
                _lastSvsInput = trigger->profile;
                _svsKeepAlivePending = false;
            }

            LOG_INFO("RetroTink: Queued command for after boot: %s", command.c_str());
            return;
        }

        // Already ON (or BOOTING with another pending) - send immediately
        if (_powerState == RT4KPowerState::ON) {
            sendCommand(command);
            LOG_INFO("RetroTink: Input %d triggered -> %s", input, command.c_str());

            if (trigger->mode == TriggerMapping::SVS) {
                _lastSvsInput = trigger->profile;
                _svsKeepAliveTime = millis();
                _svsKeepAlivePending = true;
            }
        } else if (_powerState == RT4KPowerState::BOOTING) {
            // Still waiting for initial boot - replace pending command
            _pendingCommand = command;
            if (trigger->mode == TriggerMapping::SVS) {
                _lastSvsInput = trigger->profile;
                _svsKeepAlivePending = false;
            }
            LOG_INFO("RetroTink: Updated pending command: %s", command.c_str());
        }
        return;
    }

    // FULL mode: complete power state tracking via serial messages

    // Check if RT4K needs to be woken up
    if (_serial && _powerState == RT4KPowerState::SLEEPING) {
        // Confirmed sleeping - wake and wait for boot complete
        LOG_INFO("RetroTink: RT4K is sleeping - sending power on before command");
        sendCommand("pwr on");
        _powerState = RT4KPowerState::BOOTING;
        _pendingCommand = command;
        _bootWaitStart = millis();

        // If SVS mode, also queue the keep-alive
        if (trigger->mode == TriggerMapping::SVS) {
            _lastSvsInput = trigger->profile;
            _svsKeepAlivePending = false;  // Will be set after pending command fires
        }

        LOG_INFO("RetroTink: Queued command for after boot: %s", command.c_str());
        return;
    }

    if (_serial && _powerState == RT4KPowerState::UNKNOWN) {
        // Unknown state - send pwr on and wait briefly to see if RT4K responds.
        LOG_INFO("RetroTink: RT4K state unknown - sending pwr on and waiting for response");
        sendCommand("pwr on");
        _powerState = RT4KPowerState::WAKING;
        _pendingCommand = command;
        _bootWaitStart = millis();

        if (trigger->mode == TriggerMapping::SVS) {
            _lastSvsInput = trigger->profile;
            _svsKeepAlivePending = false;
        }

        LOG_INFO("RetroTink: Queued command pending wake response: %s", command.c_str());
        return;
    }

    // RT4K is on - send command directly
    sendCommand(command);
    LOG_INFO("RetroTink: Input %d triggered -> %s", input, command.c_str());

    // For SVS mode, schedule a keep-alive
    if (trigger->mode == TriggerMapping::SVS) {
        _lastSvsInput = trigger->profile;
        _svsKeepAliveTime = millis();
        _svsKeepAlivePending = true;
        LOG_DEBUG("RetroTink: SVS keep-alive scheduled for input %d", _lastSvsInput);
    }
}

void RetroTink::sendRawCommand(const String& command) {
    sendCommand(command);
    LOG_DEBUG("RetroTink: Raw command sent: %s", command.c_str());
}

bool RetroTink::isConnected() const {
    if (!_serial) return false;
    return _serial->isConnected();
}

const char* RetroTink::getPowerStateString() const {
    switch (_powerState) {
        case RT4KPowerState::UNKNOWN:  return "unknown";
        case RT4KPowerState::WAKING:   return "waking";
        case RT4KPowerState::BOOTING:  return "booting";
        case RT4KPowerState::ON:       return "on";
        case RT4KPowerState::SLEEPING: return "sleeping";
        default:                       return "unknown";
    }
}

const TriggerMapping* RetroTink::findTrigger(int input) const {
    for (const auto& trigger : _triggers) {
        if (trigger.switcherInput == input) {
            return &trigger;
        }
    }
    return nullptr;
}

String RetroTink::generateCommand(const TriggerMapping& trigger) const {
    String cmd;

    if (trigger.mode == TriggerMapping::SVS) {
        // SVS format: "SVS NEW INPUT=N"
        cmd = "SVS NEW INPUT=" + String(trigger.profile);
    } else {
        // Remote format: "remote profN"
        cmd = "remote prof" + String(trigger.profile);
    }

    return cmd;
}

void RetroTink::sendCommand(const String& command) {
    _lastCommand = command;

    if (_serial && _serial->isConnected()) {
        // Frame command with leading/trailing CR for RT4K protocol
        String framed = "\r" + command + "\r";
        if (_serial->sendData(framed)) {
            LOG_DEBUG("RetroTink TX: [%s]", command.c_str());
        } else {
            LOG_ERROR("RetroTink: Failed to send command: %s", command.c_str());
        }
    } else {
        LOG_DEBUG("RetroTink TX (stub): [%s]", command.c_str());
    }
}

void RetroTink::processReceivedLine(const String& line) {
    if (line.length() == 0) return;

    // Sanitize line: replace non-printable bytes with '?' to avoid
    // corrupting JSON API responses (garbled bytes arrive during RT4K power transitions)
    String clean;
    clean.reserve(line.length());
    for (unsigned int i = 0; i < line.length(); i++) {
        char c = line.charAt(i);
        if (c >= 0x20 && c < 0x7F) {
            clean += c;
        } else {
            clean += '?';
        }
    }

    LOG_DEBUG("RetroTink RX: %s", clean.c_str());

    // Check for "Powering Up" message - RT4K is waking from sleep
    if (line.indexOf("Powering Up") >= 0) {
        if (_powerState == RT4KPowerState::WAKING) {
            // We were in UNKNOWN and sent pwr on - RT4K was actually off
            LOG_INFO("RetroTink: RT4K powering up confirmed - transitioning to BOOTING");
            _powerState = RT4KPowerState::BOOTING;
            // _bootWaitStart already set, _pendingCommand already queued
        } else if (_powerState != RT4KPowerState::BOOTING) {
            // Spontaneous power-on (e.g., user pressed physical button)
            LOG_INFO("RetroTink: RT4K powering up - power state: BOOTING");
            _powerState = RT4KPowerState::BOOTING;
            _bootWaitStart = millis();
        }
        return;
    }

    // Check for boot complete message
    if (line.indexOf("[MCU] Boot Sequence Complete") >= 0) {
        RT4KPowerState prevState = _powerState;
        _powerState = RT4KPowerState::ON;
        LOG_INFO("RetroTink: RT4K boot complete - power state: ON");

        // If we were waiting for boot to complete, send the pending command
        if ((prevState == RT4KPowerState::BOOTING || prevState == RT4KPowerState::WAKING)
            && _pendingCommand.length() > 0) {
            LOG_INFO("RetroTink: Sending queued command: %s", _pendingCommand.c_str());
            sendCommand(_pendingCommand);

            // If it was an SVS command, schedule keep-alive
            if (_pendingCommand.startsWith("SVS NEW INPUT=")) {
                _svsKeepAliveTime = millis();
                _svsKeepAlivePending = true;
            }

            _pendingCommand = "";
            _bootWaitStart = 0;
        }
        return;
    }

    // Check for power off / sleep messages
    if (line.indexOf("Power Off") >= 0 || line.indexOf("Entering Sleep") >= 0) {
        _powerState = RT4KPowerState::SLEEPING;
        LOG_INFO("RetroTink: RT4K powering off - power state: SLEEPING");
        return;
    }
}

void RetroTink::processIncomingData() {
    if (!_serial) return;

    String line;
    while (_serial->readLine(line)) {
        processReceivedLine(line);
    }
}

void RetroTink::processPendingOperations() {
    unsigned long now = millis();

    // Check for wake response timeout (UNKNOWN -> pwr on sent, waiting for RT4K response)
    if (_powerState == RT4KPowerState::WAKING && _bootWaitStart > 0) {
        if (now - _bootWaitStart >= WAKE_RESPONSE_TIMEOUT_MS) {
            // No "Powering Up" response - RT4K was already on
            LOG_INFO("RetroTink: No wake response after %lu ms - RT4K is already on",
                     WAKE_RESPONSE_TIMEOUT_MS);
            _powerState = RT4KPowerState::ON;

            if (_pendingCommand.length() > 0) {
                LOG_INFO("RetroTink: Sending queued command: %s", _pendingCommand.c_str());
                sendCommand(_pendingCommand);

                if (_pendingCommand.startsWith("SVS NEW INPUT=")) {
                    _svsKeepAliveTime = millis();
                    _svsKeepAlivePending = true;
                }

                _pendingCommand = "";
            }

            _bootWaitStart = 0;
        }
    }

    // Check for boot timeout
    if (_powerState == RT4KPowerState::BOOTING && _bootWaitStart > 0) {
        if (now - _bootWaitStart >= BOOT_TIMEOUT_MS) {
            LOG_WARN("RetroTink: Boot timeout (%lu ms) - sending pending command anyway",
                     BOOT_TIMEOUT_MS);

            if (_pendingCommand.length() > 0) {
                sendCommand(_pendingCommand);

                // Schedule SVS keep-alive if applicable
                if (_pendingCommand.startsWith("SVS NEW INPUT=")) {
                    _svsKeepAliveTime = millis();
                    _svsKeepAlivePending = true;
                }

                _pendingCommand = "";
            }

            _bootWaitStart = 0;
            // SIMPLE mode: assume ON after boot timeout (no serial feedback expected)
            // FULL mode: reset to UNKNOWN since we didn't get boot complete message
            _powerState = (_powerMgmtMode == PowerManagementMode::SIMPLE)
                ? RT4KPowerState::ON : RT4KPowerState::UNKNOWN;
        }
    }

    // Check for SVS keep-alive
    if (_svsKeepAlivePending && (now - _svsKeepAliveTime >= SVS_KEEPALIVE_DELAY_MS)) {
        String keepAlive = "SVS CURRENT INPUT=" + String(_lastSvsInput);
        sendCommand(keepAlive);
        LOG_DEBUG("RetroTink: SVS keep-alive sent: %s", keepAlive.c_str());
        _svsKeepAlivePending = false;
    }
}
