#ifndef RETROTINK_H
#define RETROTINK_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

class SerialInterface;

/**
 * Mapping from video switcher input number to RetroTINK 4K profile.
 *
 * When the specified switcher input is selected, the corresponding
 * RetroTINK command is sent to switch profiles.
 */
struct TriggerMapping {
    int switcherInput;  ///< Switcher input number (1-based)

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
 * RetroTINK 4K power state (FULL power management mode).
 *
 * RT4K firmware 1.75+ answers every command with a "[COM] " line while it
 * is on, and while asleep ignores everything except "pwr on". Its old
 * unsolicited status text ("[MCU] Powering Up" etc.) is no longer sent over
 * serial. The state therefore follows the replies:
 * - "[COM] Power On Requested" (reply to "pwr on" while asleep) -> BOOTING
 * - any other "[COM] " reply, including "Bad Command: pwr on" -> ON
 * - "[COM] Serial Remote: pwr", a serial line break, or no reply to a
 *   status probe -> SLEEPING
 * The status text of older firmware is still understood:
 * - "Powering Up" -> BOOTING, "Boot Sequence Complete" -> ON,
 *   "Power Off" / "Entering Sleep" -> SLEEPING
 */
enum class RT4KPowerState {
    UNKNOWN,   ///< Not determined yet, or the RT4K can't be reached
    WAKING,    ///< "pwr on" sent, waiting for the RT4K to say whether it was asleep
    BOOTING,   ///< RT4K confirmed waking up, waiting for it to accept commands
    ON,        ///< Booted and answering commands
    SLEEPING   ///< In sleep/standby mode
};

/**
 * Power management mode for RetroTINK communication.
 *
 * Controls how TinkLink handles RT4K power state:
 * - OFF: No power management. Commands sent immediately. User must ensure
 *   RT4K is on. Best when the RT4K is always on and power management is
 *   not needed.
 * - SIMPLE: On first input change, sends "pwr on" and waits 15 seconds
 *   before sending the profile command. All subsequent commands are sent
 *   immediately. Suitable when power state messages are not available.
 * - FULL: Every input change first sends "pwr on", which wakes a sleeping
 *   RT4K and reveals its power state through the reply; the profile command
 *   follows as soon as the RT4K is known to accept it (immediately if it
 *   was on, about 5 s later if it had to boot). Between input changes a
 *   periodic "ver" probe keeps the reported state current. Default mode.
 */
enum class PowerManagementMode {
    OFF,     ///< No power management, always send commands immediately
    SIMPLE,  ///< One-time "pwr on" + 15s wait on first use, then immediate
    FULL     ///< Full state tracking via serial messages
};

/**
 * RetroTINK 4K controller via serial interface.
 *
 * Communicates with the RetroTINK 4K over a SerialInterface transport.
 * Supports two modes:
 * - USB: USB FTDI (default) at 2,000,000 baud 8N1 (RT4K firmware 1.75+ default)
 * - UART: Hardware UART (HD-15) at 115200 baud 8N1
 * Either rate can be overridden with the "baudRate" config field.
 *
 * Features:
 * - Profile switching via SVS or remote commands
 * - Power state tracking (from the RT4K's command replies)
 * - Auto-wake: powers on RT4K when input changes during sleep, and sends
 *   the profile command once the RT4K has booted
 * - SVS keep-alive: sends "SVS CURRENT INPUT=N" after initial switch
 *
 * Command framing: "\r<COMMAND>\r"
 * - Leading CR clears any partial input in RT4K's buffer
 * - Trailing CR terminates the command
 *
 * RT4K firmware 1.75+ replies to each command, e.g. "[COM] Serial Remote: prof1"
 * or "[COM] Bad Command: <text>". These replies are logged and otherwise ignored.
 *
 * Usage:
 *   RetroTink tink;
 *   tink.configure(config);  // config contains serialMode, uartId, txPin, rxPin
 *   tink.begin();
 *   tink.addTrigger({1, TriggerMapping::SVS, 1, "Console 1"});
 *   // In loop():
 *   tink.update();
 *   tink.onSwitcherInputChange(1);  // Sends "SVS NEW INPUT=1"
 */
class RetroTink {
public:
    /**
     * Create RetroTINK controller.
     * Call configure() to set up the serial transport.
     */
    RetroTink();
    ~RetroTink();

    /**
     * Configure the RetroTINK controller from JSON settings.
     * Creates either USB Host or UART serial transport based on config.
     *
     * Config fields:
     * - serialMode: "usb" (default) or "uart"
     * - powerManagementMode: "off", "simple", or "full" (default "full")
     * - uartId: UART number (for uart mode, default 2)
     * - txPin: TX GPIO pin (for uart mode, default 17)
     * - rxPin: RX GPIO pin (for uart mode, default 18)
     * - baudRate: Optional baud rate override (default 2000000 for usb,
     *   115200 for uart). Unsupported USB rates fall back to 2000000.
     *
     * @param config JSON object containing RetroTINK configuration
     */
    void configure(const JsonObject& config);

