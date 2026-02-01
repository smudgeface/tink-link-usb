#include "wifi_manager.h"

WifiManager::WifiManager()
    : _state(State::DISCONNECTED)
    , _mode(Mode::STA)
    , _hostname("tinklink")
    , _connectStartTime(0)
    , _retryCount(0)
    , _retryDelayMs(0)
    , _lastRetryTime(0)
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

    Serial.printf("WifiManager: Initialized (hostname: %s)\n", _hostname.c_str());
    Serial.printf("WifiManager: AP SSID will be '%s' if needed\n", _apConfig.ssid.c_str());
    return true;
}

void WifiManager::end() {
    disconnect();
    WiFi.mode(WIFI_OFF);
}

bool WifiManager::connect(const String& ssid, const String& password) {
    if (ssid.length() == 0) {
        Serial.println("WifiManager: Cannot connect - no SSID provided");
        return false;
    }

    // If we're in AP mode, stop it and switch to STA
    if (_mode == Mode::AP) {
        stopAccessPoint();
    }

    _ssid = ssid;
    _password = password;

    Serial.printf("WifiManager: Connecting to '%s'...\n", ssid.c_str());

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
        Serial.println("WifiManager: Cannot disconnect in AP mode - use stopAccessPoint() instead");
        return;
    }

    WiFi.disconnect(true);
    setState(State::DISCONNECTED);
    Serial.println("WifiManager: Disconnected");
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
                Serial.printf("WifiManager: Connected to '%s' - IP: %s\n",
                              _ssid.c_str(), WiFi.localIP().toString().c_str());
            } else if (status == WL_CONNECT_FAILED ||
                       status == WL_NO_SSID_AVAIL ||
                       (millis() - _connectStartTime > CONNECT_TIMEOUT_MS)) {
                setState(State::FAILED);
                Serial.printf("WifiManager: Connection failed (status: %d)\n", status);
            }
            break;

        case State::CONNECTED:
            if (status != WL_CONNECTED) {
                setState(State::FAILED);  // Go to FAILED instead of DISCONNECTED to trigger retry
                Serial.println("WifiManager: Connection lost");
            }
            break;

        case State::DISCONNECTED:
            // Auto-reconnect is handled by WiFi library
            if (status == WL_CONNECTED) {
                setState(State::CONNECTED);
                setupMDNS();
                Serial.printf("WifiManager: Reconnected - IP: %s\n",
                              WiFi.localIP().toString().c_str());
            }
            break;

        case State::FAILED:
            // Handle retry logic with exponential backoff
            handleRetryLogic();
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
        Serial.println("WifiManager: Scan already in progress");
        return false;
    }

    // Delete old results if any
    if (status >= 0) {
        WiFi.scanDelete();
    }

    Serial.println("WifiManager: Starting async network scan...");
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
        Serial.printf("WifiManager: Found %d networks\n", n);
        for (int i = 0; i < n; i++) {
            NetworkInfo info;
            info.ssid = WiFi.SSID(i);
            info.rssi = WiFi.RSSI(i);
            info.encryptionType = WiFi.encryptionType(i);
            networks.push_back(info);
        }
        WiFi.scanDelete();
    } else if (n == WIFI_SCAN_FAILED) {
        Serial.println("WifiManager: Scan failed");
        WiFi.scanDelete();
    } else if (n == WIFI_SCAN_RUNNING) {
        Serial.println("WifiManager: Scan still running");
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
        Serial.printf("WifiManager: mDNS started - http://%s.local\n", _hostname.c_str());
    } else {
        Serial.println("WifiManager: mDNS setup failed");
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
    Serial.println("WifiManager: Starting Access Point...");

    // Stop any existing connection
    WiFi.disconnect(true);
    delay(100);

    // Configure AP
    WiFi.mode(WIFI_AP);
    _mode = Mode::AP;

    // Set static IP configuration
    if (!WiFi.softAPConfig(_apConfig.ip, _apConfig.gateway, _apConfig.subnet)) {
        Serial.println("WifiManager: Failed to configure AP IP");
        return false;
    }

    // Start AP (open network)
    if (!WiFi.softAP(_apConfig.ssid.c_str(), _apConfig.password.c_str())) {
        Serial.println("WifiManager: Failed to start AP");
        return false;
    }

    // Configure DHCP server
    // Note: ESP32 Arduino has DHCP server running by default for AP mode
    // The range is typically 192.168.4.2-192.168.4.255 but we use custom IP
    // DHCP server auto-configures based on the softAPConfig settings

    setState(State::AP_ACTIVE);

    Serial.println("========================================");
    Serial.println("  Access Point Active");
    Serial.println("========================================");
    Serial.printf("  SSID:     %s\n", _apConfig.ssid.c_str());
    Serial.printf("  IP:       %s\n", _apConfig.ip.toString().c_str());
    Serial.printf("  Security: Open (no password)\n");
    Serial.println();
    Serial.println("Connect to this network and browse to:");
    Serial.printf("  http://%s\n", _apConfig.ip.toString().c_str());
    Serial.println("========================================");

    return true;
}

void WifiManager::stopAccessPoint() {
    if (_mode == Mode::AP) {
        Serial.println("WifiManager: Stopping Access Point...");
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
        Serial.println("WifiManager: Max retries exceeded - falling back to AP mode");
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
        Serial.printf("WifiManager: Will retry in %lu seconds (attempt %d/%d)\n",
                      _retryDelayMs / 1000, _retryCount, MAX_RETRIES);
    } else if (currentTime - _lastRetryTime >= _retryDelayMs) {
        // Time to retry
        Serial.printf("WifiManager: Retrying connection (attempt %d/%d)...\n",
                      _retryCount, MAX_RETRIES);
        _retryDelayMs = 0;  // Reset delay
        connect(_ssid, _password);  // Retry connection
    }
}
