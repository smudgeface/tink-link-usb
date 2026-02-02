#include "wifi_manager.h"
#include "logger.h"

WifiManager::WifiManager()
    : _state(State::DISCONNECTED)
    , _mode(Mode::STA)
    , _hostname("tinklink")
    , _connectStartTime(0)
    , _retryCount(0)
    , _retryDelayMs(0)
    , _lastRetryTime(0)
    , _lastDisconnectCheck(0)
    , _stateCallback(nullptr)
{
    generateAPConfig();
}

bool WifiManager::begin(const String& hostname) {
    _hostname = hostname;
    generateAPConfig();

    // Set WiFi mode to station initially
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    LOG_DEBUG("WifiManager: Initialized (hostname: %s)", _hostname.c_str());
    LOG_DEBUG("WifiManager: AP SSID will be '%s' if needed", _apConfig.ssid.c_str());
    return true;
}

void WifiManager::end() {
    disconnect();
    WiFi.mode(WIFI_OFF);
}

bool WifiManager::connect(const String& ssid, const String& password) {
    if (ssid.length() == 0) {
        LOG_WARN("WifiManager: Cannot connect - no SSID provided");
        return false;
    }

    // If we're in AP mode, stop it and switch to STA
    if (_mode == Mode::AP) {
        stopAccessPoint();
    }

    _ssid = ssid;
    _password = password;

    LOG_INFO("WifiManager: Connecting to '%s'...", ssid.c_str());

    WiFi.disconnect(true);
    delay(100);

    // Ensure we're in STA mode
    WiFi.mode(WIFI_STA);
    _mode = Mode::STA;

    WiFi.begin(ssid.c_str(), password.c_str());

    _connectStartTime = millis();
    setState(State::CONNECTING);

    return true;
}

void WifiManager::disconnect() {
    // Don't allow disconnect in AP mode - doesn't make sense
    if (_mode == Mode::AP) {
        LOG_WARN("WifiManager: Cannot disconnect in AP mode - use stopAccessPoint() instead");
        return;
    }

    WiFi.disconnect(true);
    setState(State::DISCONNECTED);
    LOG_INFO("WifiManager: Disconnected");
}

void WifiManager::update() {
    wl_status_t status = WiFi.status();

    switch (_state) {
        case State::CONNECTING:
            if (status == WL_CONNECTED) {
                setState(State::CONNECTED);
                _retryCount = 0;  // Reset retry counter on success
                _retryDelayMs = 0;
                setupMDNS();
                LOG_INFO("WifiManager: Connected to '%s' - IP: %s",
                         _ssid.c_str(), WiFi.localIP().toString().c_str());
            } else if (status == WL_CONNECT_FAILED ||
                       status == WL_NO_SSID_AVAIL ||
                       (millis() - _connectStartTime > CONNECT_TIMEOUT_MS)) {
                setState(State::FAILED);
                LOG_WARN("WifiManager: Connection failed (status: %d)", status);
            }
            break;

        case State::CONNECTED:
            if (status != WL_CONNECTED) {
                // Use debouncing to avoid flapping on transient disconnects
                if (_lastDisconnectCheck == 0) {
                    // First time seeing disconnect - start timer
                    _lastDisconnectCheck = millis();
                    LOG_DEBUG("WifiManager: Disconnect detected (status: %d), waiting %lums to confirm",
                              status, DISCONNECT_TOLERANCE_MS);
                } else if (millis() - _lastDisconnectCheck >= DISCONNECT_TOLERANCE_MS) {
                    // Disconnect persisted for tolerance period - actually disconnected
                    setState(State::FAILED);  // Go to FAILED instead of DISCONNECTED to trigger retry
                    LOG_WARN("WifiManager: Connection lost (confirmed)");
                    _lastDisconnectCheck = 0;
                }
            } else {
                // WiFi is connected - reset disconnect timer
                _lastDisconnectCheck = 0;
            }
            break;

        case State::DISCONNECTED:
            // Auto-reconnect is handled by WiFi library
            if (status == WL_CONNECTED) {
                setState(State::CONNECTED);
                setupMDNS();
                LOG_INFO("WifiManager: Reconnected - IP: %s",
                         WiFi.localIP().toString().c_str());
            }
            break;

        case State::FAILED:
            // First check if we're actually connected (WiFi might have recovered)
            if (status == WL_CONNECTED) {
                setState(State::CONNECTED);
                _retryCount = 0;  // Reset retry counter
                _retryDelayMs = 0;
                setupMDNS();
                LOG_INFO("WifiManager: Connection recovered - IP: %s",
                         WiFi.localIP().toString().c_str());
            } else {
                // Handle retry logic with exponential backoff
                handleRetryLogic();
            }
            break;

        case State::AP_ACTIVE:
            // Nothing to do in AP mode, web interface handles everything
            break;
    }
}

bool WifiManager::startScan() {
    // Check if already scanning
    int16_t status = WiFi.scanComplete();
    if (status == WIFI_SCAN_RUNNING) {
        LOG_DEBUG("WifiManager: Scan already in progress");
        return false;
    }

    // Delete old results if any
    if (status >= 0) {
        WiFi.scanDelete();
    }

    LOG_DEBUG("WifiManager: Starting async network scan...");
    WiFi.scanNetworks(true, false);  // async=true, show_hidden=false
    return true;
}

bool WifiManager::isScanComplete() {
    int16_t status = WiFi.scanComplete();
    return (status != WIFI_SCAN_RUNNING);
}