    /**
     * Initialize the RetroTINK controller and its transport.
     * Must be called after configure() and before update().
     * @return true on success
     */
    bool begin();

    /**
     * Process serial data and pending commands.
     * Must be called in loop(). Handles:
     * - Reading and parsing incoming RT4K serial data
     * - Sending pending commands after boot completes
     * - SVS keep-alive timing
     */
    void update();

    /**
     * Add a trigger mapping from switcher input to RetroTINK profile.
     * @param trigger The input-to-profile mapping to add
     */
    void addTrigger(const TriggerMapping& trigger);

    /**
     * Clear all trigger mappings.
     */
    void clearTriggers();

    /**
     * Handle a video switcher input change event.
     * If RT4K is sleeping, sends power-on first and queues the profile command.
     * If RT4K is on, sends the profile command immediately.
     * @param input The new switcher input number (1-based)
     */
    void onSwitcherInputChange(int input);

    /**
     * Run the trigger mapped to a switcher input, as if the switcher had
     * just selected it (including waking the RT4K if needed). Safe to call
     * from any task; the trigger runs on the next update().
     * @param input Switcher input number (1-based)
     * @return false if no trigger is mapped to the input
     */
    bool requestTrigger(int input);

    /**
     * Send a raw command string to RetroTINK.
     * Useful for testing from the debug web interface.
     * @param command The command to send (e.g., "remote prof1")
     */
    void sendRawCommand(const String& command);

    /**
     * Check whether the RT4K serial connection is active.
     * @return true if serial interface is connected and ready
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
    String getLastCommand() const;

    /**
     * Change the serial baud rate at runtime (not persisted).
     * USB rates must satisfy UsbHostSerial::isSupportedBaud(); UART accepts
     * UartSerial::MIN_BAUD to UartSerial::MAX_BAUD.
     * @param baud New baud rate
     * @return true if the transport accepted the rate
     */
    bool setBaudRate(uint32_t baud);

    /** @return Current serial baud rate, or 0 if no transport is configured */
    uint32_t getBaudRate() const;

    /**
     * @return Default baud rate for the configured serial mode
     *         (2,000,000 for USB, 115,200 for UART)
     */
    uint32_t getDefaultBaudRate() const { return _defaultBaudRate; }

    /**
     * Open the raw byte channel to the RT4K, giving the caller exclusive use
     * of the serial link (used by the Retro-Bridge API). While open:
     * - received bytes are captured verbatim (binary safe) for rawRead()
     * - clean text lines in the stream still drive power state tracking
     * - TinkLink's own commands (triggers, keep-alives, /api/tink/send) are
     *   held back and sent when the channel closes, so they can't corrupt a
     *   binary transfer
     * Safe to call from any task.
     * @return false if the channel is already open or no serial transport exists
     */
    bool rawOpen();

    /** Close the raw byte channel, resume line processing and send held-back commands. */
    void rawClose();

    /** @return true while the raw byte channel is open */
    bool isRawOpen() const { return _rawOpen; }

    /**
     * Queue bytes for transmission to the RT4K exactly as given (no framing).
     * @param data Bytes to send
     * @param length Number of bytes
     * @return Number of bytes accepted by the transport
     */
    size_t rawWrite(const uint8_t* data, size_t length);

    /**
     * Read captured bytes starting at a stream position. The capture is a
     * sliding window over everything received since rawOpen(); position 0 is
     * the first byte received.
     * @param cursor Stream position to read from
     * @param buf Destination buffer
     * @param maxLen Maximum bytes to read
     * @param overflow Set to true if bytes at the cursor were already
     *        discarded (the read then resumes at the oldest byte still held)
     * @param nextCursor Set to the stream position following the returned bytes
     * @return Number of bytes copied into buf
     */
    size_t rawRead(uint32_t cursor, uint8_t* buf, size_t maxLen, bool& overflow, uint32_t& nextCursor);

    /**
     * @return Size of the raw channel's capture window in bytes. Received
     *         data that is not read before this much more arrives is lost.
     */
    size_t rawCapacity() const { return _rawCapacity; }

    /** @return Total bytes captured since rawOpen() */
    uint32_t rawTotal() const { return _rawTotal; }

    /** @return millis() timestamp of the last captured byte (or of rawOpen() if none yet) */
    unsigned long rawLastRxTime() const { return _rawLastRx; }

    /** @return The serial transport (for diagnostics), or nullptr if not configured */
    SerialInterface* getSerial() const { return _serial; }

private:
    SerialInterface* _serial;
    uint32_t _defaultBaudRate;

