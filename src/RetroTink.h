#ifndef RETROTINK_H
#define RETROTINK_H

#include <Arduino.h>
#include <vector>

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
 * RetroTINK 4K controller via USB serial.
 *
 * Currently a stub implementation that logs commands without sending.
 * Phase 3 will implement actual USB Host FTDI communication.
 *
 * The RetroTINK 4K accepts commands at 115200 baud over its USB-C port,
 * which appears as an FTDI FT232R device (VID:0403, PID:6001).
 *
 * Supported command formats:
 * - "remote profN" - Emulate IR remote profile button
 * - "SVS NEW INPUT=N" - SVS protocol for profile switching
 *
 * Usage:
 *   RetroTink tink;
 *   tink.begin();
 *   tink.addTrigger({1, TriggerMapping::SVS, 1, "Console 1"});
 *   tink.onExtronInputChange(1);  // Sends "SVS NEW INPUT=1"
 */
class RetroTink {
public:
    RetroTink();
    ~RetroTink();

    /**
     * Initialize the RetroTINK controller.
     * In Phase 3, this will initialize USB Host.
     */
    void begin();

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
     * Looks up the trigger for the input and sends the corresponding command.
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
     * Send repeated test signals for oscilloscope testing.
     * Useful for verifying USB serial timing.
     * @param count Number of test signals to send (default 10)
     */
    void sendContinuousTest(int count = 10);

    /**
     * Get the last command that was sent (or would be sent in stub mode).
     * @return The last command string
     */
    String getLastCommand() const { return _lastCommand; }

private:
    std::vector<TriggerMapping> _triggers;
    String _lastCommand;

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
     * Send a command to the RetroTINK.
     * Currently logs to debug output (stub mode).
     * Phase 3 will send via USB Host FTDI.
     * @param command The command to send
     */
    void sendCommand(const String& command);
};

#endif // RETROTINK_H
