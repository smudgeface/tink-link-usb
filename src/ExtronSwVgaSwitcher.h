#ifndef EXTRON_SW_VGA_SWITCHER_H
#define EXTRON_SW_VGA_SWITCHER_H

#include <Arduino.h>
#include <functional>
#include <vector>
#include "Switcher.h"

// Forward declaration
class SerialInterface;

/**
 * Extron SW series VGA switcher implementation.
 *
 * Monitors serial interface for input change messages from Extron switchers.
 * Message format: "In<N> All" or "In<N> Vid" where N is input number.
 *
 * Uses UART at 9600 baud, 8N1 to match Extron RS-232 settings.
 * Requires RS-232 level shifter between ESP32 (3.3V) and Extron (RS-232 levels).
 *
 * Usage:
 *   Switcher* sw = new ExtronSwVgaSwitcher();
 *   sw->configure(config);  // Reads uartId, txPin, rxPin, autoSwitch from JSON
 *   sw->begin();
 *   sw->onInputChange([](int input) { ... });
 *   // In loop():
 *   sw->update();  // Process incoming serial data
 */
class ExtronSwVgaSwitcher : public Switcher {
public:
    /**
     * Create Extron switcher handler.
     * Call configure() to set up pins and transport.
     */
    ExtronSwVgaSwitcher();

    ~ExtronSwVgaSwitcher();

    // Switcher interface overrides
    void configure(const JsonObject& config) override;
    bool begin() override;
    void end() override;
    void update() override;
    void onInputChange(InputChangeCallback callback) override;
    int getCurrentInput() const override { return _currentInput; }
    void sendCommand(const char* cmd) override;
    std::vector<String> getRecentMessages(int count = 10) override;
    void clearRecentMessages() override;
    const char* getTypeName() const override { return "Extron SW VGA"; }
    void setAutoSwitchEnabled(bool enabled) override { _autoSwitchEnabled = enabled; }
    bool isAutoSwitchEnabled() const override { return _autoSwitchEnabled; }

private:
    SerialInterface* _serial;
    int _currentInput;

    InputChangeCallback _inputCallback;

    // Store recent messages for debugging (circular buffer)
    static const int MAX_RECENT_MESSAGES = 50;
    std::vector<String> _recentMessages;

    // Signal detection auto-switch
    static const int MAX_SIG_INPUTS = 16;
    static const unsigned long SIG_DEBOUNCE_MS = 2000;
    bool _autoSwitchEnabled;
    bool _signalWasLost;                  ///< True if stable state went to all-zero
    int _lastSigState[MAX_SIG_INPUTS];    ///< Most recently parsed signal state
    int _stableSigState[MAX_SIG_INPUTS];  ///< Last signal state we acted on
    int _numSigInputs;                    ///< Number of inputs in Sig messages
    unsigned long _sigChangeTime;         ///< When _lastSigState last changed

    void processLine(const String& line);
    bool isInputMessage(const String& line);
    int parseInputNumber(const String& line);
    bool isSigMessage(const String& line);
    void parseSigMessage(const String& line);
    void processAutoSwitch();
};

#endif // EXTRON_SW_VGA_SWITCHER_H
