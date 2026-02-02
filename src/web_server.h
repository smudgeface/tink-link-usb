#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <functional>

class WifiManager;
class ConfigManager;
class ExtronSwVga;
class RetroTink;

// LED control callback: r, g, b values (0-255), or -1 to reset to WiFi mode
using LEDControlCallback = std::function<void(int r, int g, int b)>;

// OTA update modes
enum class OTAMode {
    FIRMWARE,
    FILESYSTEM
};

// Web server for status monitoring and WiFi configuration
class WebServer {
public:
    WebServer(uint16_t port = 80);
    ~WebServer();

    void begin(WifiManager* wifi, ConfigManager* config, ExtronSwVga* extron, RetroTink* tink);
    void end();
    void setLEDCallback(LEDControlCallback callback);

private:
    AsyncWebServer* _server;
    WifiManager* _wifi;
    ConfigManager* _config;
    ExtronSwVga* _extron;
    RetroTink* _tink;
    LEDControlCallback _ledCallback;

    // OTA state
    OTAMode _otaMode;
    size_t _otaProgress;
    size_t _otaTotal;
    bool _otaInProgress;
    String _otaError;

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
    void handleApiDebugContinuous(AsyncWebServerRequest* request);
    void handleApiDebugLED(AsyncWebServerRequest* request);
    void handleApiUartSend(AsyncWebServerRequest* request);
    void handleApiUartReceive(AsyncWebServerRequest* request);
    void handleApiLogs(AsyncWebServerRequest* request);
    void handleApiOtaStatus(AsyncWebServerRequest* request);
    void handleNotFound(AsyncWebServerRequest* request);

    // OTA upload handler
    void handleOtaUpload(AsyncWebServerRequest* request, String filename, size_t index,
                         uint8_t* data, size_t len, bool final);
};

#endif // WEB_SERVER_H
