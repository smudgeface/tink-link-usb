#ifndef SERIAL_INTERFACE_H
#define SERIAL_INTERFACE_H

#include <Arduino.h>

/**
 * Abstract base class for serial transport interfaces.
 *
 * Provides a common API for different serial transports:
 * - UsbHostSerial (USB FTDI for RetroTINK 4K)
 * - TelnetSerial (TCP socket for Denon AVR)
 *
 * Clients (e.g., RetroTink) accept a SerialInterface* so any
 * transport can satisfy the dependency.
 */
class SerialInterface {
public:
    virtual ~SerialInterface() = default;

    /** Process transport events. Called each loop iteration. */
    virtual void update() = 0;

    /** Check if transport connection is active. */
    virtual bool isConnected() const = 0;

    /** Send a string over the transport. */
    virtual bool sendData(const String& data) = 0;

    /** Read a complete line (CR/LF terminated). Returns true if a line was read. */
    virtual bool readLine(String& line) = 0;

    /** Number of bytes available to read. */
    virtual size_t available() const = 0;
};

#endif // SERIAL_INTERFACE_H
