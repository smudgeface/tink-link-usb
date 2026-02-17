#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <functional>

class WifiManager;
class ConfigManager;
class Switcher;
class RetroTink;
class DenonAvr;

/**
 * LED control callback function type.
 * @param r Red value (0-255), or -1 to reset to WiFi state mode
 * @param g Green value (0-255), or -1 to reset
 * @param b Blue value (0-255), or -1 to reset
 */
using LEDControlCallback = std::function<void(int r, int g, int b)>;

/** OTA update type */
enum class OTAMode {
    FIRMWARE,    ///< Application firmware update
    FILESYSTEM   ///< LittleFS filesystem update
};

/**
 * Async web server for TinkLink-USB.
 *
 * Provides:
 * - Static file serving from LittleFS (index.html, config.html, debug.html)
 * - REST API for status, WiFi config, debug commands
 * - OTA firmware and filesystem updates
 * - UART testing endpoints
 * - System log retrieval
 *
 * API Endpoints:
 * - GET  /api/status             - System status (WiFi, switcher, triggers)
 * - GET  /api/wifi/scan          - Scan for WiFi networks
 * - POST /api/wifi/connect       - Connect to WiFi network
 * - POST /api/wifi/disconnect    - Disconnect from WiFi
 * - POST /api/wifi/save          - Save WiFi credentials
 * - POST /api/tink/send          - Send command to RetroTINK
 * - POST /api/debug/led          - Control status LED
 * - POST /api/switcher/send      - Send message to video switcher
 * - GET  /api/switcher/receive   - Get recent switcher messages
 * - GET  /api/logs               - Get system logs
 * - GET  /api/ota/status         - Get OTA update progress
 * - POST /api/ota/upload         - Upload firmware or filesystem
 */
class WebServer {
public:
    /**
     * Create web server on specified port.
     * @param port HTTP port (default 80)
     */
    WebServer(uint16_t port = 80);

    ~WebServer();

    /**
     * Start the web server with required dependencies.
     * @param wifi WiFi manager for network operations
     * @param config Configuration manager for settings
     * @param switcher Video switcher handler for commands
     * @param tink RetroTINK controller for commands
     * @param avr Denon AVR controller (nullptr if not used)
     */
    void begin(WifiManager* wifi, ConfigManager* config, Switcher* switcher, RetroTink* tink, DenonAvr** avr = nullptr);

    /** Stop the web server. */
    void end();

    /**
     * Set callback for LED control from debug interface.
     * @param callback Function to call for LED color changes
     */
    void setLEDCallback(LEDControlCallback callback);

private:
    AsyncWebServer* _server;
    WifiManager* _wifi;
    ConfigManager* _config;
    Switcher* _switcher;
    RetroTink* _tink;
    DenonAvr** _avrPtr;  // Pointer-to-pointer so we can create/destroy at runtime
    DenonAvr* avr() const { return _avrPtr ? *_avrPtr : nullptr; }
    LEDControlCallback _ledCallback;

    // OTA state
    OTAMode _otaMode;
    size_t _otaProgress;
    size_t _otaTotal;
    bool _otaInProgress;
    String _otaError;

    // Config restore state
    String _restoreBody;
    String _restoreError;

    /** Configure all HTTP routes and handlers. */
    void setupRoutes();

    // API route handlers
    void handleApiStatus(AsyncWebServerRequest* request);
    void handleApiScan(AsyncWebServerRequest* request);
    void handleApiConnect(AsyncWebServerRequest* request);
    void handleApiDisconnect(AsyncWebServerRequest* request);
    void handleApiSave(AsyncWebServerRequest* request);
    void handleApiConfigTriggers(AsyncWebServerRequest* request);
    void handleApiTinkSend(AsyncWebServerRequest* request);
    void handleApiDebugLED(AsyncWebServerRequest* request);
    void handleApiSwitcherSend(AsyncWebServerRequest* request);
    void handleApiSwitcherReceive(AsyncWebServerRequest* request);
    void handleApiLogs(AsyncWebServerRequest* request);
    void handleApiOtaStatus(AsyncWebServerRequest* request);
    void handleApiAvrSend(AsyncWebServerRequest* request);
    void handleApiAvrDiscover(AsyncWebServerRequest* request);
    void handleApiConfigAvr(AsyncWebServerRequest* request);
    void handleApiConfigAvrGet(AsyncWebServerRequest* request);
    void handleApiConfigBackup(AsyncWebServerRequest* request);
    void handleApiConfigRestore(AsyncWebServerRequest* request);
    void handleApiConfigRestoreBody(AsyncWebServerRequest* request,
                                     uint8_t* data, size_t len,
                                     size_t index, size_t total);
    void handleNotFound(AsyncWebServerRequest* request);

    /**
     * Handle chunked OTA upload.
     * @param request The HTTP request
     * @param filename Uploaded filename
     * @param index Byte offset of this chunk
     * @param data Chunk data
     * @param len Chunk length
     * @param final true if this is the last chunk
     */
    void handleOtaUpload(AsyncWebServerRequest* request, String filename, size_t index,
                         uint8_t* data, size_t len, bool final);
};

#endif // WEB_SERVER_H
