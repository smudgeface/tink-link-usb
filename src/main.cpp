#include <Arduino.h>
#include <FastLED.h>
#include "ConfigManager.h"
#include "Switcher.h"
#include "SwitcherFactory.h"
#include "RetroTink.h"
#include "DenonAvr.h"
#include "WifiManager.h"
#include "WebServer.h"
#include "Logger.h"
#include "version.h"

// WS2812 RGB LED configuration (loaded from config.json)
#define NUM_LEDS 1

// FastLED array
CRGB leds[NUM_LEDS];

// LED pin (set during setup from config)
uint8_t ledPin = 21;  // Default for Waveshare ESP32-S3-Zero

// Global instances
ConfigManager configManager;
Switcher* switcher = nullptr;
RetroTink* tink = nullptr;
DenonAvr* avr = nullptr;
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
    // USB is in OTG mode - no CDC serial available
    // Disable serial output in logger since there's no CDC
    Logger::instance().setSerialEnabled(false);

    // Initialize logger
    Logger::instance().begin();

    LOG_RAW("\n");
    LOG_RAW("========================================\n");
    LOG_INFO("  TinkLink-USB v%s", TINKLINK_VERSION_STRING);
    LOG_RAW("  ESP32-S3 RetroTINK 4K Controller\n");
    LOG_RAW("========================================\n");
    LOG_RAW("\n");

    // Initialize configuration manager (LittleFS) - load before hardware init
    LOG_INFO("[1/6] Initializing configuration...");
    if (!configManager.begin()) {
        LOG_ERROR("Failed to initialize configuration manager!");
    }

    // Get configurations
    auto hardwareConfig = configManager.getHardwareConfig();
    auto wifiConfig = configManager.getWifiConfig();

    // Store LED pin from config
    ledPin = hardwareConfig.ledPin;
    String ledColorOrder = hardwareConfig.ledColorOrder;

    // Initialize WS2812 LED using configured pin and color order.
    // FastLED requires compile-time constants for both pin and color order,
    // so we enumerate supported combinations. Add new pins as needed.
    if (ledColorOrder == "RGB") {
        switch (ledPin) {
            case 8:  FastLED.addLeds<WS2812, 8, RGB>(leds, NUM_LEDS); break;
            case 21: FastLED.addLeds<WS2812, 21, RGB>(leds, NUM_LEDS); break;
            default:
                LOG_ERROR("LED pin %d not supported. Add it to the FastLED switch in main.cpp.", ledPin);
                break;
        }
    } else {
        // Default to GRB (most common WS2812 order)
        switch (ledPin) {
            case 8:  FastLED.addLeds<WS2812, 8, GRB>(leds, NUM_LEDS); break;
            case 21: FastLED.addLeds<WS2812, 21, GRB>(leds, NUM_LEDS); break;
            default:
                LOG_ERROR("LED pin %d not supported. Add it to the FastLED switch in main.cpp.", ledPin);
                break;
        }
    }
    FastLED.setBrightness(50);  // 0-255, moderate brightness
    leds[0] = CRGB::Black;
    FastLED.show();

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

    // Initialize RetroTINK controller
    LOG_INFO("[2/6] Initializing RetroTINK controller...");
    tink = new RetroTink();
    tink->configure(configManager.getRetroTinkConfig());
    tink->begin();

    // Load triggers from config
    for (const auto& trigger : configManager.getTriggers()) {
        tink->addTrigger(trigger);
    }

    // Initialize AVR controller if enabled
    LOG_INFO("[3/6] Initializing AVR controller...");
    if (configManager.isAvrEnabled()) {
        avr = new DenonAvr();
        avr->configure(configManager.getAvrConfig());
        avr->begin();
    } else {
        LOG_INFO("AVR control disabled");
    }

    // Initialize video switcher
    auto switcherType = configManager.getSwitcherType();
    LOG_INFO("[4/6] Initializing %s...", switcherType.c_str());
    switcher = SwitcherFactory::create(switcherType);
    if (switcher) {
        switcher->configure(configManager.getSwitcherConfig());
        if (!switcher->begin()) {
            LOG_ERROR("Failed to initialize switcher!");
        }

        // Connect switcher input changes to RetroTINK and AVR
        switcher->onInputChange([](int input) {
            LOG_INFO("Input change detected: %d", input);
            tink->onSwitcherInputChange(input);
            if (avr) avr->onInputChange();
        });
    } else {
        LOG_ERROR("Unknown switcher type: %s", switcherType.c_str());
    }

    // Initialize WiFi manager
    LOG_INFO("[5/6] Initializing WiFi...");
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
    LOG_INFO("[6/6] Starting web server...");
    webServer.begin(&wifiManager, &configManager, switcher, tink, avr);
    webServer.setLEDCallback(setLEDColor);

    LOG_RAW("\n");
    LOG_RAW("========================================\n");
    LOG_RAW("  Initialization complete!\n");
    LOG_RAW("========================================\n");
    LOG_RAW("\n");
    LOG_INFO("Pin assignments:");
    if (switcher) {
        auto switcherConfig = configManager.getSwitcherConfig();
        LOG_INFO("  Switcher TX:  GPIO%d", switcherConfig["txPin"] | 43);
        LOG_INFO("  Switcher RX:  GPIO%d", switcherConfig["rxPin"] | 44);
    }
    auto tinkConfig = configManager.getRetroTinkConfig();
    String tinkMode = tinkConfig["serialMode"] | "usb";
    if (tinkMode == "uart") {
        LOG_INFO("  Tink TX:      GPIO%d", tinkConfig["txPin"] | 17);
        LOG_INFO("  Tink RX:      GPIO%d", tinkConfig["rxPin"] | 18);
    } else {
        LOG_INFO("  USB Host:     GPIO19 (D-) / GPIO20 (D+)");
    }
    LOG_INFO("  RGB LED:      GPIO%d", ledPin);
    LOG_INFO("RetroTINK serial: %s", tinkMode.c_str());
    LOG_INFO("Serial debugging: disabled (use web console or scripts/logs.py)");
    if (wifiManager.isAPActive()) {
        LOG_INFO("Web interface: http://%s", wifiManager.getIP().c_str());
    } else {
        String hostname = configManager.getWifiConfig().hostname;
        LOG_INFO("Web interface: http://%s.local", hostname.c_str());
    }
}

void loop() {
    // Update WiFi connection state
    wifiManager.update();

    // Process incoming switcher messages
    if (switcher) switcher->update();

    // Process USB Host events and RT4K communication
    tink->update();

    // Process AVR commands and responses
    if (avr) avr->update();

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
