#ifndef EXTRON_SW_VGA_H
#define EXTRON_SW_VGA_H

#include <Arduino.h>
#include <functional>
#include <vector>

/**
 * Extron SW series VGA switcher serial protocol handler.
 *
 * Monitors UART for input change messages from Extron switchers.
 * Message format: "In<N> All" or "In<N> Vid" where N is input number.
 *
 * Uses UART0 at 9600 baud, 8N1 to match Extron RS-232 settings.
 * Requires RS-232 level shifter between ESP32 (3.3V) and Extron (RS-232 levels).
 *
 * Usage:
 *   ExtronSwVga extron(43, 44, 9600);  // TX, RX, baud
 *   extron.begin();
 *   extron.onInputChange([](int input) { ... });
 *   // In loop():
 *   extron.update();  // Process incoming UART data
 */
class ExtronSwVga {
public:
    /** Callback type for input change notifications */
    using InputChangeCallback = std::function<void(int input)>;

    /**
     * Create Extron switcher handler.
     * @param txPin UART TX GPIO pin number
     * @param rxPin UART RX GPIO pin number
     * @param baud Baud rate (typically 9600 for Extron)
     */
    ExtronSwVga(uint8_t txPin, uint8_t rxPin, uint32_t baud = 9600);

    ~ExtronSwVga();

    /**
     * Initialize UART and start listening.
     * @return true on success
     */
    bool begin();

    /** Stop UART and release resources. */
    void end();

    /**
     * Process incoming UART data. Must be called regularly from loop().
     * Parses complete lines and triggers callbacks on input changes.
     */
    void update();

    /**
     * Register callback for input change events.
     * Callback receives the new input number (1-based).
     * @param callback Function called when input changes
     */
    void onInputChange(InputChangeCallback callback);

    /**
     * Get the most recently detected input.
     * @return Input number (1-based), or 0 if no input detected yet
     */
    int getCurrentInput() const { return _currentInput; }

    /**
     * Send a command string to the Extron switcher.
     * Automatically appends CR+LF terminator.
     * @param cmd Command string (without terminator)
     */
    void sendCommand(const char* cmd);

    /**
     * Get recent received messages for debugging.
     * @param count Maximum messages to return (default 10)
     * @return Vector of message strings, oldest first
     */
    std::vector<String> getRecentMessages(int count = 10);

    /** Clear the received message history. */
    void clearRecentMessages();

    /**
     * Get the human-readable type name for this switcher.
     * @return Type name string (e.g., "Extron SW VGA")
     */
    static const char* getTypeName() { return "Extron SW VGA"; }

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