std::vector<WifiManager::NetworkInfo> WifiManager::getScanResults() {
    std::vector<NetworkInfo> networks;

    int n = WiFi.scanComplete();

    if (n >= 0) {
        LOG_DEBUG("WifiManager: Found %d networks", n);
        for (int i = 0; i < n; i++) {
            NetworkInfo info;
            info.ssid = WiFi.SSID(i);
            info.rssi = WiFi.RSSI(i);
            info.encryptionType = WiFi.encryptionType(i);
            networks.push_back(info);
        }
        WiFi.scanDelete();
    } else if (n == WIFI_SCAN_FAILED) {
        LOG_WARN("WifiManager: Scan failed");
        WiFi.scanDelete();
    } else if (n == WIFI_SCAN_RUNNING) {
        LOG_DEBUG("WifiManager: Scan still running");
    }

    return networks;
}

String WifiManager::getIP() const {
    if (_state == State::CONNECTED) {
        return WiFi.localIP().toString();
    } else if (_state == State::AP_ACTIVE) {
        return WiFi.softAPIP().toString();
    }
    return "";
}

String WifiManager::getSSID() const {
    if (_state == State::CONNECTED) {
        return WiFi.SSID();
    } else if (_state == State::AP_ACTIVE) {
        return _apConfig.ssid;
    }
    return _ssid;
}

int WifiManager::getRSSI() const {
    if (_state == State::CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}

void WifiManager::onStateChange(StateChangeCallback callback) {
    _stateCallback = callback;
}

void WifiManager::setState(State newState) {
    if (_state != newState) {
        _state = newState;
        if (_stateCallback) {
            _stateCallback(newState);
        }
    }
}

void WifiManager::setupMDNS() {
    if (MDNS.begin(_hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        LOG_INFO("WifiManager: mDNS started - http://%s.local", _hostname.c_str());
    } else {
        LOG_ERROR("WifiManager: mDNS setup failed");
    }
}

void WifiManager::generateAPConfig() {
    // Get MAC address and generate unique SSID
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macStr[7];
    snprintf(macStr, sizeof(macStr), "%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    _apConfig.ssid = "TinkLink-" + String(macStr);
    _apConfig.password = "";  // Open network
    _apConfig.ip = IPAddress(192, 168, 1, 1);
    _apConfig.gateway = IPAddress(192, 168, 1, 1);
    _apConfig.subnet = IPAddress(255, 255, 255, 0);
    _apConfig.dhcpStart = IPAddress(192, 168, 1, 100);
    _apConfig.dhcpEnd = IPAddress(192, 168, 1, 200);
}

bool WifiManager::startAccessPoint() {
    LOG_INFO("WifiManager: Starting Access Point...");

    // Stop any existing connection
    WiFi.disconnect(true);
    delay(100);

    // Configure AP
    WiFi.mode(WIFI_AP);
    _mode = Mode::AP;

    // Set static IP configuration
    if (!WiFi.softAPConfig(_apConfig.ip, _apConfig.gateway, _apConfig.subnet)) {
        LOG_ERROR("WifiManager: Failed to configure AP IP");
        return false;
    }

    // Start AP (open network)
    if (!WiFi.softAP(_apConfig.ssid.c_str(), _apConfig.password.c_str())) {
        LOG_ERROR("WifiManager: Failed to start AP");
        return false;
    }

    // Configure DHCP server
    // Note: ESP32 Arduino has DHCP server running by default for AP mode
    // The range is typically 192.168.4.2-192.168.4.255 but we use custom IP
    // DHCP server auto-configures based on the softAPConfig settings

    setState(State::AP_ACTIVE);

    // Enable mDNS in AP mode so tinklink.local works
    setupMDNS();

    LOG_RAW("========================================\n");
    LOG_RAW("  Access Point Active\n");
    LOG_RAW("========================================\n");
    LOG_INFO("  SSID:     %s", _apConfig.ssid.c_str());
    LOG_INFO("  IP:       %s", _apConfig.ip.toString().c_str());
    LOG_INFO("  URL:      http://%s.local or http://%s", _hostname.c_str(), _apConfig.ip.toString().c_str());
    LOG_INFO("  Security: Open (no password)");
    LOG_RAW("========================================\n");

    return true;
}

void WifiManager::stopAccessPoint() {
    if (_mode == Mode::AP) {
        LOG_INFO("WifiManager: Stopping Access Point...");
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        _mode = Mode::STA;
        _retryCount = 0;  // Reset retry counter for fresh STA connection
        _retryDelayMs = 0;
        setState(State::DISCONNECTED);
    }
}

unsigned long WifiManager::getRetryDelay(int retryCount) {
    // Exponential backoff: 30s, 60s, 120s
    return BASE_RETRY_DELAY_MS * (1 << retryCount);
}

void WifiManager::handleRetryLogic() {
    wl_status_t status = WiFi.status();

    // Only handle retry logic if we're in failed state
    if (_state != State::FAILED) {
        return;
    }

    // Check if we've exceeded max retries
    if (_retryCount >= MAX_RETRIES) {
        LOG_WARN("WifiManager: Max retries exceeded - falling back to AP mode");
        _retryCount = 0;  // Reset for next time
        startAccessPoint();
        return;
    }

    // Check if it's time to retry
    unsigned long currentTime = millis();
    if (_retryDelayMs == 0) {
        // First failure - calculate delay
        _retryDelayMs = getRetryDelay(_retryCount);
        _lastRetryTime = currentTime;
        _retryCount++;
        LOG_INFO("WifiManager: Will retry in %lu seconds (attempt %d/%d)",
                 _retryDelayMs / 1000, _retryCount, MAX_RETRIES);
    } else if (currentTime - _lastRetryTime >= _retryDelayMs) {
        // Time to retry
        LOG_INFO("WifiManager: Retrying connection (attempt %d/%d)...",
                 _retryCount, MAX_RETRIES);
        _retryDelayMs = 0;  // Reset delay
        connect(_ssid, _password);  // Retry connection
    }
}
