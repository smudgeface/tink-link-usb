#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <functional>

class WifiManager;
class ConfigManager;
class ExtronSwVga;
class RetroTink;

// Web server for status monitoring and WiFi configuration
class WebServer {
public:
    WebServer(uint16_t port = 80);
    ~WebServer();

    void begin(WifiManager* wifi, ConfigManager* config, ExtronSwVga* extron, RetroTink* tink);
    void end();

private:
    AsyncWebServer* _server;
    WifiManager* _wifi;
    ConfigManager* _config;
    ExtronSwVga* _extron;
    RetroTink* _tink;

    void setupRoutes();

    // Route handlers
    void handleRoot(AsyncWebServerRequest* request);
    void handleConfigPage(AsyncWebServerRequest* request);
    void handleApiStatus(AsyncWebServerRequest* request);
    void handleApiScan(AsyncWebServerRequest* request);
    void handleApiConnect(AsyncWebServerRequest* request);
    void handleApiDisconnect(AsyncWebServerRequest* request);
    void handleApiSave(AsyncWebServerRequest* request);
    void handleApiDebugSend(AsyncWebServerRequest* request);
    void handleApiDebugSimulate(AsyncWebServerRequest* request);
    void handleApiDebugContinuous(AsyncWebServerRequest* request);
    void handleNotFound(AsyncWebServerRequest* request);
};

#endif // WEB_SERVER_H
