#include "WebServer.h"
#include "WifiManager.h"
#include "ConfigManager.h"
#include "Switcher.h"
#include "RetroTink.h"
#include "DenonAvr.h"
#include "Logger.h"
#include "version.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>

WebServer::WebServer(uint16_t port)
    : _server(new AsyncWebServer(port))
    , _wifi(nullptr)
    , _config(nullptr)
    , _switcher(nullptr)
    , _tink(nullptr)
    , _avr(nullptr)
    , _otaMode(OTAMode::FIRMWARE)
    , _otaProgress(0)
    , _otaTotal(0)
    , _otaInProgress(false)
    , _otaError("")
{
}

WebServer::~WebServer() {
    delete _server;
}

void WebServer::begin(WifiManager* wifi, ConfigManager* config, Switcher* switcher, RetroTink* tink, DenonAvr* avr) {
    _wifi = wifi;
    _config = config;
    _switcher = switcher;
    _tink = tink;
    _avr = avr;

    setupRoutes();
    _server->begin();

    LOG_INFO("WebServer: Started on port 80");
}

void WebServer::end() {
    _server->end();
}

void WebServer::setLEDCallback(LEDControlCallback callback) {
    _ledCallback = callback;
}

void WebServer::setupRoutes() {
    // API endpoints - register these BEFORE serveStatic to ensure they're matched first
    _server->on("/api/status", HTTP_GET,
        [this](AsyncWebServerRequest* request) { handleApiStatus(request); });

    // WiFi endpoints
    _server->on("/api/wifi/scan", HTTP_GET,
        [this](AsyncWebServerRequest* request) { handleApiScan(request); });

    _server->on("/api/wifi/connect", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiConnect(request); });

    _server->on("/api/wifi/disconnect", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiDisconnect(request); });

    _server->on("/api/wifi/save", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiSave(request); });

    // Configuration endpoints
    _server->on("/api/config/triggers", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiConfigTriggers(request); });

    // RetroTINK endpoints
    _server->on("/api/tink/send", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiTinkSend(request); });

    // Debug endpoints
    _server->on("/api/debug/led", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiDebugLED(request); });

    // Switcher endpoints
    _server->on("/api/switcher/send", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiSwitcherSend(request); });

    _server->on("/api/switcher/receive", HTTP_GET,
        [this](AsyncWebServerRequest* request) { handleApiSwitcherReceive(request); });

    // AVR endpoints
    _server->on("/api/avr/discover", HTTP_GET,
        [this](AsyncWebServerRequest* request) { handleApiAvrDiscover(request); });

    _server->on("/api/avr/send", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiAvrSend(request); });

    _server->on("/api/config/avr", HTTP_GET,
        [this](AsyncWebServerRequest* request) { handleApiConfigAvrGet(request); });

    _server->on("/api/config/avr", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiConfigAvr(request); });

    // System logs endpoint
    _server->on("/api/logs", HTTP_GET,
        [this](AsyncWebServerRequest* request) { handleApiLogs(request); });

    // OTA update endpoints
    _server->on("/api/ota/status", HTTP_GET,
        [this](AsyncWebServerRequest* request) { handleApiOtaStatus(request); });

    _server->on("/api/ota/upload", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            // Final response after upload completes
            if (_otaError.length() > 0) {
                request->send(400, "application/json", "{\"error\":\"" + _otaError + "\"}");
            } else {
                request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Update successful. Rebooting...\"}");
                delay(500);
                ESP.restart();
            }
        },
        [this](AsyncWebServerRequest* request, String filename, size_t index,
               uint8_t* data, size_t len, bool final) {
            handleOtaUpload(request, filename, index, data, len, final);
        }
    );

    // Serve static files from LittleFS - must come AFTER API routes
    _server->serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // Handle 404
    _server->onNotFound(
        [this](AsyncWebServerRequest* request) { handleNotFound(request); });
}

