#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <functional>
#include <vector>

// WiFi connection manager
// Supports Station mode with AP fallback for configuration

class WifiManager {
public:
    enum class Mode {
        STA,        // Station mode only
        AP,         // Access Point mode only
        AP_STA      // Both modes simultaneously
    };

    enum class State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        FAILED,
        AP_ACTIVE   // Access Point is active
    };

    struct NetworkInfo {
        String ssid;
        int32_t rssi;
        uint8_t encryptionType;
    };

    struct APConfig {
        String ssid;           // Generated from MAC
        String password;       // Empty = open network
        IPAddress ip;          // AP IP address
        IPAddress gateway;     // Gateway address
        IPAddress subnet;      // Subnet mask
        IPAddress dhcpStart;   // DHCP range start
        IPAddress dhcpEnd;     // DHCP range end
    };

    using StateChangeCallback = std::function<void(State state)>;

    WifiManager();

    bool begin(const String& hostname = "tinklink");
    void end();

    // Connection management (Station mode)
    bool connect(const String& ssid, const String& password);
    void disconnect();

    // Access Point management
    bool startAccessPoint();
    void stopAccessPoint();
    bool isAPActive() const { return _state == State::AP_ACTIVE; }

    // Update - call from loop() to handle connection state
    void update();

    // Network scanning
    bool startScan();  // Start async scan, returns false if already scanning
    bool isScanComplete();  // Check if scan is done
    std::vector<NetworkInfo> getScanResults();  // Get results and clear scan

    // State
    State getState() const { return _state; }
    Mode getMode() const { return _mode; }
    bool isConnected() const { return _state == State::CONNECTED; }
    String getIP() const;
    String getSSID() const;
    int getRSSI() const;
    APConfig getAPConfig() const { return _apConfig; }

    // Callback
    void onStateChange(StateChangeCallback callback);

private:
    State _state;
    Mode _mode;
    String _hostname;
    String _ssid;
    String _password;
    unsigned long _connectStartTime;
    int _retryCount;
    unsigned long _retryDelayMs;
    unsigned long _lastRetryTime;
    unsigned long _lastDisconnectCheck;  // Track transient disconnects

    static const unsigned long CONNECT_TIMEOUT_MS = 15000;   // 15 seconds
    static const int MAX_RETRIES = 2;                        // 2 retries = 3 total attempts
    static const unsigned long BASE_RETRY_DELAY_MS = 5000;   // 5s, 10s delays
    static const unsigned long DISCONNECT_TOLERANCE_MS = 3000; // 3s tolerance for transient disconnects

    APConfig _apConfig;
    StateChangeCallback _stateCallback;

    void setState(State newState);
    void setupMDNS();
    void generateAPConfig();
    void handleRetryLogic();
    unsigned long getRetryDelay(int retryCount);
};

#endif // WIFI_MANAGER_H
