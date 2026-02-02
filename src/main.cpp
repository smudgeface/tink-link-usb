#include <Arduino.h>
#include <FastLED.h>
#include "ConfigManager.h"
#include "ExtronSwVga.h"
#include "RetroTink.h"
#include "WifiManager.h"
#include "WebServer.h"
#include "Logger.h"
#include "version.h"

// WS2812 RGB LED on GPIO21 (Waveshare ESP32-S3-Zero)
#define RGB_LED_PIN 21
#define NUM_LEDS 1

// FastLED array
CRGB leds[NUM_LEDS];

// Global instances
ConfigManager configManager;
ExtronSwVga* extron = nullptr;
RetroTink* tink = nullptr;
WifiManager wifiManager;
WebServer webServer;

// LED manual control
bool ledManualMode = false;
unsigned long ledManualModeStart = 0;
const unsigned long LED_MANUAL_TIMEOUT = 10000;  // 10 seconds

/**
 * Get the LED color for a given WiFi state.
 * Used to map WiFi connection states to status LED colors.
 * @param state The current WiFi state
 * @return CRGB color value for the LED
 */
CRGB getColorForWifiState(WifiManager::State state) {
    switch (state) {
        case WifiManager::State::CONNECTED:   return CRGB::Green;
        case WifiManager::State::CONNECTING:  return CRGB::Yellow;
        case WifiManager::State::FAILED:      return CRGB::Red;
        case WifiManager::State::AP_ACTIVE:   return CRGB::Blue;
        case WifiManager::State::DISCONNECTED:
        default:                              return CRGB::Black;
    }
}

/**
 * LED control callback for web interface.
 * @param r Red value (0-255), or -1 to reset to WiFi state mode
 * @param g Green value (0-255), or -1 to reset
 * @param b Blue value (0-255), or -1 to reset
 */
void setLEDColor(int r, int g, int b) {
    if (r == -1 && g == -1 && b == -1) {
        // Reset to WiFi mode
        ledManualMode = false;
        LOG_DEBUG("LED: Manual mode disabled - returning to WiFi state indication");

        // Immediately update LED to current WiFi state
        leds[0] = getColorForWifiState(wifiManager.getState());
        FastLED.show();
    } else {
        // Set manual color
        ledManualMode = true;
        ledManualModeStart = millis();
        leds[0] = CRGB(r, g, b);
        FastLED.show();
        LOG_DEBUG("LED: Manual mode enabled - RGB(%d,%d,%d)", r, g, b);
    }
}