void WebServer::handleApiStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;

    // Version
    doc["version"] = TINKLINK_VERSION_STRING;

    // WiFi status
    doc["wifi"]["connected"] = _wifi->isConnected();
    doc["wifi"]["ssid"] = _wifi->getSSID();
    doc["wifi"]["ip"] = _wifi->getIP();
    doc["wifi"]["rssi"] = _wifi->getRSSI();

    const char* stateStr = "unknown";
    switch (_wifi->getState()) {
        case WifiManager::State::DISCONNECTED: stateStr = "disconnected"; break;
        case WifiManager::State::CONNECTING: stateStr = "connecting"; break;
        case WifiManager::State::CONNECTED: stateStr = "connected"; break;
        case WifiManager::State::FAILED: stateStr = "failed"; break;
        case WifiManager::State::AP_ACTIVE: stateStr = "ap_active"; break;
    }
    doc["wifi"]["state"] = stateStr;

    const char* modeStr = "sta";
    switch (_wifi->getMode()) {
        case WifiManager::Mode::STA: modeStr = "sta"; break;
        case WifiManager::Mode::AP: modeStr = "ap"; break;
        case WifiManager::Mode::AP_STA: modeStr = "ap_sta"; break;
    }
    doc["wifi"]["mode"] = modeStr;

    // Add AP info if in AP mode
    if (_wifi->isAPActive()) {
        auto apConfig = _wifi->getAPConfig();
        doc["wifi"]["ap_ssid"] = apConfig.ssid;
        doc["wifi"]["ap_ip"] = apConfig.ip.toString();
    }

    // Switcher status
    doc["switcher"]["type"] = _switcher->getTypeName();
    doc["switcher"]["currentInput"] = _switcher->getCurrentInput();

    // RetroTINK status
    doc["tink"]["connected"] = _tink->isConnected();
    doc["tink"]["powerState"] = _tink->getPowerStateString();
    doc["tink"]["lastCommand"] = _tink->getLastCommand();

    // AVR status
    if (_avr) {
        auto avrConfig = _config->getAvrConfig();
        doc["avr"]["type"] = avrConfig["type"] | "Denon X4300H";
        doc["avr"]["enabled"] = true;  // AVR exists, so it's enabled
        doc["avr"]["connected"] = _avr->isConnected();
        doc["avr"]["ip"] = avrConfig["ip"] | "";
        doc["avr"]["input"] = _avr->getInput();
        doc["avr"]["lastCommand"] = _avr->getLastCommand();
        doc["avr"]["lastResponse"] = _avr->getLastResponse();
    } else {
        doc["avr"]["enabled"] = false;
    }

    // Triggers
    JsonArray triggersArray = doc["triggers"].to<JsonArray>();
    for (const auto& trigger : _config->getTriggers()) {
        JsonObject triggerObj = triggersArray.add<JsonObject>();
        triggerObj["input"] = trigger.switcherInput;
        triggerObj["profile"] = trigger.profile;
        triggerObj["mode"] = trigger.mode == TriggerMapping::SVS ? "SVS" : "Remote";
        triggerObj["name"] = trigger.name;
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleApiScan(AsyncWebServerRequest* request) {
    JsonDocument doc;

    // Check if scan is complete
    if (_wifi->isScanComplete()) {
        // Get results
        auto networks = _wifi->getScanResults();

        doc["status"] = "complete";
        JsonArray networksArray = doc["networks"].to<JsonArray>();

        for (const auto& net : networks) {
            JsonObject netObj = networksArray.add<JsonObject>();
            netObj["ssid"] = net.ssid;
            netObj["rssi"] = net.rssi;
            netObj["secure"] = net.encryptionType != WIFI_AUTH_OPEN;
        }

        // Start a new scan for next time
        _wifi->startScan();
    } else {
        // Try to start a new scan
        if (_wifi->startScan()) {
            doc["status"] = "scanning";
        } else {
            doc["status"] = "scanning";  // Already in progress
        }
        doc["networks"] = JsonArray();
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleApiConnect(AsyncWebServerRequest* request) {
    String ssid;
    String password;

    if (request->hasParam("ssid", true)) {
        ssid = request->getParam("ssid", true)->value();
    }
    if (request->hasParam("password", true)) {
        password = request->getParam("password", true)->value();
    }

    if (ssid.length() == 0) {
        request->send(400, "application/json", "{\"error\":\"SSID required\"}");
        return;
    }

    LOG_INFO("WebServer: Connect request for '%s'", ssid.c_str());

    if (_wifi->connect(ssid, password)) {
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to start connection\"}");
    }
}

void WebServer::handleApiDisconnect(AsyncWebServerRequest* request) {
    _wifi->disconnect();
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebServer::handleApiSave(AsyncWebServerRequest* request) {
    String ssid;
    String password;

    if (request->hasParam("ssid", true)) {
        ssid = request->getParam("ssid", true)->value();
    }
    if (request->hasParam("password", true)) {
        password = request->getParam("password", true)->value();
    }

    if (ssid.length() == 0) {
        request->send(400, "application/json", "{\"error\":\"SSID required\"}");
        return;
    }

    _config->setWifiCredentials(ssid, password);
    if (_config->saveWifiConfig()) {
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to save configuration\"}");
    }
}

void WebServer::handleApiConfigTriggers(AsyncWebServerRequest* request) {
    if (!request->hasParam("triggers", true)) {
        request->send(400, "application/json", "{\"error\":\"Missing triggers parameter\"}");
        return;
    }

    String triggersJson = request->getParam("triggers", true)->value();

    // Parse JSON array of triggers
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, triggersJson);

    if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    if (!doc.is<JsonArray>()) {
        request->send(400, "application/json", "{\"error\":\"Triggers must be an array\"}");
        return;
    }

    // Convert JSON to TriggerMapping vector
    std::vector<TriggerMapping> triggers;
    for (JsonObject triggerObj : doc.as<JsonArray>()) {
        TriggerMapping trigger;
        trigger.switcherInput = triggerObj["input"] | 0;
        trigger.profile = triggerObj["profile"] | 0;
        trigger.name = triggerObj["name"] | "";

        String mode = triggerObj["mode"] | "SVS";
        trigger.mode = (mode == "Remote") ? TriggerMapping::REMOTE : TriggerMapping::SVS;

        if (trigger.switcherInput > 0 && trigger.profile > 0) {
            triggers.push_back(trigger);
        }
    }

    LOG_INFO("WebServer: Updating triggers (count: %d)", triggers.size());

    // Update configuration
    _config->setTriggers(triggers);
    if (_config->saveConfig()) {
        // Update RetroTink with new triggers
        _tink->clearTriggers();
        for (const auto& trigger : triggers) {
            _tink->addTrigger(trigger);
        }

        request->send(200, "application/json", "{\"status\":\"ok\"}");
        LOG_INFO("WebServer: Triggers saved successfully");
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to save configuration\"}");
        LOG_ERROR("WebServer: Failed to save triggers");
    }
}

