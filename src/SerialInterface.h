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

    /**
     * Initialize the transport. Must be called before use.
     * Named initTransport() to avoid conflicts with other begin() methods
     * in classes using multiple inheritance.
     */
    virtual bool initTransport() = 0;

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

    /**
     * Change the baud rate of the running transport.
     * @param baud New baud rate
     * @return false if the transport has no baud rate (e.g. TCP) or doesn't support the rate
     */
    virtual bool setBaudRate(uint32_t /*baud*/) { return false; }

    /** @return Current baud rate, or 0 if the transport has no baud rate */
    virtual uint32_t getBaudRate() const { return 0; }
};

#endif // SERIAL_INTERFACE_H
