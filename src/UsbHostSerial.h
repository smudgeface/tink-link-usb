#ifndef USB_HOST_SERIAL_H
#define USB_HOST_SERIAL_H

#ifndef NO_USB_HOST

#include <Arduino.h>
#include <EspUsbHostSerial_FTDI.h>
#include <functional>
#include "SerialInterface.h"

/**
 * Ring buffer size for incoming USB serial data.
 * Stores data received from the FTDI device until consumed.
 */
static const size_t USB_RX_BUFFER_SIZE = 512;

/**
 * USB Host serial driver for FTDI devices (RetroTINK 4K).
 *
 * Subclasses EspUsbHostSerial_FTDI to provide:
 * - Automatic FTDI device detection and initialization at 115200 baud
 * - Connection/disconnection tracking with callbacks
 * - Ring buffer for incoming data
 * - Framed data transmission (max 64 bytes per submit)
 *
 * The RetroTINK 4K uses an FTDI FT232R chip (VID:0x0403, PID:0x6001)
 * on its USB-C port. The ESP32-S3's GPIO19/20 pins connect to the
 * USB Host interface.
 *
 * Usage:
 *   UsbHostSerial usb;
 *   usb.begin();                          // Initialize USB Host
 *   usb.setOnConnected([]{ ... });        // Optional callbacks
 *   // In loop():
 *   usb.update();                         // Process USB events
 *   if (usb.available()) {
 *       uint8_t buf[64];
 *       size_t n = usb.read(buf, sizeof(buf));
 *   }
 */
class UsbHostSerial : public EspUsbHostSerial_FTDI, public SerialInterface {
public:
    using ConnectCallback = std::function<void()>;

    UsbHostSerial();
    ~UsbHostSerial();

    /**
     * Initialize USB Host via EspUsbHost::begin() for FTDI communication at 115200 baud.
     * Implements SerialInterface::initTransport().
     * @return true on success
     */
    bool initTransport() override;

    /**
     * Process USB Host events. Must be called in loop().
     * Delegates to EspUsbHostSerial_FTDI::task().
     */
    void update() override;

    /**
     * Send data to the connected FTDI device.
     * Data is sent via USB bulk transfer. Maximum 64 bytes per call.
     * @param data The data string to send
     * @return true if data was submitted, false if device not connected or data too large
     */
    bool sendData(const String& data) override;

    /**
     * Send raw bytes to the connected FTDI device.
     * @param data Pointer to byte buffer
     * @param length Number of bytes to send (max 64)
     * @return true if data was submitted, false if device not connected or data too large
     */
    bool sendData(const uint8_t* data, size_t length);

    /**
     * Check whether an FTDI device is connected and initialized.
     * @return true if device is ready for communication
     */
    bool isDeviceConnected() const { return _connected; }

    /**
     * SerialInterface: Check if transport connection is active.
     * @return true if FTDI device is connected
     */
    bool isConnected() const override { return _connected; }

    /**
     * Get the manufacturer string from the connected device.
     * @return Manufacturer string, or empty if not connected
     */
    String getDeviceManufacturer();

    /**
     * Get the product string from the connected device.
     * @return Product string, or empty if not connected
     */
    String getDeviceProduct();

    /**
     * Get the number of bytes available to read from the receive buffer.
     * @return Number of bytes available
     */
    size_t available() const override;

    /**
     * Read data from the receive buffer.
     * @param buf Destination buffer
     * @param maxLen Maximum bytes to read
     * @return Number of bytes actually read
     */
    size_t read(uint8_t* buf, size_t maxLen);

    /**
     * Read a line from the receive buffer (up to newline or CR).
     * @param line Output string (without the terminator)
     * @return true if a complete line was read, false if no complete line available
     */
    bool readLine(String& line) override;

    /**
     * Set callback for device connection events.
     * @param callback Function to call when FTDI device connects
     */
    void setOnConnected(ConnectCallback callback) { _onConnected = callback; }

    /**
     * Set callback for device disconnection events.
     * @param callback Function to call when FTDI device disconnects
     */
    void setOnDisconnected(ConnectCallback callback) { _onDisconnected = callback; }

protected:
    /** Called by EspUsbHost when the FTDI device is connected and ready. */
    void onNew() override;

    /** Called by EspUsbHost when the FTDI device is disconnected. */
    void onGone() override;

    /** Called by EspUsbHost when data is received from the FTDI device. */
    void onReceive(const uint8_t* data, const size_t length) override;

private:
    volatile bool _connected;

    // Ring buffer for received data
    uint8_t _rxBuffer[USB_RX_BUFFER_SIZE];
    volatile size_t _rxHead;
    volatile size_t _rxTail;

    // Callbacks
    ConnectCallback _onConnected;
    ConnectCallback _onDisconnected;

    /**
     * Write a byte to the ring buffer.
     * @param byte The byte to write
     * @return true if written, false if buffer full
     */
    bool rxBufferWrite(uint8_t byte);
};

#endif // NO_USB_HOST
#endif // USB_HOST_SERIAL_H