void WebServer::handleApiTinkSend(AsyncWebServerRequest* request) {
    String command;

    if (request->hasParam("command", true)) {
        command = request->getParam("command", true)->value();
    }

    if (command.length() == 0) {
        request->send(400, "application/json", "{\"error\":\"Command required\"}");
        return;
    }

    LOG_DEBUG("WebServer: Tink command: %s", command.c_str());

    // Send command to RetroTINK
    _tink->sendRawCommand(command.c_str());

    // Return success response
    JsonDocument doc;
    doc["status"] = "ok";
    doc["command"] = command;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleApiDebugLED(AsyncWebServerRequest* request) {
    if (!_ledCallback) {
        request->send(500, "application/json", "{\"error\":\"LED control not available\"}");
        return;
    }

    int r = -1, g = -1, b = -1;
    bool reset = false;

    // Check if this is a reset request
    if (request->hasParam("reset", true)) {
        reset = true;
    }
    // Check for named color
    else if (request->hasParam("color", true)) {
        String color = request->getParam("color", true)->value();
        color.toLowerCase();

        if (color == "red") { r = 255; g = 0; b = 0; }
        else if (color == "green") { r = 0; g = 255; b = 0; }
        else if (color == "blue") { r = 0; g = 0; b = 255; }
        else if (color == "yellow") { r = 255; g = 255; b = 0; }
        else if (color == "cyan") { r = 0; g = 255; b = 255; }
        else if (color == "magenta") { r = 255; g = 0; b = 255; }
        else if (color == "white") { r = 255; g = 255; b = 255; }
        else if (color == "off") { r = 0; g = 0; b = 0; }
        else {
            request->send(400, "application/json", "{\"error\":\"Unknown color\"}");
            return;
        }
    }
    // Check for custom RGB values
    else if (request->hasParam("r", true) && request->hasParam("g", true) && request->hasParam("b", true)) {
        r = request->getParam("r", true)->value().toInt();
        g = request->getParam("g", true)->value().toInt();
        b = request->getParam("b", true)->value().toInt();

        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
            request->send(400, "application/json", "{\"error\":\"RGB values must be 0-255\"}");
            return;
        }
    }
    else {
        request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
        return;
    }

    // Call the LED control callback
    if (reset) {
        _ledCallback(-1, -1, -1);  // -1 = reset to WiFi mode
        LOG_DEBUG("WebServer: LED reset to WiFi mode");
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        _ledCallback(r, g, b);
        LOG_DEBUG("WebServer: LED set to RGB(%d,%d,%d)", r, g, b);

        // Return success response with the RGB values
        JsonDocument doc;
        doc["status"] = "ok";
        doc["r"] = r;
        doc["g"] = g;
        doc["b"] = b;

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    }
}

