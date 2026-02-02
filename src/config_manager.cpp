#include "config_manager.h"
#include "logger.h"
#include <LittleFS.h>

ConfigManager::ConfigManager() {
    // Default pin assignments for ESP32-S3-Zero
    // Extron uses UART0 on GPIO43 (TX) / GPIO44 (RX)
    _extronConfig.txPin = 43;
    _extronConfig.rxPin = 44;
    _wifiConfig.hostname = "tinklink";
}

bool ConfigManager::begin() {
    if (!LittleFS.begin(true)) {  // true = format if mount fails
        LOG_ERROR("ConfigManager: Failed to mount LittleFS");
        return false;
    }

    LOG_DEBUG("ConfigManager: LittleFS mounted");

    // Load configurations
    loadConfig();
    loadWifiConfig();

    return true;
}

bool ConfigManager::loadConfig() {
    File file = LittleFS.open(CONFIG_PATH, "r");
    if (!file) {
        LOG_WARN("ConfigManager: No config.json found, using defaults");
        return loadDefaultConfig();
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        LOG_ERROR("ConfigManager: Failed to parse config.json: %s", error.c_str());
        return loadDefaultConfig();
    }

    // Parse extron config
    if (doc["extron"].is<JsonObject>()) {
        _extronConfig.txPin = doc["extron"]["txPin"] | 43;
        _extronConfig.rxPin = doc["extron"]["rxPin"] | 44;
    }

    // Parse hostname (from root or wirelessClient for backwards compatibility)
    if (doc["hostname"].is<const char*>()) {
        _wifiConfig.hostname = doc["hostname"].as<String>();
    } else if (doc["wirelessClient"]["hostname"].is<const char*>()) {
        _wifiConfig.hostname = doc["wirelessClient"]["hostname"].as<String>();
    }

    // Parse triggers
    _triggers.clear();
    if (doc["triggers"].is<JsonArray>()) {
        for (JsonObject triggerObj : doc["triggers"].as<JsonArray>()) {
            TriggerMapping trigger;
            trigger.extronInput = triggerObj["input"] | 0;
            trigger.profile = triggerObj["profile"] | 0;
            trigger.name = triggerObj["name"] | "";
            trigger.mode = parseProfileMode(triggerObj["mode"] | "SVS");

            if (trigger.extronInput > 0 && trigger.profile > 0) {
                _triggers.push_back(trigger);
            }
        }
    }

    LOG_DEBUG("ConfigManager: Loaded %d triggers from config", _triggers.size());
    return true;
}

bool ConfigManager::loadDefaultConfig() {
    // Set reasonable defaults
    _triggers.clear();

    // Add a couple of example triggers
    TriggerMapping trigger1 = {1, TriggerMapping::SVS, 1, "Input 1"};
    TriggerMapping trigger2 = {2, TriggerMapping::SVS, 2, "Input 2"};
    _triggers.push_back(trigger1);
    _triggers.push_back(trigger2);

    return true;
}

bool ConfigManager::saveConfig() {
    JsonDocument doc;

    // Extron config
    doc["extron"]["txPin"] = _extronConfig.txPin;
    doc["extron"]["rxPin"] = _extronConfig.rxPin;

    // Hostname
    doc["hostname"] = _wifiConfig.hostname;

    // Triggers
    JsonArray triggersArray = doc["triggers"].to<JsonArray>();
    for (const auto& trigger : _triggers) {
        JsonObject triggerObj = triggersArray.add<JsonObject>();
        triggerObj["input"] = trigger.extronInput;
        triggerObj["mode"] = profileModeToString(trigger.mode);
        triggerObj["profile"] = trigger.profile;
        triggerObj["name"] = trigger.name;
    }

    File file = LittleFS.open(CONFIG_PATH, "w");
    if (!file) {
        LOG_ERROR("ConfigManager: Failed to open config.json for writing");
        return false;
    }

    serializeJsonPretty(doc, file);
    file.close();

    LOG_INFO("ConfigManager: Configuration saved");
    return true;
}

bool ConfigManager::loadWifiConfig() {
    File file = LittleFS.open(WIFI_CONFIG_PATH, "r");
    if (!file) {
        LOG_DEBUG("ConfigManager: No wifi.json found");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        LOG_ERROR("ConfigManager: Failed to parse wifi.json: %s", error.c_str());
        return false;
    }

    _wifiConfig.ssid = doc["ssid"] | "";
    _wifiConfig.password = doc["password"] | "";

    if (doc["hostname"].is<const char*>()) {
        _wifiConfig.hostname = doc["hostname"].as<String>();
    }

    LOG_DEBUG("ConfigManager: WiFi config loaded (SSID: %s)",
              _wifiConfig.ssid.length() > 0 ? _wifiConfig.ssid.c_str() : "(none)");
    return true;
}

bool ConfigManager::saveWifiConfig() {
    JsonDocument doc;

    doc["ssid"] = _wifiConfig.ssid;
    doc["password"] = _wifiConfig.password;
    doc["hostname"] = _wifiConfig.hostname;

    File file = LittleFS.open(WIFI_CONFIG_PATH, "w");
    if (!file) {
        LOG_ERROR("ConfigManager: Failed to open wifi.json for writing");
        return false;
    }

    serializeJsonPretty(doc, file);
    file.close();

    LOG_INFO("ConfigManager: WiFi configuration saved");
    return true;
}

void ConfigManager::setWifiCredentials(const String& ssid, const String& password) {
    _wifiConfig.ssid = ssid;
    _wifiConfig.password = password;
}

void ConfigManager::setHostname(const String& hostname) {
    _wifiConfig.hostname = hostname;
}

void ConfigManager::setTriggers(const std::vector<TriggerMapping>& triggers) {
    _triggers = triggers;
}

bool ConfigManager::hasWifiCredentials() const {
    return _wifiConfig.ssid.length() > 0;
}

TriggerMapping::Mode ConfigManager::parseProfileMode(const char* mode) {
    if (mode && (strcasecmp(mode, "Remote") == 0 || strcasecmp(mode, "REMOTE") == 0)) {
        return TriggerMapping::REMOTE;
    }
    return TriggerMapping::SVS;
}

const char* ConfigManager::profileModeToString(TriggerMapping::Mode mode) {
    switch (mode) {
        case TriggerMapping::REMOTE:
            return "Remote";
        case TriggerMapping::SVS:
        default:
            return "SVS";
    }
}