    // After a baud rate change, a lone CR is sent once the new rate is in
    // effect so bytes the RT4K received at a mismatched rate are discarded
    // instead of corrupting the next command
    volatile unsigned long _baudFlushAt;
    static const unsigned long BAUD_FLUSH_DELAY_MS = 100;
    std::vector<TriggerMapping> _triggers;
    String _lastCommand;

    // Raw byte channel: sliding capture window written by the loop task and
    // read by web server tasks, guarded by _rawMutex
    volatile bool _rawOpen;
    uint8_t* _rawBuffer;
    size_t _rawCapacity;
    volatile uint32_t _rawTotal;
    volatile unsigned long _rawLastRx;
    SemaphoreHandle_t _rawMutex;
    String _rawLineBuffer;                  // Text line being assembled from the raw stream
    bool _rawLineClean;                     // false once the line contains non-text bytes
    std::vector<String> _deferredCommands;  // Commands held back while the channel is open
    static const size_t RAW_LINE_MAX = 200;
    static const size_t DEFERRED_COMMANDS_MAX = 8;
    static const size_t RAW_CAPTURE_PSRAM_BYTES = 1048576;
    static const size_t RAW_CAPTURE_HEAP_BYTES = 16384;

    // Power management
    PowerManagementMode _powerMgmtMode;
    RT4KPowerState _powerState;
    String _serialLineBuffer;

    // Pending command (queued until the RT4K is known to be on)
    String _pendingCommand;
    unsigned long _bootWaitStart;
    volatile int _requestedInput; // Input whose trigger was requested via requestTrigger() (0 = none)
    volatile bool _wakePending;   // An input change is waiting for startWake()
    bool _bootProbing;            // BOOTING on firmware 1.75+: poll until it answers
    bool _comReplySeen;           // Firmware answers commands (1.75+)
    bool _wasConnected;
    unsigned long _nextBootProbe;
    unsigned long _nextIdleProbe;
    unsigned long _probeSentAt;     // Status probe awaiting a reply (0 = none)
    unsigned long _probeQuietUntil; // Probe replies before this time aren't logged
    uint32_t _lastBreakCount;
    static const unsigned long BOOT_TIMEOUT_MS = 15000;
    static const unsigned long WAKE_RESPONSE_TIMEOUT_MS = 3000;
    static const unsigned long BOOT_PROBE_DELAY_MS = 2000;     // Boot takes ~5 s; no point asking sooner
    static const unsigned long BOOT_PROBE_INTERVAL_MS = 500;
    static const unsigned long IDLE_PROBE_INTERVAL_MS = 30000;
    static const unsigned long CONNECT_PROBE_DELAY_MS = 1000;
    static const unsigned long PROBE_REPLY_TIMEOUT_MS = 1500;

    // SVS keep-alive
    int _lastSvsInput;
    unsigned long _svsKeepAliveTime;
    bool _svsKeepAlivePending;
    static const unsigned long SVS_KEEPALIVE_DELAY_MS = 1000;

    /**
     * Find the trigger mapping for a given switcher input.
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
     * Send a framed command to the RetroTINK via serial.
     * Frames as "\r<command>\r" for proper RT4K parsing.
     * Falls back to stub logging if serial is not available.
     * @param command The command to send
     */
    void sendCommand(const String& command);

    /**
     * Process a complete line received from the RT4K serial output.
     * Updates power state based on known status messages. Unrecognized
     * lines are logged and otherwise ignored.
     * @param line The received line (without terminator)
     */
    void processReceivedLine(const String& line);

    /**
     * Read and process incoming serial data from the RT4K.
     * Assembles characters into lines and calls processReceivedLine().
     */
    void processIncomingData();

    /** Move received bytes into the raw capture window (raw channel open). */
    void captureIncomingData();

    /**
     * Pick clean text lines out of raw channel data and pass them to
     * processReceivedLine(), so power state tracking keeps working while an
     * app owns the link. Lines containing binary data are discarded.
     */
    void scanCapturedText(const uint8_t* data, size_t length);

    /** Begin the wake/query sequence for the queued command by sending "pwr on". */
    void startWake();

    /** Send the queued command, if any, and schedule the SVS keep-alive it may need. */
    void sendPendingCommand();

    /** Change the power state and log the transition. */
    void setPowerState(RT4KPowerState state, const char* reason);

    /** Send a "ver" status probe (not logged, not recorded as the last command). */
    void sendProbe();

    /**
     * FULL mode housekeeping: line break detection, wake and boot timeouts,
     * boot polling and the idle status probe.
     * @param now Current millis()
     */
    void processPowerTracking(unsigned long now);

    /**
     * Handle pending operations: power tracking, SVS keep-alive.
     */
    void processPendingOperations();
};

#endif // RETROTINK_H