void WebServer::handleApiSwitcherSend(AsyncWebServerRequest* request) {
    if (!request->hasParam("message", true)) {
        request->send(400, "application/json", "{\"error\":\"Missing message parameter\"}");
        return;
    }

    String message = request->getParam("message", true)->value();

    LOG_DEBUG("WebServer: Sending switcher message: [%s]", message.c_str());

    // Send via switcher UART
    _switcher->sendCommand(message.c_str());

    // Return success response
    JsonDocument doc;
    doc["status"] = "ok";
    doc["message"] = message;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleApiSwitcherReceive(AsyncWebServerRequest* request) {
    // Get count parameter (default 10, max 50)
    int count = 10;
    if (request->hasParam("count")) {
        count = request->getParam("count")->value().toInt();
        if (count < 1) count = 1;
        if (count > 50) count = 50;
    }

    // Check if clear parameter is present
    if (request->hasParam("clear")) {
        _switcher->clearRecentMessages();
    }

    // Get recent messages
    std::vector<String> messages = _switcher->getRecentMessages(count);

    // Build JSON response
    JsonDocument doc;
    doc["count"] = messages.size();

    JsonArray messagesArray = doc["messages"].to<JsonArray>();
    for (const String& msg : messages) {
        messagesArray.add(msg);
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleApiLogs(AsyncWebServerRequest* request) {
    Logger& logger = Logger::instance();

    // Get since parameter for incremental updates
    unsigned long since = 0;
    if (request->hasParam("since")) {
        since = request->getParam("since")->value().toInt();
    }

    // Get count parameter (default 50, max 100)
    int count = 50;
    if (request->hasParam("count")) {
        count = request->getParam("count")->value().toInt();
        if (count < 1) count = 1;
        if (count > 100) count = 100;
    }

    // Check if clear parameter is present
    if (request->hasParam("clear")) {
        logger.clearLogs();
    }

    // Get logs
    std::vector<LogEntry> logs;
    if (since > 0) {
        logs = logger.getLogsSince(since, count);
    } else {
        logs = logger.getRecentLogs(count);
    }

    // Build JSON response
    JsonDocument doc;
    doc["total"] = logger.getLogCount();
    doc["count"] = logs.size();

    JsonArray logsArray = doc["logs"].to<JsonArray>();
    for (const LogEntry& entry : logs) {
        JsonObject logObj = logsArray.add<JsonObject>();
        logObj["ts"] = entry.timestamp;
        logObj["lvl"] = static_cast<int>(entry.level);
        logObj["msg"] = entry.message;
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleApiOtaStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;

    doc["inProgress"] = _otaInProgress;
    doc["progress"] = _otaProgress;
    doc["total"] = _otaTotal;
    doc["error"] = _otaError;

    if (_otaTotal > 0) {
        doc["percent"] = (int)((_otaProgress * 100) / _otaTotal);
    } else {
        doc["percent"] = 0;
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleOtaUpload(AsyncWebServerRequest* request, String filename,
                                 size_t index, uint8_t* data, size_t len, bool final) {
    // Track progress percentage for logging (reset each upload)
    static int lastPercent = -1;

    // First chunk - initialize update
    if (index == 0) {
        _otaError = "";
        _otaProgress = 0;
        _otaTotal = request->contentLength();
        _otaInProgress = true;
        lastPercent = -1;  // Reset progress tracking for new upload

        // Determine update type from query parameter or filename
        _otaMode = OTAMode::FIRMWARE;
        if (request->hasParam("mode", true)) {
            String mode = request->getParam("mode", true)->value();
            if (mode == "fs" || mode == "filesystem") {
                _otaMode = OTAMode::FILESYSTEM;
            }
        } else if (filename.endsWith(".bin") && filename.indexOf("littlefs") >= 0) {
            _otaMode = OTAMode::FILESYSTEM;
        }

        int updateCommand = (_otaMode == OTAMode::FILESYSTEM) ? U_SPIFFS : U_FLASH;
        const char* updateType = (_otaMode == OTAMode::FILESYSTEM) ? "filesystem" : "firmware";

        LOG_INFO("OTA: Starting %s update, size: %u bytes", updateType, _otaTotal);
        LOG_INFO("OTA: Filename: %s", filename.c_str());

        // For filesystem updates, unmount LittleFS first
        if (_otaMode == OTAMode::FILESYSTEM) {
            LittleFS.end();
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, updateCommand)) {
            _otaError = Update.errorString();
            LOG_ERROR("OTA: Update.begin() failed: %s", _otaError.c_str());
            _otaInProgress = false;
            return;
        }
    }

    // Write chunk
    if (_otaInProgress && len > 0) {
        if (Update.write(data, len) != len) {
            _otaError = Update.errorString();
            LOG_ERROR("OTA: Update.write() failed: %s", _otaError.c_str());
            _otaInProgress = false;
            return;
        }
        _otaProgress += len;

        // Log progress every 10%
        int percent = (_otaTotal > 0) ? (int)((_otaProgress * 100) / _otaTotal) : 0;
        if (percent / 10 > lastPercent / 10) {
            LOG_INFO("OTA: Progress %d%%", percent);
            lastPercent = percent;
        }
    }

    // Final chunk - finish update
    if (final) {
        if (!Update.end(true)) {
            _otaError = Update.errorString();
            LOG_ERROR("OTA: Update.end() failed: %s", _otaError.c_str());
        } else {
            LOG_INFO("OTA: Update successful! Total: %u bytes", _otaProgress);
        }
        _otaInProgress = false;
    }
}

void WebServer::handleApiAvrSend(AsyncWebServerRequest* request) {
    if (!_avr) {
        request->send(500, "application/json", "{\"error\":\"AVR not configured\"}");
        return;
    }

    String command;
    if (request->hasParam("command", true)) {
        command = request->getParam("command", true)->value();
    }

    if (command.length() == 0) {
        request->send(400, "application/json", "{\"error\":\"Command required\"}");
        return;
    }

    if (!_avr) {
        request->send(400, "application/json", "{\"error\":\"AVR control is disabled\"}");
        return;
    }

    LOG_DEBUG("WebServer: AVR command: %s", command.c_str());

    bool sent = _avr->sendRawCommand(command);

    JsonDocument doc;
    doc["status"] = sent ? "ok" : "error";
    doc["command"] = command;
    if (!sent) {
        doc["error"] = "Failed to send command";
    }

    String response;
    serializeJson(doc, response);
    request->send(sent ? 200 : 500, "application/json", response);
}

void WebServer::handleApiAvrDiscover(AsyncWebServerRequest* request) {
    if (!_avr) {
        request->send(500, "application/json", "{\"error\":\"AVR not configured\"}");
        return;
    }

    JsonDocument doc;

    if (_avr->isDiscoveryComplete()) {
        auto devices = _avr->getDiscoveryResults();

        doc["status"] = "complete";
        JsonArray devicesArray = doc["devices"].to<JsonArray>();

        for (const auto& dev : devices) {
            JsonObject devObj = devicesArray.add<JsonObject>();
            devObj["ip"] = dev.ip;
            devObj["name"] = dev.friendlyName;
        }

        // Start a new scan for the next poll
        _avr->startDiscovery();
    } else {
        _avr->startDiscovery(); // Start if not already running
        doc["status"] = "discovering";
        doc["devices"] = JsonArray();
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleApiConfigAvrGet(AsyncWebServerRequest* request) {
    auto avrConfig = _config->getAvrConfig();

    JsonDocument doc;
    doc["type"] = avrConfig["type"] | "Denon X4300H";
    doc["enabled"] = avrConfig["enabled"] | false;
    doc["ip"] = avrConfig["ip"] | "";
    doc["input"] = avrConfig["input"] | "GAME";

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleApiConfigAvr(AsyncWebServerRequest* request) {
    // Build new config from request params
    JsonDocument newConfigDoc;
    auto avrConfig = _config->getAvrConfig();

    // Start with existing values
    newConfigDoc["type"] = avrConfig["type"] | "Denon X4300H";
    newConfigDoc["enabled"] = avrConfig["enabled"] | false;
    newConfigDoc["ip"] = avrConfig["ip"] | "";
    newConfigDoc["input"] = avrConfig["input"] | "GAME";

    // Update with request params
    if (request->hasParam("enabled", true)) {
        String val = request->getParam("enabled", true)->value();
        newConfigDoc["enabled"] = (val == "true" || val == "1");
    }
    if (request->hasParam("ip", true)) {
        newConfigDoc["ip"] = request->getParam("ip", true)->value();
    }
    if (request->hasParam("input", true)) {
        newConfigDoc["input"] = request->getParam("input", true)->value();
    }

    JsonObject newConfig = newConfigDoc.as<JsonObject>();
    _config->setAvrConfig(newConfig);

    if (_config->saveConfig()) {
        // Reconfigure live instance
        if (_avr) {
            _avr->configure(newConfig);
            _avr->begin();
        }

        request->send(200, "application/json", "{\"status\":\"ok\"}");
        LOG_INFO("WebServer: AVR config saved (enabled: %s, ip: %s, input: %s)",
                 newConfig["enabled"].as<bool>() ? "yes" : "no",
                 newConfig["ip"].as<const char*>(),
                 newConfig["input"].as<const char*>());
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to save configuration\"}");
    }
}

void WebServer::handleNotFound(AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not Found");
}
