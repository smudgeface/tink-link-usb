#include "RetroTink.h"
#include "SerialInterface.h"
#ifndef NO_USB_HOST
#include "UsbHostSerial.h"
#endif
#include "UartSerial.h"
#include "Logger.h"

// RT4K firmware 1.75+ defaults its USB serial port to 2,000,000 baud (was 115,200)
static const uint32_t DEFAULT_USB_BAUD = 2000000;
// RT4K HD-15 serial port baud rate
static const uint32_t DEFAULT_UART_BAUD = 115200;

RetroTink::RetroTink()
    : _serial(nullptr)
    , _defaultBaudRate(DEFAULT_USB_BAUD)
    , _baudFlushAt(0)
    , _lastCommand("")
    , _rawOpen(false)
    , _rawBuffer(nullptr)
    , _rawCapacity(0)
    , _rawTotal(0)
    , _rawLastRx(0)
    , _rawMutex(xSemaphoreCreateMutex())
    , _rawLineClean(true)
    , _powerMgmtMode(PowerManagementMode::FULL)
    , _powerState(RT4KPowerState::UNKNOWN)
    , _pendingCommand("")
    , _bootWaitStart(0)
    , _requestedInput(0)
    , _wakePending(false)
    , _bootProbing(false)
    , _comReplySeen(false)
    , _wasConnected(false)
    , _nextBootProbe(0)
    , _nextIdleProbe(0)
    , _probeSentAt(0)
    , _probeQuietUntil(0)
    , _lastBreakCount(0)
    , _lastSvsInput(0)
    , _svsKeepAliveTime(0)
    , _svsKeepAlivePending(false)
{
}

