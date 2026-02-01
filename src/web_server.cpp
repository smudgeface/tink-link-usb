#include "web_server.h"
#include "wifi_manager.h"
#include "config_manager.h"
#include "extron_sw_vga.h"
#include "retrotink.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

WebServer::WebServer(uint16_t port)
    : _server(new AsyncWebServer(port))
    , _wifi(nullptr)
    , _config(nullptr)
    , _extron(nullptr)
    , _tink(nullptr)
{
}

WebServer::~WebServer() {
    delete _server;
}

void WebServer::begin(WifiManager* wifi, ConfigManager* config, ExtronSwVga* extron, RetroTink* tink) {
    _wifi = wifi;
    _config = config;
    _extron = extron;
    _tink = tink;

    setupRoutes();
    _server->begin();

    Serial.println("WebServer: Started on port 80");
}

void WebServer::end() {
    _server->end();
}

void WebServer::setupRoutes() {
    // API endpoints - register these BEFORE serveStatic to ensure they're matched first
    _server->on("/api/status", HTTP_GET,
        [this](AsyncWebServerRequest* request) { handleApiStatus(request); });

    _server->on("/api/scan", HTTP_GET,
        [this](AsyncWebServerRequest* request) { handleApiScan(request); });

    _server->on("/api/connect", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiConnect(request); });

    _server->on("/api/disconnect", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiDisconnect(request); });

    _server->on("/api/save", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiSave(request); });

    // Debug endpoints
    _server->on("/api/debug/send", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiDebugSend(request); });

    _server->on("/api/debug/simulate", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiDebugSimulate(request); });

    _server->on("/api/debug/continuous", HTTP_POST,
        [this](AsyncWebServerRequest* request) { handleApiDebugContinuous(request); });

    // Serve static files from LittleFS - must come AFTER API routes
    _server->serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // Handle 404
    _server->onNotFound(
        [this](AsyncWebServerRequest* request) { handleNotFound(request); });
}

void WebServer::handleRoot(AsyncWebServerRequest* request) {
    request->send(LittleFS, "/index.html", "text/html");
}

void WebServer::handleConfigPage(AsyncWebServerRequest* request) {
    request->send(LittleFS, "/config.html", "text/html");
}

void WebServer::handleApiStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;

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

    // Extron status
    doc["extron"]["currentInput"] = _extron->getCurrentInput();

    // RetroTINK status
    doc["tink"]["lastCommand"] = _tink->getLastCommand();

    // Triggers
    JsonArray triggersArray = doc["triggers"].to<JsonArray>();
    for (const auto& trigger : _config->getTriggers()) {
        JsonObject triggerObj = triggersArray.add<JsonObject>();
        triggerObj["input"] = trigger.extronInput;
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

    Serial.printf("WebServer: Connect request for '%s'\n", ssid.c_str());

    if (_wifi->connect(ssid, password)) {
        request->send(200, "application/json", "{\"status\":\"connecting\"}");
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to start connection\"}");
    }
}

void WebServer::handleApiDisconnect(AsyncWebServerRequest* request) {
    _wifi->disconnect();
    request->send(200, "application/json", "{\"status\":\"disconnected\"}");
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
        request->send(200, "application/json", "{\"status\":\"saved\"}");
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to save configuration\"}");
    }
}

void WebServer::handleApiDebugSend(AsyncWebServerRequest* request) {
    String command;

    if (request->hasParam("command", true)) {
        command = request->getParam("command", true)->value();
    }

    if (command.length() == 0) {
        request->send(400, "application/json", "{\"error\":\"Command required\"}");
        return;
    }

    Serial.printf("WebServer: Debug command: %s\n", command.c_str());

    // Send command to RetroTINK
    _tink->sendRawCommand(command.c_str());

    // Return success response
    JsonDocument doc;
    doc["status"] = "sent";
    doc["command"] = command;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleApiDebugSimulate(AsyncWebServerRequest* request) {
    int input = 0;

    if (request->hasParam("input", true)) {
        input = request->getParam("input", true)->value().toInt();
    }

    if (input < 1 || input > 16) {
        request->send(400, "application/json", "{\"error\":\"Input must be 1-16\"}");
        return;
    }

    Serial.printf("WebServer: Simulating Extron input change: %d\n", input);

    // Simulate the input change by calling the RetroTINK handler directly
    _tink->onExtronInputChange(input);

    // Return success response
    JsonDocument doc;
    doc["status"] = "simulated";
    doc["input"] = input;
    doc["result"] = _tink->getLastCommand();

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleApiDebugContinuous(AsyncWebServerRequest* request) {
    int count = 10;  // Default 10 signals

    if (request->hasParam("count", true)) {
        count = request->getParam("count", true)->value().toInt();
        if (count < 1) count = 1;
        if (count > 100) count = 100;  // Max 100 to prevent abuse
    }

    Serial.printf("WebServer: Sending continuous test pattern (%d signals)\n", count);

    // Send continuous test signals
    _tink->sendContinuousTest(count);

    // Return success response
    JsonDocument doc;
    doc["status"] = "sent";
    doc["count"] = count;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleNotFound(AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not Found");
}
