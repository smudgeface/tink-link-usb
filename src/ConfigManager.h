#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "RetroTink.h"

/// Path to main configuration file in LittleFS
#define CONFIG_PATH "/config.json"
/// Path to WiFi credentials file in LittleFS
#define WIFI_CONFIG_PATH "/wifi.json"

/**
 * Manages persistent configuration stored in LittleFS.
 *
 * Configuration is split into two files:
 * - config.json: Hardware settings, triggers, hostname
 * - wifi.json: WiFi credentials (separate for easier clearing)
 *
 * Usage:
 *   ConfigManager config;
 *   config.begin();  // Mounts LittleFS and loads config
 *   auto wifi = config.getWifiConfig();
 */
class ConfigManager {
public:
    /**
     * WiFi connection and network settings.
     */
    struct WifiConfig {
        String ssid;       ///< Network SSID to connect to
        String password;   ///< Network password
        String hostname;   ///< mDNS hostname (default: "tinklink")
    };

    /**
     * Video switcher UART pin configuration.
     */
    struct SwitcherConfig {
        String type;    ///< Switcher type (e.g., "Extron SW VGA")
        uint8_t txPin;  ///< UART TX pin (default: GPIO43)
        uint8_t rxPin;  ///< UART RX pin (default: GPIO44)
    };

    /**
     * Hardware pin configuration.
     */
    struct HardwareConfig {
        uint8_t ledPin;  ///< WS2812 RGB LED pin (default: GPIO21)
    };

    ConfigManager();

    /**
     * Initialize LittleFS and load configuration files.
     * Creates default config if files don't exist.
     * @return true if LittleFS mounted successfully
     */
    bool begin();

    /**
     * Load main configuration from CONFIG_PATH.
     * @return true if loaded successfully, false uses defaults
     */
    bool loadConfig();

    /**
     * Save main configuration to CONFIG_PATH.
     * @return true if saved successfully
     */
    bool saveConfig();

    /**
     * Load WiFi credentials from WIFI_CONFIG_PATH.
     * @return true if loaded successfully
     */
    bool loadWifiConfig();

    /**
     * Save WiFi credentials to WIFI_CONFIG_PATH.
     * @return true if saved successfully
     */
    bool saveWifiConfig();

    /** @return Current WiFi configuration */
    const WifiConfig& getWifiConfig() const { return _wifiConfig; }

    /** @return Current switcher pin configuration */
    const SwitcherConfig& getSwitcherConfig() const { return _switcherConfig; }

    /** @return Current hardware pin configuration */
    const HardwareConfig& getHardwareConfig() const { return _hardwareConfig; }

    /** @return List of configured switcher input to RetroTINK profile triggers */
    const std::vector<TriggerMapping>& getTriggers() const { return _triggers; }

    /**
     * Set WiFi credentials (not saved until saveWifiConfig() called).
     * @param ssid Network SSID
     * @param password Network password
     */
    void setWifiCredentials(const String& ssid, const String& password);

    /**
     * Set mDNS hostname (not saved until saveConfig() called).
     * @param hostname The hostname without .local suffix
     */
    void setHostname(const String& hostname);

    /**
     * Set trigger mappings (not saved until saveConfig() called).
     * @param triggers List of input-to-profile mappings
     */
    void setTriggers(const std::vector<TriggerMapping>& triggers);

    /**
     * Check if WiFi credentials have been configured.
     * @return true if SSID is non-empty
     */
    bool hasWifiCredentials() const;

private:
    WifiConfig _wifiConfig;
    SwitcherConfig _switcherConfig;
    HardwareConfig _hardwareConfig;
    std::vector<TriggerMapping> _triggers;

    bool loadDefaultConfig();
    TriggerMapping::Mode parseProfileMode(const char* mode);
    const char* profileModeToString(TriggerMapping::Mode mode);
};

#endif // CONFIG_MANAGER_H
