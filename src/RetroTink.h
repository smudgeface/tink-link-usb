#ifndef RETROTINK_H
#define RETROTINK_H

#include <Arduino.h>
#include <vector>

class UsbHostSerial;

/**
 * Mapping from Extron input number to RetroTINK 4K profile.
 *
 * When the specified Extron input is selected, the corresponding
 * RetroTINK command is sent to switch profiles.
 */
struct TriggerMapping {
    int extronInput;  ///< Extron input number (1-based)

    /**
     * Command mode for RetroTINK profile switching.
     * - SVS: Uses "SVS NEW INPUT=N" command (preferred, loads S<N>_*.rt4 profile)
     * - REMOTE: Uses "remote profN" command (emulates IR remote button)
     */
    enum Mode { SVS, REMOTE } mode;

    int profile;   ///< Target profile number (1-12)
    String name;   ///< Human-readable name for this trigger (for UI display)
};

/**
 * RetroTINK 4K power state.
 *
 * Tracked by parsing serial messages from the RT4K:
 * - "[MCU] Powering Up" -> BOOTING
 * - "[MCU] Boot Sequence Complete..." -> ON
 * - "Power Off" or "Entering Sleep" -> SLEEPING
 */
enum class RT4KPowerState {
    UNKNOWN,   ///< Initial state or unable to determine
    WAKING,    ///< pwr on sent from UNKNOWN state, waiting briefly for RT4K response
    BOOTING,   ///< RT4K confirmed powering up, waiting for boot complete
    ON,        ///< Fully booted and ready for commands
    SLEEPING   ///< In sleep/standby mode
};

/**
 * RetroTINK 4K controller via USB Host FTDI serial.
 *
 * Communicates with the RetroTINK 4K over its USB-C port, which appears
 * as an FTDI FT232R device (VID:0x0403, PID:0x6001) at 115200 baud 8N1.
 *
 * Features:
 * - Profile switching via SVS or remote commands
 * - Power state tracking (parses RT4K serial output)
 * - Auto-wake: powers on RT4K when input changes during sleep
 * - SVS keep-alive: sends "SVS CURRENT INPUT=N" after initial switch
 *
 * Command framing: "\r<COMMAND>\r"
 * - Leading CR clears any partial input in RT4K's buffer
 * - Trailing CR terminates the command
 *
 * Usage:
 *   UsbHostSerial usb;
 *   RetroTink tink(&usb);
 *   tink.begin();
 *   tink.addTrigger({1, TriggerMapping::SVS, 1, "Console 1"});
 *   // In loop():
 *   tink.update();
 *   tink.onExtronInputChange(1);  // Sends "SVS NEW INPUT=1"
 */
class RetroTink {
public:
    /**
     * Create RetroTINK controller with USB Host serial.
     * @param usb Pointer to UsbHostSerial instance (nullptr for stub mode)
     */
    explicit RetroTink(UsbHostSerial* usb = nullptr);
    ~RetroTink();

    /**
     * Initialize the RetroTINK controller.
     * Logs USB Host or stub mode status.
     */
    void begin();

    /**
     * Process USB serial data and pending commands.
     * Must be called in loop(). Handles:
     * - Reading and parsing incoming RT4K serial data
     * - Sending pending commands after boot completes
     * - SVS keep-alive timing
     */
    void update();

    /**
     * Add a trigger mapping from Extron input to RetroTINK profile.
     * @param trigger The input-to-profile mapping to add
     */
    void addTrigger(const TriggerMapping& trigger);

    /**
     * Clear all trigger mappings.
     */
    void clearTriggers();

    /**
     * Handle an Extron input change event.
     * If RT4K is sleeping, sends power-on first and queues the profile command.
     * If RT4K is on, sends the profile command immediately.
     * @param input The new Extron input number (1-based)
     */
    void onExtronInputChange(int input);

    /**
     * Send a raw command string to RetroTINK.
     * Useful for testing from the debug web interface.
     * @param command The command to send (e.g., "remote prof1")
     */
    void sendRawCommand(const String& command);

    /**
     * Check whether the RT4K USB device is connected.
     * @return true if FTDI device is detected and ready
     */
    bool isConnected() const;

    /**
     * Get the current RT4K power state.
     * @return Current power state enum value
     */
    RT4KPowerState getPowerState() const { return _powerState; }

    /**
     * Get the power state as a human-readable string.
     * @return "unknown", "booting", "on", or "sleeping"
     */
    const char* getPowerStateString() const;

    /**
     * Get the last command that was sent (or would be sent in stub mode).
     * @return The last command string
     */
    String getLastCommand() const { return _lastCommand; }

private:
    UsbHostSerial* _usb;
    std::vector<TriggerMapping> _triggers;
    String _lastCommand;

    // Power state tracking
    RT4KPowerState _powerState;
    String _serialLineBuffer;

    // Pending command (queued during boot)
    String _pendingCommand;
    unsigned long _bootWaitStart;
    static const unsigned long BOOT_TIMEOUT_MS = 15000;
    static const unsigned long WAKE_RESPONSE_TIMEOUT_MS = 3000;

    // SVS keep-alive
    int _lastSvsInput;
    unsigned long _svsKeepAliveTime;
    bool _svsKeepAlivePending;
    static const unsigned long SVS_KEEPALIVE_DELAY_MS = 1000;

    /**
     * Find the trigger mapping for a given Extron input.
     * @param input The input number to look up
     * @return Pointer to the trigger, or nullptr if not found
     */
    const TriggerMapping* findTrigger(int input) const;

    /**
     * Generate the command string for a trigger.
     * @param trigger The trigger mapping
     * @return Command string (e.g., "SVS NEW INPUT=1" or "remote prof1")
     */
    String generateCommand(const TriggerMapping& trigger) const;

    /**
     * Send a framed command to the RetroTINK via USB.
     * Frames as "\r<command>\r" for proper RT4K parsing.
     * Falls back to stub logging if USB is not available.
     * @param command The command to send
     */
    void sendCommand(const String& command);

    /**
     * Process a complete line received from the RT4K serial output.
     * Updates power state based on known status messages.
     * @param line The received line (without terminator)
     */
    void processReceivedLine(const String& line);

    /**
     * Read and process incoming serial data from the RT4K.
     * Assembles characters into lines and calls processReceivedLine().
     */
    void processIncomingData();

    /**
     * Handle pending operations: boot timeout, SVS keep-alive.
     */
    void processPendingOperations();
};

#endif // RETROTINK_H
