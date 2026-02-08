#include "ConfigManager.h"
#include "Logger.h"
#include <LittleFS.h>

ConfigManager::ConfigManager() {
    // Default pin assignments for ESP32-S3-Zero (Waveshare)
    // Switcher uses UART1 on GPIO43 (TX) / GPIO44 (RX)
    _switcherType = "Extron SW VGA";
    _switcherConfigDoc["type"] = "Extron SW VGA";
    _switcherConfigDoc["uartId"] = 1;
    _switcherConfigDoc["txPin"] = 43;
    _switcherConfigDoc["rxPin"] = 44;
    _switcherConfigDoc["autoSwitch"] = true;

    // Hardware pins
    _hardwareConfig.ledPin = 21;          // WS2812 LED data pin
    _hardwareConfig.ledColorOrder = "GRB"; // Most WS2812 LEDs use GRB

    // AVR defaults
    _avrConfigDoc["type"] = "Denon X4300H";
    _avrConfigDoc["enabled"] = false;
    _avrConfigDoc["ip"] = "";
    _avrConfigDoc["input"] = "GAME";

    // RetroTink defaults
    // Default to USB mode with full power management
    _retrotinkConfigDoc["serialMode"] = "usb";
    _retrotinkConfigDoc["powerManagementMode"] = "full";
    _retrotinkConfigDoc["uartId"] = 2;
    _retrotinkConfigDoc["txPin"] = 17;
    _retrotinkConfigDoc["rxPin"] = 18;

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

    // Parse switcher config (store raw JSON)
    if (doc["switcher"].is<JsonObject>()) {
        _switcherType = doc["switcher"]["type"] | "Extron SW VGA";
        _switcherConfigDoc.clear();
        _switcherConfigDoc.set(doc["switcher"]);
    }

    // Parse hardware config
    if (doc["hardware"].is<JsonObject>()) {
        _hardwareConfig.ledPin = doc["hardware"]["ledPin"] | 21;
        _hardwareConfig.ledColorOrder = doc["hardware"]["ledColorOrder"] | "GRB";
    }

    // Parse AVR config (store raw JSON)
    if (doc["avr"].is<JsonObject>()) {
        _avrConfigDoc.clear();
        _avrConfigDoc.set(doc["avr"]);
    }

    // Parse RetroTink config (store raw JSON)
    if (doc["tink"].is<JsonObject>()) {
        _retrotinkConfigDoc.clear();
        _retrotinkConfigDoc.set(doc["tink"]);
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
            trigger.switcherInput = triggerObj["input"] | 0;
            trigger.profile = triggerObj["profile"] | 0;
            trigger.name = triggerObj["name"] | "";
            trigger.mode = parseProfileMode(triggerObj["mode"] | "SVS");

            if (trigger.switcherInput > 0 && trigger.profile > 0) {
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
    _triggers.push_back({1, TriggerMapping::SVS, 1, "Input 1"});
    _triggers.push_back({2, TriggerMapping::SVS, 2, "Input 2"});

    return true;
}

bool ConfigManager::saveConfig() {
    JsonDocument doc;

    // Switcher config (write raw JSON)
    doc["switcher"].set(_switcherConfigDoc);

    // Hardware config
    doc["hardware"]["ledPin"] = _hardwareConfig.ledPin;
    doc["hardware"]["ledColorOrder"] = _hardwareConfig.ledColorOrder;

    // AVR config (write raw JSON)
    doc["avr"].set(_avrConfigDoc);

    // RetroTink config (write raw JSON if not empty)
    if (!_retrotinkConfigDoc.isNull()) {
        doc["tink"].set(_retrotinkConfigDoc);
    }

    // Hostname
    doc["hostname"] = _wifiConfig.hostname;

    // Triggers
    JsonArray triggersArray = doc["triggers"].to<JsonArray>();
    for (const auto& trigger : _triggers) {
        JsonObject triggerObj = triggersArray.add<JsonObject>();
        triggerObj["input"] = trigger.switcherInput;
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

void ConfigManager::setAvrConfig(const JsonObject& config) {
    _avrConfigDoc.clear();
    _avrConfigDoc.set(config);
}

JsonObject ConfigManager::getSwitcherConfig() {
    return _switcherConfigDoc.as<JsonObject>();
}

JsonObject ConfigManager::getAvrConfig() {
    return _avrConfigDoc.as<JsonObject>();
}

bool ConfigManager::isAvrEnabled() const {
    return _avrConfigDoc["enabled"] | false;
}

JsonObject ConfigManager::getRetroTinkConfig() {
    return _retrotinkConfigDoc.as<JsonObject>();
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
