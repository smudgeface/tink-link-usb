#ifndef TELNET_SERIAL_H
#define TELNET_SERIAL_H

#include <Arduino.h>
#include <WiFiClient.h>
#include "SerialInterface.h"

/**
 * TCP socket serial transport implementing SerialInterface.
 *
 * Connects to a device via TCP (typically telnet on port 23).
 * Used for Denon AVR control over network.
 *
 * Key behaviors:
 * - Connects on demand when sendData() is called
 * - Reuses existing connection if still active
 * - readLine() accumulates bytes and returns on CR (Denon protocol)
 * - configure() disconnects if IP changed so next send reconnects
 */
class TelnetSerial : public SerialInterface {
public:
    TelnetSerial();

    /**
     * Initialize with target IP and port.
     * Does not connect immediately; connection is made on first sendData().
     * @param ip Target IP address
     * @param port Target port (default 23 for telnet)
     */
    void begin(const String& ip, uint16_t port = 23);

    /**
     * Reconfigure target IP and port.
     * Disconnects existing connection if IP changed.
     * @param ip New target IP address
     * @param port New target port
     */
    void configure(const String& ip, uint16_t port = 23);

    // SerialInterface overrides
    void update() override;
    bool isConnected() const override;
    bool sendData(const String& data) override;
    bool readLine(String& line) override;
    size_t available() const override;

    /** @return Configured target IP address */
    String getIP() const { return _ip; }

    /** @return Configured target port */
    uint16_t getPort() const { return _port; }

private:
    mutable WiFiClient _client;
    String _ip;
    uint16_t _port;
    String _lineBuffer;
    static const unsigned long CONNECT_TIMEOUT_MS = 2000;

    /**
     * Ensure TCP connection is active. Connects if not already connected.
     * @return true if connected (or just connected successfully)
     */
    bool ensureConnected();

    /** Close the TCP connection. */
    void disconnect();
};

#endif // TELNET_SERIAL_H
