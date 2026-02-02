#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <functional>

class WifiManager;
class ConfigManager;
class ExtronSwVga;
class RetroTink;

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
 * - GET  /api/status          - System status (WiFi, Extron, triggers)
 * - GET  /api/wifi/scan       - Scan for WiFi networks
 * - POST /api/wifi/connect    - Connect to WiFi network
 * - POST /api/wifi/disconnect - Disconnect from WiFi
 * - POST /api/wifi/save       - Save WiFi credentials
 * - POST /api/debug/send      - Send command to RetroTINK
 * - POST /api/debug/led       - Control status LED
 * - POST /api/uart/send       - Send UART test message
 * - GET  /api/uart/receive    - Get recent UART messages
 * - GET  /api/logs            - Get system logs
 * - GET  /api/ota/status      - Get OTA update progress
 * - POST /api/ota/upload      - Upload firmware or filesystem
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
     * @param extron Extron handler for UART operations
     * @param tink RetroTINK controller for commands
     */
    void begin(WifiManager* wifi, ConfigManager* config, ExtronSwVga* extron, RetroTink* tink);

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
    ExtronSwVga* _extron;
    RetroTink* _tink;
    LEDControlCallback _ledCallback;

    // OTA state
    OTAMode _otaMode;
    size_t _otaProgress;
    size_t _otaTotal;
    bool _otaInProgress;
    String _otaError;

    /** Configure all HTTP routes and handlers. */
    void setupRoutes();

    // API route handlers
    void handleApiStatus(AsyncWebServerRequest* request);
    void handleApiScan(AsyncWebServerRequest* request);
    void handleApiConnect(AsyncWebServerRequest* request);
    void handleApiDisconnect(AsyncWebServerRequest* request);
    void handleApiSave(AsyncWebServerRequest* request);
    void handleApiDebugSend(AsyncWebServerRequest* request);
    void handleApiDebugLED(AsyncWebServerRequest* request);
    void handleApiUartSend(AsyncWebServerRequest* request);
    void handleApiUartReceive(AsyncWebServerRequest* request);
    void handleApiLogs(AsyncWebServerRequest* request);
    void handleApiOtaStatus(AsyncWebServerRequest* request);
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