void setup() {
    // Initialize serial for debug output
    Serial.begin(115200);
    delay(1000);  // Give USB CDC time to connect

    // Initialize logger
    Logger::instance().begin();

    // Initialize WS2812 RGB LED
    // Note: This board uses RGB color order, not the more common GRB
    FastLED.addLeds<WS2812, RGB_LED_PIN, RGB>(leds, NUM_LEDS);
    FastLED.setBrightness(50);  // 0-255, moderate brightness
    leds[0] = CRGB::Black;
    FastLED.show();

    LOG_RAW("\n");
    LOG_RAW("========================================\n");
    LOG_INFO("  TinkLink-USB v%s", TINKLINK_VERSION_STRING);
    LOG_RAW("  ESP32-S3 RetroTINK 4K Controller\n");
    LOG_RAW("========================================\n");
    LOG_RAW("\n");

    // LED Test Sequence - cycle through colors to verify LED works
    LOG_DEBUG("LED Test: Red...");
    leds[0] = CRGB::Red;
    FastLED.show();
    delay(500);

    LOG_DEBUG("LED Test: Green...");
    leds[0] = CRGB::Green;
    FastLED.show();
    delay(500);

    LOG_DEBUG("LED Test: Blue...");
    leds[0] = CRGB::Blue;
    FastLED.show();
    delay(500);

    LOG_DEBUG("LED Test: Yellow...");
    leds[0] = CRGB::Yellow;
    FastLED.show();
    delay(500);

    LOG_DEBUG("LED Test: Off");
    leds[0] = CRGB::Black;
    FastLED.show();

    // Initialize configuration manager (LittleFS)
    LOG_INFO("[1/5] Initializing configuration...");
    if (!configManager.begin()) {
        LOG_ERROR("Failed to initialize configuration manager!");
    }

    // Get pin configurations
    auto switcherConfig = configManager.getSwitcherConfig();
    auto wifiConfig = configManager.getWifiConfig();

    // Initialize RetroTINK controller (stub mode for now)
    LOG_INFO("[2/5] Initializing RetroTINK controller...");
    tink = new RetroTink();
    tink->begin();

    // Load triggers from config
    for (const auto& trigger : configManager.getTriggers()) {
        tink->addTrigger(trigger);
    }

    // Initialize video switcher
    LOG_INFO("[3/5] Initializing %s...", switcherConfig.type.c_str());
    extron = new ExtronSwVga(switcherConfig.txPin, switcherConfig.rxPin, 9600);
    if (!extron->begin()) {
        LOG_ERROR("Failed to initialize Extron handler!");
    }

    // Connect Extron input changes to RetroTINK
    extron->onInputChange([](int input) {
        LOG_INFO("Input change detected: %d", input);
        tink->onExtronInputChange(input);
    });

    // Initialize WiFi manager
    LOG_INFO("[4/5] Initializing WiFi...");
    wifiManager.begin(wifiConfig.hostname);

    // Set up WiFi state change callback
    wifiManager.onStateChange([](WifiManager::State state) {
        switch (state) {
            case WifiManager::State::CONNECTED:
                LOG_INFO("WiFi: Connected!");
                break;
            case WifiManager::State::DISCONNECTED:
                LOG_INFO("WiFi: Disconnected");
                break;
            case WifiManager::State::CONNECTING:
                LOG_INFO("WiFi: Connecting...");
                break;
            case WifiManager::State::FAILED:
                LOG_WARN("WiFi: Connection failed");
                break;
            case WifiManager::State::AP_ACTIVE:
                LOG_INFO("WiFi: Access Point active");
                break;
        }
    });

    // Auto-connect if credentials are saved, otherwise start AP mode
    if (configManager.hasWifiCredentials()) {
        LOG_INFO("Attempting to connect to saved network: %s", wifiConfig.ssid.c_str());
        wifiManager.connect(wifiConfig.ssid, wifiConfig.password);
    } else {
        LOG_INFO("No WiFi credentials saved - starting Access Point mode");
        LOG_INFO("Connect to the AP and configure WiFi via web interface");
        wifiManager.startAccessPoint();
    }

    // Initialize web server
    LOG_INFO("[5/5] Starting web server...");
    webServer.begin(&wifiManager, &configManager, extron, tink);
    webServer.setLEDCallback(setLEDColor);

    LOG_RAW("\n");
    LOG_RAW("========================================\n");
    LOG_RAW("  Initialization complete!\n");
    LOG_RAW("========================================\n");
    LOG_RAW("\n");
    LOG_INFO("Pin assignments:");
    LOG_INFO("  Switcher TX:  GPIO%d", switcherConfig.txPin);
    LOG_INFO("  Switcher RX:  GPIO%d", switcherConfig.rxPin);
    LOG_INFO("  RGB LED:      GPIO%d", RGB_LED_PIN);
    LOG_INFO("USB Mode: CDC (Serial debugging enabled)");
    LOG_INFO("Note: USB Host for RetroTINK not yet implemented");
    if (wifiManager.isAPActive()) {
        LOG_INFO("Web interface: http://%s", wifiManager.getIP().c_str());
    } else {
        LOG_INFO("Web interface: http://tinklink.local");
    }
}

void loop() {
    // Update WiFi connection state
    wifiManager.update();

    // Process incoming Extron messages
    extron->update();

    // Check for manual LED mode timeout
    unsigned long now = millis();
    if (ledManualMode && (now - ledManualModeStart >= LED_MANUAL_TIMEOUT)) {
        ledManualMode = false;
        LOG_DEBUG("LED: Manual mode timeout - returning to WiFi state indication");

        // Force LED update to current WiFi state
        leds[0] = getColorForWifiState(wifiManager.getState());
        FastLED.show();
    }

    // WS2812 RGB LED status indication (skip if in manual mode)
    if (!ledManualMode) {
        static unsigned long lastBlink = 0;
        static bool ledOn = false;
        static WifiManager::State lastState = WifiManager::State::DISCONNECTED;
        WifiManager::State currentState = wifiManager.getState();

        // Only update LED when state changes or for blinking states
        if (currentState == WifiManager::State::AP_ACTIVE) {
            // Blink blue in AP mode (500ms interval)
            if (now - lastBlink >= 500) {
                lastBlink = now;
                ledOn = !ledOn;
                leds[0] = ledOn ? CRGB::Blue : CRGB::Black;
                FastLED.show();
            }
        } else if (lastState != currentState) {
            // State changed - update LED to new state color
            leds[0] = getColorForWifiState(currentState);
            FastLED.show();
        }

        lastState = currentState;
    }
}
