#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <functional>
#include <vector>

/**
 * WiFi connection manager with Access Point fallback.
 *
 * Manages WiFi connectivity in two modes:
 * - Station (STA): Connects to existing network
 * - Access Point (AP): Creates hotspot for configuration
 *
 * Features:
 * - Automatic retry with exponential backoff on connection failure
 * - Automatic fallback to AP mode after max retries
 * - Transient disconnect tolerance (3s debounce)
 * - mDNS advertisement for easy discovery
 * - Async network scanning
 *
 * Usage:
 *   WifiManager wifi;
 *   wifi.begin("tinklink");
 *   wifi.connect("MyNetwork", "password");
 *   // In loop():
 *   wifi.update();  // Must be called regularly
 */
class WifiManager {
public:
    /** WiFi operating mode */
    enum class Mode {
        STA,     ///< Station mode - connects to existing network
        AP,      ///< Access Point mode - creates hotspot
        AP_STA   ///< Both modes simultaneously (not currently used)
    };

    /** Connection state machine states */
    enum class State {
        DISCONNECTED,  ///< Not connected, not trying
        CONNECTING,    ///< Connection attempt in progress
        CONNECTED,     ///< Successfully connected to network
        FAILED,        ///< Connection failed, may retry
        AP_ACTIVE      ///< Access Point is active
    };

    /** Information about a discovered WiFi network */
    struct NetworkInfo {
        String ssid;              ///< Network name
        int32_t rssi;             ///< Signal strength in dBm
        uint8_t encryptionType;   ///< wifi_auth_mode_t value
    };

    /** Access Point configuration */
    struct APConfig {
        String ssid;           ///< AP network name (generated from MAC)
        String password;       ///< AP password (empty = open network)
        IPAddress ip;          ///< AP IP address (192.168.1.1)
        IPAddress gateway;     ///< Gateway address
        IPAddress subnet;      ///< Subnet mask
        IPAddress dhcpStart;   ///< DHCP range start
        IPAddress dhcpEnd;     ///< DHCP range end
    };

    /** Callback type for state change notifications */
    using StateChangeCallback = std::function<void(State state)>;

    WifiManager();

    /**
     * Initialize WiFi subsystem.
     * @param hostname mDNS hostname (without .local suffix)
     * @return true on success
     */
    bool begin(const String& hostname = "tinklink");

    /** Shutdown WiFi and release resources. */
    void end();

    /**
     * Connect to a WiFi network (Station mode).
     * Stops AP mode if active. Non-blocking - use update() to monitor.
     * @param ssid Network name
     * @param password Network password
     * @return true if connection attempt started
     */
    bool connect(const String& ssid, const String& password);

    /**
     * Disconnect from current network.
     * Does nothing in AP mode.
     */
    void disconnect();

    /**
     * Start Access Point mode for configuration.
     * Creates an open hotspot with DHCP server.
     * @return true if AP started successfully
     */
    bool startAccessPoint();

    /** Stop Access Point and return to Station mode. */
    void stopAccessPoint();

    /** @return true if Access Point is currently active */
    bool isAPActive() const { return _state == State::AP_ACTIVE; }

    /**
     * Process WiFi state machine. Must be called regularly from loop().
     * Handles connection monitoring, retry logic, and state transitions.
     */
    void update();

    /**
     * Start asynchronous network scan.
     * @return false if scan already in progress
     */
    bool startScan();

    /**
     * Check if network scan has completed.
     * @return true if scan is done (or failed)
     */
    bool isScanComplete();

    /**
     * Get scan results and clear scan state.
     * @return Vector of discovered networks, empty if scan not complete
     */
    std::vector<NetworkInfo> getScanResults();

    /** @return Current connection state */
    State getState() const { return _state; }

    /** @return Current operating mode */
    Mode getMode() const { return _mode; }

    /** @return true if connected to a network (STA mode) */
    bool isConnected() const { return _state == State::CONNECTED; }

    /**
     * Get current IP address.
     * @return IP as string, or empty if not connected
     */
    String getIP() const;

    /**
     * Get current SSID.
     * @return Connected network SSID, AP SSID, or saved SSID
     */
    String getSSID() const;

    /**
     * Get signal strength of current connection.
     * @return RSSI in dBm, or 0 if not connected
     */
    int getRSSI() const;

    /** @return Current Access Point configuration */
    APConfig getAPConfig() const { return _apConfig; }

    /**
     * Register callback for state changes.
     * @param callback Function called when state changes
     */
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
