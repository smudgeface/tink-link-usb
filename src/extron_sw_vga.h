#ifndef EXTRON_SW_VGA_H
#define EXTRON_SW_VGA_H

#include <Arduino.h>
#include <functional>

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

private:
    uint8_t _txPin;
    uint8_t _rxPin;
    uint32_t _baud;

    String _lineBuffer;
    int _currentInput;

    InputChangeCallback _inputCallback;

    void processLine(const String& line);
    bool isInputMessage(const String& line);
    int parseInputNumber(const String& line);
};

#endif // EXTRON_SW_VGA_H