RetroTink::~RetroTink() {
    free(_rawBuffer);
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

    _defaultBaudRate = (serialMode == "uart") ? DEFAULT_UART_BAUD : DEFAULT_USB_BAUD;

    // Create appropriate serial interface based on mode
    if (serialMode == "uart") {
        // UART mode - read UART configuration
        uint8_t uartId = config["uartId"] | 2;
        uint8_t txPin = config["txPin"] | 17;
        uint8_t rxPin = config["rxPin"] | 18;

        uint32_t baud = config["baudRate"] | DEFAULT_UART_BAUD;

        LOG_DEBUG("RetroTink: Configuring UART mode (UART%d, TX=%d, RX=%d, %lu baud)",
                  uartId, txPin, rxPin, (unsigned long)baud);

        _serial = new UartSerial(uartId, rxPin, txPin, baud);
    } else {
        // USB mode (default)
#ifndef NO_USB_HOST
        uint32_t baud = config["baudRate"] | DEFAULT_USB_BAUD;
        if (!UsbHostSerial::isSupportedBaud(baud)) {
            LOG_ERROR("RetroTink: Unsupported USB baud rate %lu - using %lu",
                      (unsigned long)baud, (unsigned long)DEFAULT_USB_BAUD);
            baud = DEFAULT_USB_BAUD;
        }

        LOG_DEBUG("RetroTink: Configuring USB Host mode (%lu baud)", (unsigned long)baud);
        _serial = new UsbHostSerial(baud);
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

    // Capture window for the raw channel: PSRAM if the board has it
    _rawCapacity = RAW_CAPTURE_PSRAM_BYTES;
    _rawBuffer = (uint8_t*)heap_caps_malloc(_rawCapacity, MALLOC_CAP_SPIRAM);
    if (!_rawBuffer) {
        _rawCapacity = RAW_CAPTURE_HEAP_BYTES;
        _rawBuffer = (uint8_t*)malloc(_rawCapacity);
    }
    if (!_rawBuffer) _rawCapacity = 0;

    LOG_INFO("RetroTink: Controller initialized (%u KB raw capture window)", (unsigned)(_rawCapacity / 1024));
    return true;
}

void RetroTink::update() {
    if (!_serial) return;

    // Process USB Host events
    _serial->update();

    // Read incoming serial data from RT4K: captured verbatim while the raw
    // channel is open, otherwise parsed as lines
    if (_rawOpen) {
        captureIncomingData();
    } else {
        processIncomingData();
    }

    // Trigger requested from another task (web API)
    if (_requestedInput > 0) {
        int input = _requestedInput;
        _requestedInput = 0;
        onSwitcherInputChange(input);
    }

    // Send commands that were held back while the raw channel was open
    if (!_rawOpen && !_deferredCommands.empty()) {
        xSemaphoreTake(_rawMutex, portMAX_DELAY);
        std::vector<String> commands;
        commands.swap(_deferredCommands);
        xSemaphoreGive(_rawMutex);
        for (const String& command : commands) {
            sendCommand(command);
        }
    }

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

    // FULL mode: verify the power state with the RT4K before sending.
    // The command waits in _pendingCommand until the RT4K is known to be on.
    if (trigger->mode == TriggerMapping::SVS) {
        _lastSvsInput = trigger->profile;
        _svsKeepAlivePending = false;  // Scheduled when the command is actually sent
    }
    _pendingCommand = command;
    LOG_INFO("RetroTink: Input %d triggered -> %s", input, command.c_str());

    if (_powerState == RT4KPowerState::WAKING || _powerState == RT4KPowerState::BOOTING) {
        // Already waiting for the RT4K - the newer command simply replaces the queued one
        LOG_INFO("RetroTink: Still waiting for the RT4K - queued command updated");
        return;
    }

    _wakePending = true;  // Started by update(), once the serial link is free
}

void RetroTink::startWake() {
    // "pwr on" doubles as a power state query on firmware 1.75+: the RT4K
    // answers "Power On Requested" if it was asleep and "Bad Command: pwr on"
    // if it is already on. (A sleeping RT4K ignores every other command.)
    _wakePending = false;
    _bootProbing = false;
    _powerState = RT4KPowerState::WAKING;
    _bootWaitStart = millis();
    sendCommand("pwr on");
}

void RetroTink::sendPendingCommand() {
    if (_pendingCommand.length() == 0) return;

    LOG_INFO("RetroTink: Sending queued command: %s", _pendingCommand.c_str());
    sendCommand(_pendingCommand);

    // For SVS mode, schedule a keep-alive
    if (_pendingCommand.startsWith("SVS NEW INPUT=")) {
        _svsKeepAliveTime = millis();
        _svsKeepAlivePending = true;
    }
    _pendingCommand = "";
}

void RetroTink::setPowerState(RT4KPowerState state, const char* reason) {
    if (_powerState == state) return;
    _powerState = state;
    LOG_INFO("RetroTink: Power state: %s (%s)", getPowerStateString(), reason);
}

bool RetroTink::requestTrigger(int input) {
    if (!findTrigger(input)) return false;
    _requestedInput = input;
    return true;
}

void RetroTink::sendRawCommand(const String& command) {
    sendCommand(command);
    LOG_DEBUG("RetroTink: Raw command sent: %s", command.c_str());
}

String RetroTink::getLastCommand() const {
    xSemaphoreTake(_rawMutex, portMAX_DELAY);
    String command = _lastCommand;
    xSemaphoreGive(_rawMutex);
    return command;
}

bool RetroTink::isConnected() const {
    if (!_serial) return false;
    return _serial->isConnected();
}

bool RetroTink::setBaudRate(uint32_t baud) {
    if (!_serial || !_serial->setBaudRate(baud)) {
        LOG_WARN("RetroTink: Baud rate %lu not supported", (unsigned long)baud);
        return false;
    }

    LOG_INFO("RetroTink: Baud rate set to %lu", (unsigned long)baud);
    _baudFlushAt = millis() + BAUD_FLUSH_DELAY_MS;
    return true;
}

uint32_t RetroTink::getBaudRate() const {
    return _serial ? _serial->getBaudRate() : 0;
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
    // Commands come from the loop task and from web server tasks
    xSemaphoreTake(_rawMutex, portMAX_DELAY);
    _lastCommand = command;
    xSemaphoreGive(_rawMutex);

    if (_rawOpen) {
        // An app owns the serial link - hold the command until it lets go
        xSemaphoreTake(_rawMutex, portMAX_DELAY);
        if (_deferredCommands.size() >= DEFERRED_COMMANDS_MAX) {
            _deferredCommands.erase(_deferredCommands.begin());
        }
        _deferredCommands.push_back(command);
        xSemaphoreGive(_rawMutex);
        LOG_INFO("RetroTink: Serial link in use - command deferred: %s", command.c_str());
        return;
    }

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
    unsigned int unprintable = 0;
    for (unsigned int i = 0; i < line.length(); i++) {
        char c = line.charAt(i);
        if (c >= 0x20 && c < 0x7F) {
            clean += c;
        } else {
            clean += '?';
            unprintable++;
        }
    }

    // Mostly unprintable = binary data, e.g. the rest of a file transfer an
    // app abandoned. Nothing to parse, and logging it would flush the log.
    if (unprintable * 4 > line.length()) return;

    // Replies to our own status probes are housekeeping, not news
    bool probeReply = (long)(millis() - _probeQuietUntil) < 0 &&
                      (line.indexOf("FW Version:") >= 0 || line.indexOf("Build tag:") >= 0);
    if (!probeReply) {
        LOG_DEBUG("RetroTink RX: %s", clean.c_str());
    }

    if (_powerMgmtMode == PowerManagementMode::OFF) return;

    // --- Firmware 1.75+: every command gets a "[COM] " reply while the RT4K
    // is on, and a sleeping RT4K answers nothing but "pwr on" ---
    if (_powerMgmtMode == PowerManagementMode::FULL && line.startsWith("[COM] ")) {
        _comReplySeen = true;
        _probeSentAt = 0;

        if (line.indexOf("Power On Requested") >= 0) {
            // It was asleep. It starts answering again once it has booted,
            // which processPendingOperations() probes for.
            setPowerState(RT4KPowerState::BOOTING, "RT4K was asleep and is waking up");
            _bootWaitStart = millis();
            _bootProbing = true;
            _nextBootProbe = millis() + BOOT_PROBE_DELAY_MS;
            return;
        }

        if (line.endsWith("Serial Remote: pwr")) {
            // The power button toggles, but a sleeping RT4K wouldn't have answered: it was on
            setPowerState(RT4KPowerState::SLEEPING, "power toggled over serial");
            return;
        }

        // Anything else (including "Bad Command: pwr on", the reply to our
        // wake/query while it is already on) means the RT4K is up
        setPowerState(RT4KPowerState::ON, "RT4K is answering");
        _bootProbing = false;
        _bootWaitStart = 0;
        sendPendingCommand();
        return;
    }

    // --- Older firmware: unsolicited status text ---
    if (line.indexOf("Powering Up") >= 0) {
        if (_powerState != RT4KPowerState::BOOTING) {
            setPowerState(RT4KPowerState::BOOTING, "RT4K reports powering up");
            _bootWaitStart = millis();
        }
        return;
    }

    if (line.indexOf("Boot Sequence Complete") >= 0) {
        setPowerState(RT4KPowerState::ON, "RT4K reports boot complete");
        _bootProbing = false;
        _bootWaitStart = 0;
        sendPendingCommand();
        return;
    }

    if (line.indexOf("Power Off") >= 0 || line.indexOf("Entering Sleep") >= 0) {
        setPowerState(RT4KPowerState::SLEEPING, "RT4K reports power off");
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

bool RetroTink::rawOpen() {
    if (!_serial || _rawOpen) return false;

    xSemaphoreTake(_rawMutex, portMAX_DELAY);
    _rawTotal = 0;
    _rawLastRx = millis();
    bool ok = (_rawBuffer != nullptr);
    _rawOpen = ok;
    xSemaphoreGive(_rawMutex);

    if (ok) {
        LOG_DEBUG("RetroTink: Raw channel opened (%u byte capture window)", (unsigned)_rawCapacity);
    } else {
        LOG_ERROR("RetroTink: Raw channel - out of memory");
    }
    return ok;
}

void RetroTink::rawClose() {
    if (!_rawOpen) return;
    _rawOpen = false;
    LOG_DEBUG("RetroTink: Raw channel closed (%lu bytes captured)", (unsigned long)_rawTotal);
    // Held-back commands are sent by update() (loop task)
}

size_t RetroTink::rawWrite(const uint8_t* data, size_t length) {
    if (!_serial || !_rawOpen) return 0;
    return _serial->write(data, length);
}

size_t RetroTink::rawRead(uint32_t cursor, uint8_t* buf, size_t maxLen, bool& overflow, uint32_t& nextCursor) {
    overflow = false;
    nextCursor = cursor;
    if (!_rawBuffer) return 0;

    xSemaphoreTake(_rawMutex, portMAX_DELAY);
    uint32_t total = _rawTotal;
    uint32_t oldest = (total > _rawCapacity) ? total - _rawCapacity : 0;
    if (cursor < oldest) {
        overflow = true;
        cursor = oldest;
    }
    if (cursor > total) cursor = total;

    size_t count = total - cursor;
    if (count > maxLen) count = maxLen;
    for (size_t i = 0; i < count; i++) {
        buf[i] = _rawBuffer[(cursor + i) % _rawCapacity];
    }
    xSemaphoreGive(_rawMutex);

    nextCursor = cursor + count;
    return count;
}

void RetroTink::captureIncomingData() {
    uint8_t chunk[256];
    size_t n;
    while ((n = _serial->read(chunk, sizeof(chunk))) > 0) {
        xSemaphoreTake(_rawMutex, portMAX_DELAY);
        for (size_t i = 0; i < n; i++) {
            _rawBuffer[(_rawTotal + i) % _rawCapacity] = chunk[i];
        }
        _rawTotal += n;
        _rawLastRx = millis();
        xSemaphoreGive(_rawMutex);

        scanCapturedText(chunk, n);
    }
}

void RetroTink::scanCapturedText(const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        uint8_t c = data[i];
        if (c == '\n' || c == '\r') {
            if (_rawLineClean && _rawLineBuffer.length() > 0) {
                processReceivedLine(_rawLineBuffer);
            }
            _rawLineBuffer = "";
            _rawLineClean = true;
        } else if (c < 0x20 || c >= 0x7F || _rawLineBuffer.length() >= RAW_LINE_MAX) {
            // Binary data (or not a status line) - ignore up to the next terminator
            _rawLineClean = false;
            _rawLineBuffer = "";
        } else if (_rawLineClean) {
            _rawLineBuffer += (char)c;
        }
    }
}

void RetroTink::processPowerTracking(unsigned long now) {
    // The RT4K's serial output drops low when it powers down
    uint32_t breaks = _serial->getRxBreakCount();
    if (breaks != _lastBreakCount) {
        _lastBreakCount = breaks;
        if (_powerState == RT4KPowerState::ON || _powerState == RT4KPowerState::UNKNOWN) {
            setPowerState(RT4KPowerState::SLEEPING, "serial line dropped");
        }
    }

    if (!_serial->isConnected()) {
        if (_powerState != RT4KPowerState::UNKNOWN) setPowerState(RT4KPowerState::UNKNOWN, "serial link down");
        if (_pendingCommand.length() > 0) _wakePending = true;  // Retry when the link is back
        _wasConnected = false;
        _probeSentAt = 0;
        return;
    }
    if (!_wasConnected) {
        // Link just came up: find out where the RT4K stands
        _wasConnected = true;
        _nextIdleProbe = now + CONNECT_PROBE_DELAY_MS;
    }

    // An input change is waiting for the serial link (an app was using it)
    if (_wakePending) {
        startWake();
        return;
    }

    switch (_powerState) {
        case RT4KPowerState::WAKING:
            if (now - _bootWaitStart >= WAKE_RESPONSE_TIMEOUT_MS) {
                // No answer to "pwr on". Firmware before 1.75 never answers,
                // and would have printed "Powering Up" by now if it had been
                // asleep - so send the command. On newer firmware the RT4K
                // is unreachable and the command is a long shot either way.
                LOG_WARN("RetroTink: No reply to pwr on after %lu ms - sending command anyway",
                         WAKE_RESPONSE_TIMEOUT_MS);
                _bootWaitStart = 0;
                _powerState = _comReplySeen ? RT4KPowerState::UNKNOWN : RT4KPowerState::ON;
                sendPendingCommand();
            }
            break;

        case RT4KPowerState::BOOTING:
            if (now - _bootWaitStart >= BOOT_TIMEOUT_MS) {
                LOG_WARN("RetroTink: Boot timeout (%lu ms) - sending pending command anyway", BOOT_TIMEOUT_MS);
                _bootWaitStart = 0;
                _bootProbing = false;
                _powerState = RT4KPowerState::UNKNOWN;
                sendPendingCommand();
            } else if (_bootProbing && (long)(now - _nextBootProbe) >= 0) {
                // The RT4K ignores commands until it has booted; the first
                // reply marks the moment it accepts a profile command
                _nextBootProbe = now + BOOT_PROBE_INTERVAL_MS;
                sendProbe();
            }
            break;

        default:
            // Idle: keep the reported state honest. The RT4K can be switched
            // on or off with its remote or power button without telling us.
            if (_probeSentAt && now - _probeSentAt >= PROBE_REPLY_TIMEOUT_MS) {
                _probeSentAt = 0;
                if (_comReplySeen) setPowerState(RT4KPowerState::SLEEPING, "RT4K stopped answering");
            }
            if ((long)(now - _nextIdleProbe) >= 0) {
                _nextIdleProbe = now + IDLE_PROBE_INTERVAL_MS;
                sendProbe();
            }
            break;
    }
}

void RetroTink::sendProbe() {
    // Sent outside sendCommand(): probes are housekeeping, so they don't
    // show up as the last command and aren't logged
    if (_rawOpen || !_serial->isConnected()) return;
    if (_serial->sendData(String("\rver\r"))) {
        _probeSentAt = millis();
        _probeQuietUntil = _probeSentAt + PROBE_REPLY_TIMEOUT_MS;
    }
}

void RetroTink::processPendingOperations() {
    unsigned long now = millis();

    // Flush the RT4K's input after a baud rate change (see _baudFlushAt)
    if (_baudFlushAt && (long)(now - _baudFlushAt) >= 0) {
        _baudFlushAt = 0;
        if (_serial && _serial->isConnected()) {
            _serial->sendData("\r");
            LOG_DEBUG("RetroTink: Flushed RT4K input after baud rate change");
        }
    }

    if (_powerMgmtMode == PowerManagementMode::FULL && !_rawOpen) {
        processPowerTracking(now);
    }

    // SIMPLE mode: fixed wait after the one-time "pwr on"
    if (_powerMgmtMode == PowerManagementMode::SIMPLE && _powerState == RT4KPowerState::BOOTING &&
        _bootWaitStart > 0 && now - _bootWaitStart >= BOOT_TIMEOUT_MS) {
        _bootWaitStart = 0;
        _powerState = RT4KPowerState::ON;  // No serial feedback expected - assume it booted
        sendPendingCommand();
    }

    // Check for SVS keep-alive
    if (_svsKeepAlivePending && (now - _svsKeepAliveTime >= SVS_KEEPALIVE_DELAY_MS)) {
        String keepAlive = "SVS CURRENT INPUT=" + String(_lastSvsInput);
        sendCommand(keepAlive);
        LOG_DEBUG("RetroTink: SVS keep-alive sent: %s", keepAlive.c_str());
        _svsKeepAlivePending = false;
    }
}
