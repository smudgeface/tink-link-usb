#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "retrotink.h"

// Configuration paths
#define CONFIG_PATH "/config.json"
#define WIFI_CONFIG_PATH "/wifi.json"

// Configuration manager using LittleFS
class ConfigManager {
public:
    struct WifiConfig {
        String ssid;
        String password;
        String hostname;
    };

    struct ExtronConfig {
        uint8_t txPin;
        uint8_t rxPin;
    };

    ConfigManager();

    bool begin();

    // Load/save main configuration
    bool loadConfig();
    bool saveConfig();

    // Load/save WiFi credentials separately
    bool loadWifiConfig();
    bool saveWifiConfig();

    // Getters
    const WifiConfig& getWifiConfig() const { return _wifiConfig; }
    const ExtronConfig& getExtronConfig() const { return _extronConfig; }
    const std::vector<TriggerMapping>& getTriggers() const { return _triggers; }

    // Setters
    void setWifiCredentials(const String& ssid, const String& password);
    void setHostname(const String& hostname);
    void setTriggers(const std::vector<TriggerMapping>& triggers);

    // Check if WiFi credentials are configured
    bool hasWifiCredentials() const;

private:
    WifiConfig _wifiConfig;
    ExtronConfig _extronConfig;
    std::vector<TriggerMapping> _triggers;

    bool loadDefaultConfig();
    TriggerMapping::Mode parseProfileMode(const char* mode);
    const char* profileModeToString(TriggerMapping::Mode mode);
};

#endif // CONFIG_MANAGER_H
