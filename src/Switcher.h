#ifndef SWITCHER_H
#define SWITCHER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>

/**
 * Abstract base class for video switchers.
 *
 * Provides a common interface for different video switcher types:
 * - ExtronSwVgaSwitcher (Extron SW series via RS-232)
 * - Future: Other switcher brands/models
 *
 * Concrete implementations handle their own serial transport creation
 * via configure(), allowing each switcher type to use the appropriate
 * transport (UART, USB, network, etc.).
 *
 * Usage:
 *   Switcher* sw = SwitcherFactory::create("Extron SW VGA");
 *   sw->configure(config);
 *   sw->begin();
 *   sw->onInputChange([](int input) { ... });
 *   // In loop():
 *   sw->update();
 */
class Switcher {
public:
    /** Callback type for input change notifications */
    using InputChangeCallback = std::function<void(int input)>;

    virtual ~Switcher() = default;

    /**
     * Configure the switcher from JSON settings.
     * Each concrete switcher reads its own type-specific fields.
     * Creates and configures the appropriate serial transport internally.
     * @param config JSON object containing switcher configuration
     */
    virtual void configure(const JsonObject& config) = 0;

    /**
     * Initialize the switcher and its transport.
     * Must be called after configure() and before update().
     * @return true on success
     */
    virtual bool begin() = 0;

    /** Stop the switcher and release resources. */
    virtual void end() = 0;

    /**
     * Process incoming data and events. Must be called regularly from loop().
     * Parses messages and triggers callbacks on input changes.
     */
    virtual void update() = 0;

    /**
     * Register callback for input change events.
     * Callback receives the new input number (typically 1-based).
     * @param callback Function called when input changes
     */
    virtual void onInputChange(InputChangeCallback callback) = 0;

    /**
     * Get the most recently detected input.
     * @return Input number, or 0 if no input detected yet
     */
    virtual int getCurrentInput() const = 0;

    /**
     * Send a command string to the switcher.
     * Format depends on switcher protocol.
     * @param cmd Command string
     */
    virtual void sendCommand(const char* cmd) = 0;

    /**
     * Get recent received messages for debugging.
     * @param count Maximum messages to return (default 10)
     * @return Vector of message strings, oldest first
     */
    virtual std::vector<String> getRecentMessages(int count = 10) = 0;

    /** Clear the received message history. */
    virtual void clearRecentMessages() = 0;

    /**
     * Get the human-readable type name for this switcher.
     * @return Type name string (e.g., "Extron SW VGA")
     */
    virtual const char* getTypeName() const = 0;

    /**
     * Enable or disable signal-based auto-switching.
     * Behavior depends on switcher capabilities.
     * @param enabled true to enable auto-switching
     */
    virtual void setAutoSwitchEnabled(bool enabled) = 0;

    /**
     * Check if signal-based auto-switching is enabled.
     * @return true if auto-switching is enabled
     */
    virtual bool isAutoSwitchEnabled() const = 0;
};

#endif // SWITCHER_H
