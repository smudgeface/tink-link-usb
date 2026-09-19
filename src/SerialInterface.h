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

    /** @return true if this transport reaches the device through its USB port */
    virtual bool isUsb() const { return false; }

    /** Send a string over the transport. */
    virtual bool sendData(const String& data) = 0;

    /** Read a complete line (CR/LF terminated). Returns true if a line was read. */
    virtual bool readLine(String& line) = 0;

    /** Number of bytes available to read. */
    virtual size_t available() const = 0;

    /**
     * Read raw bytes without any line processing. Used for binary exchanges
     * (e.g. RT4K file transfers) where readLine() would mangle the data.
     * @param buf Destination buffer
     * @param maxLen Maximum bytes to read
     * @return Number of bytes read (0 if none, or if the transport has no raw read path)
     */
    virtual size_t read(uint8_t* /*buf*/, size_t /*maxLen*/) { return 0; }

    /**
     * Queue raw bytes for transmission. Unlike sendData(), the data may be
     * binary and of any length; it is sent in order as the transport allows.
     * @param data Bytes to send
     * @param length Number of bytes
     * @return Number of bytes accepted (less than length if the transmit queue is full)
     */
    virtual size_t write(const uint8_t* /*data*/, size_t /*length*/) { return 0; }

    /**
     * Enable or disable RTS/CTS hardware flow control.
     * @return false if the transport does not support it
     */
    virtual bool setHardwareFlowControl(bool /*enabled*/) { return false; }

    /** @return true if RTS/CTS hardware flow control is enabled */
    virtual bool getHardwareFlowControl() const { return false; }

    /** @return Number of bytes write() can currently accept */
    virtual size_t writeAvailable() const { return 0; }

    /**
     * @return Number of line breaks (receive line held low) detected since
     *         boot, or 0 if the transport can't detect them. The RT4K's
     *         serial output drops low like this when it powers down.
     */
    virtual uint32_t getRxBreakCount() const { return 0; }

    /**
     * @return Count of receive-side data loss events (buffer overflows, UART
     *         overruns) since boot. Lets a binary exchange detect corruption.
     */
    virtual uint32_t getRxLossCount() const { return 0; }

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
