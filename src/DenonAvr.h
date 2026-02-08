#ifndef DENON_AVR_H
#define DENON_AVR_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <vector>

class SerialInterface;

/**
 * AVR discovered via SSDP on the local network.
 */
struct DiscoveredAvr {
    String ip;            ///< IP address of the AVR
    String friendlyName;  ///< Model/friendly name (e.g., "Denon AVR-X4300H")
};

/**
 * Denon AVR controller via telnet (TCP port 23).
 *
 * Sends power-on and input source commands to a Denon AVR receiver
 * when triggered by video switcher input changes. Uses the Denon
 * serial protocol over TCP telnet.
 *
 * Denon protocol:
 * - Commands are ASCII strings terminated with CR (\r)
 * - Responses are ASCII strings terminated with CR
 * - Common commands: PWON (power on), PWSTANDBY, SI<input> (select input)
 * - Query commands end with ?: PW?, SI?
 *
 * On input change:
 * 1. Sends "PWON\r" immediately (idempotent if already on)
 * 2. After 1 second delay, sends "SI<input>\r" (e.g., "SIGAME\r")
 *
 * Usage:
 *   DenonAvr avr;
 *   avr.configure(config);  // Reads ip, input from JSON
 *   avr.begin();
 *   // On input change:
 *   avr.onInputChange();
 *   // In loop():
 *   avr.update();
 */
class DenonAvr {
public:
    /**
     * Create Denon AVR controller.
     * Call configure() to set up connection and input source.
     */
    DenonAvr();

    ~DenonAvr();

    /**
     * Configure the AVR from JSON settings.
     * Reads ip, input fields and creates TelnetSerial transport.
     * @param config JSON object containing AVR configuration
     */
    void configure(const JsonObject& config);

    /**
     * Initialize the AVR and its transport.
     * Must be called after configure() and before update().
     * @return true on success
     */
    bool begin();

    /**
     * Process pending commands and read responses.
     * Must be called in loop().
     */
    void update();

    /**
     * Handle a video switcher input change.
     * Sends PWON immediately and queues SI command after delay.
     */
    void onInputChange();

    /**
     * Send a raw command string to the AVR.
     * CR terminator is appended automatically.
     * @param command Command to send (e.g., "PW?", "SIGAME")
     * @return true if command was sent
     */
    bool sendRawCommand(const String& command);

    /** @return true if TCP connection to AVR is active */
    bool isConnected() const;

    /** @return Configured input source name */
    String getInput() const { return _input; }

    /** @return Last command sent to AVR */
    String getLastCommand() const { return _lastCommand; }

    /** @return Last response received from AVR */
    String getLastResponse() const { return _lastResponse; }

    /**
     * Start SSDP discovery for Denon/Marantz AVRs on the local network.
     * Sends M-SEARCH multicast and collects responses for DISCOVERY_TIMEOUT_MS.
     * @return true if discovery started, false if already in progress
     */
    bool startDiscovery();

    /**
     * Check if SSDP discovery has finished.
     * @return true if discovery is complete (or was never started)
     */
    bool isDiscoveryComplete() const;

    /**
     * Get the list of AVRs found during the last discovery.
     * @return Vector of discovered AVR devices
     */
    std::vector<DiscoveredAvr> getDiscoveryResults() const;

private:
    SerialInterface* _serial;
    String _input;
    String _lastCommand;
    String _lastResponse;

    bool _siPending;
    unsigned long _siPendingTime;
    static const unsigned long SI_DELAY_MS = 1000;

    // SSDP discovery state
    bool _discovering = false;
    unsigned long _discoveryStartTime = 0;
    static const unsigned long DISCOVERY_TIMEOUT_MS = 3000;
    WiFiUDP _discoveryUdp;
    std::vector<DiscoveredAvr> _discoveredDevices;

    /** Process incoming SSDP responses during discovery. */
    void processDiscoveryResponses();

    /**
     * Extract IP address from a LOCATION header URL.
     * @param location URL like "http://192.168.1.100:60006/upnp/desc/..."
     * @return IP address string, or empty on failure
     */
    String extractIPFromLocation(const String& location);

    /**
     * Fetch the friendly name from a UPnP device description XML.
     * @param locationUrl Full URL from the LOCATION header
     * @return Friendly name, or "Denon/Marantz AVR" on failure
     */
    String fetchFriendlyName(const String& locationUrl);

    /**
     * Send a command with CR terminator.
     * @param command The command to send
     * @return true if sent successfully
     */
    bool sendCommand(const String& command);

    /** Read and store any available response data. */
    void readResponse();
};

#endif // DENON_AVR_H
