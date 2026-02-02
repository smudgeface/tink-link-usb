#ifndef EXTRON_SW_VGA_H
#define EXTRON_SW_VGA_H

#include <Arduino.h>
#include <functional>
#include <vector>

// Extron SW VGA switcher handler
// Monitors serial output for input change messages
// Message format: "In3 All" or "In10 Vid"

class ExtronSwVga {
public:
    using InputChangeCallback = std::function<void(int input)>;

    ExtronSwVga(uint8_t txPin = 21, uint8_t rxPin = 20, uint32_t baud = 9600);
    ~ExtronSwVga();

    bool begin();
    void end();

    // Process incoming data - call from loop()
    void update();

    // Set callback for input changes
    void onInputChange(InputChangeCallback callback);

    // Get current input (0 if unknown)
    int getCurrentInput() const { return _currentInput; }

    // Send command to switcher
    void sendCommand(const char* cmd);

    // Get recent received messages (for debugging)
    std::vector<String> getRecentMessages(int count = 10);
    void clearRecentMessages();

private:
    uint8_t _txPin;
    uint8_t _rxPin;
    uint32_t _baud;

    String _lineBuffer;
    int _currentInput;

    InputChangeCallback _inputCallback;

    // Store recent messages for debugging (circular buffer)
    static const int MAX_RECENT_MESSAGES = 50;
    std::vector<String> _recentMessages;

    void processLine(const String& line);
    bool isInputMessage(const String& line);
    int parseInputNumber(const String& line);
};

#endif // EXTRON_SW_VGA_H
