#include <Arduino.h>
#include "config_manager.h"
#include "extron_sw_vga.h"
#include "retrotink.h"
#include "wifi_manager.h"
#include "web_server.h"

// WS2812 RGB LED on GPIO21 (Waveshare ESP32-S3-Zero)
#define RGB_LED_PIN 21

// Global instances
ConfigManager configManager;
ExtronSwVga* extron = nullptr;
RetroTink* tink = nullptr;
WifiManager wifiManager;
WebServer webServer;

void setup() {
    // Initialize serial for debug output
    Serial.begin(115200);
    delay(1000);  // Give USB CDC time to connect

    // Initialize RGB LED (turn it off initially)
    pinMode(RGB_LED_PIN, OUTPUT);
    digitalWrite(RGB_LED_PIN, LOW);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  TinkLink-USB v1.0.0");
    Serial.println("  ESP32-S3 RetroTINK 4K Controller");
    Serial.println("========================================");
    Serial.println();

    // Initialize configuration manager (LittleFS)
    Serial.println("[1/5] Initializing configuration...");
    if (!configManager.begin()) {
        Serial.println("ERROR: Failed to initialize configuration manager!");
    }

    // Get pin configurations
    auto extronConfig = configManager.getExtronConfig();
    auto wifiConfig = configManager.getWifiConfig();

    // Initialize RetroTINK controller (stub mode for now)
    Serial.println("[2/5] Initializing RetroTINK controller...");
    tink = new RetroTink();
    tink->begin();

    // Load triggers from config
    for (const auto& trigger : configManager.getTriggers()) {
        tink->addTrigger(trigger);
    }

    // Initialize Extron switcher
    Serial.println("[3/5] Initializing Extron SW VGA...");
    extron = new ExtronSwVga(extronConfig.txPin, extronConfig.rxPin, 9600);
    if (!extron->begin()) {
        Serial.println("ERROR: Failed to initialize Extron handler!");
    }

    // Connect Extron input changes to RetroTINK
    extron->onInputChange([](int input) {
        Serial.printf("Input change detected: %d\n", input);
        tink->onExtronInputChange(input);
    });

    // Initialize WiFi manager
    Serial.println("[4/5] Initializing WiFi...");
    wifiManager.begin(wifiConfig.hostname);

    // Set up WiFi state change callback
    wifiManager.onStateChange([](WifiManager::State state) {
        switch (state) {
            case WifiManager::State::CONNECTED:
                Serial.println("WiFi: Connected!");
                break;
            case WifiManager::State::DISCONNECTED:
                Serial.println("WiFi: Disconnected");
                break;
            case WifiManager::State::CONNECTING:
                Serial.println("WiFi: Connecting...");
                break;
            case WifiManager::State::FAILED:
                Serial.println("WiFi: Connection failed");
                break;
            case WifiManager::State::AP_ACTIVE:
                Serial.println("WiFi: Access Point active");
                break;
        }
    });

    // Auto-connect if credentials are saved, otherwise start AP mode
    if (configManager.hasWifiCredentials()) {
        Serial.printf("Attempting to connect to saved network: %s\n",
                      wifiConfig.ssid.c_str());
        wifiManager.connect(wifiConfig.ssid, wifiConfig.password);
    } else {
        Serial.println("No WiFi credentials saved - starting Access Point mode");
        Serial.println("Connect to the AP and configure WiFi via web interface");
        wifiManager.startAccessPoint();
    }

    // Initialize web server
    Serial.println("[5/5] Starting web server...");
    webServer.begin(&wifiManager, &configManager, extron, tink);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  Initialization complete!");
    Serial.println("========================================");
    Serial.println();
    Serial.println("Pin assignments:");
    Serial.printf("  Extron TX:    GPIO%d\n", extronConfig.txPin);
    Serial.printf("  Extron RX:    GPIO%d\n", extronConfig.rxPin);
    Serial.printf("  RGB LED:      GPIO%d\n", RGB_LED_PIN);
    Serial.println();
    Serial.println("USB Mode: CDC (Serial debugging enabled)");
    Serial.println("Note: USB Host for RetroTINK not yet implemented");
    Serial.println("      RetroTINK commands will be logged only");
    Serial.println();
    if (wifiManager.isAPActive()) {
        Serial.printf("Web interface: http://%s\n", wifiManager.getIP().c_str());
    } else {
        Serial.println("Web interface: http://tinklink.local");
    }
    Serial.println();
}

void loop() {
    // Update WiFi connection state
    wifiManager.update();

    // Process incoming Extron messages
    extron->update();

    // Simple RGB LED control using digitalWrite
    // For now, just use it as a simple indicator (blue LED function)
    // TODO: Could use FastLED or Adafruit_NeoPixel for full RGB control
    static unsigned long lastBlink = 0;
    static bool ledOn = false;
    unsigned long now = millis();

    if (wifiManager.getState() == WifiManager::State::AP_ACTIVE) {
        // Blink in AP mode (500ms interval)
        if (now - lastBlink >= 500) {
            lastBlink = now;
            ledOn = !ledOn;
            digitalWrite(RGB_LED_PIN, ledOn ? HIGH : LOW);
        }
    } else if (wifiManager.getState() == WifiManager::State::CONNECTED) {
        // Solid on when connected
        digitalWrite(RGB_LED_PIN, HIGH);
    } else {
        // Off for other states
        digitalWrite(RGB_LED_PIN, LOW);
    }

    // Small delay to prevent tight loop
    delay(1);
}
