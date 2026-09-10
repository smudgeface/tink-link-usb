#ifndef USB_HOST_SERIAL_H
#define USB_HOST_SERIAL_H

#ifndef NO_USB_HOST

#include <Arduino.h>
#include <EspUsbHostSerial_FTDI.h>
#include <functional>
#include "SerialInterface.h"

/**
 * Ring buffer size for incoming USB serial data.
 * Stores data received from the FTDI device until consumed. Sized to absorb
 * bursts of RT4K output at 2 Mbaud between loop() iterations.
 */
static const size_t USB_RX_BUFFER_SIZE = 2048;

/**
 * Size of each bulk IN transfer from the FTDI device.
 * Must be a multiple of the endpoint's max packet size (64 bytes on the
 * FT232R). Receiving up to 8 packets per poll keeps up with 2 Mbaud output;
 * the FT232R's own receive FIFO is only 256 bytes.
 */
static const size_t USB_RX_TRANSFER_SIZE = 512;

/**
 * USB Host serial driver for FTDI devices (RetroTINK 4K).
 *
 * Subclasses EspUsbHostSerial_FTDI to provide:
 * - Automatic FTDI device detection and initialization at a configurable baud rate
 * - Runtime baud rate changes
 * - Connection/disconnection tracking with callbacks
 * - Multi-packet bulk IN transfers, polled on every update()
 * - Ring buffer for incoming data
 * - FTDI line error (overrun/framing) reporting via the logger
 * - Framed data transmission (max 64 bytes per submit)
 *
 * The RetroTINK 4K uses an FTDI FT232R chip (VID:0x0403, PID:0x6001)
 * on its USB-C port. The ESP32-S3's GPIO19/20 pins connect to the
 * USB Host interface.
 *
 * Usage:
 *   UsbHostSerial usb(2000000);
 *   usb.initTransport();                  // Initialize USB Host
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

    /**
     * Create the USB Host serial driver.
     * @param baud FTDI baud rate. Must satisfy isSupportedBaud().
     */
    explicit UsbHostSerial(uint32_t baud);
    ~UsbHostSerial();

    /**
     * Check whether a baud rate can be configured on the FTDI device.
     * Only standard rates with a known FT232R divisor are supported.
     * @param baud Baud rate to check
     * @return true if the rate is supported
     */
    static bool isSupportedBaud(uint32_t baud);

    /**
     * Change the baud rate. A connected device is reconfigured on the next
     * update() (USB transfers must be submitted from the loop task); devices
     * connected later use the new rate.
     * @param baud New baud rate (must satisfy isSupportedBaud())
     * @return false if the rate is not supported
     */
    bool setBaudRate(uint32_t baud) override;

    /** @return Configured FTDI baud rate */
    uint32_t getBaudRate() const override { return _baud; }

    /**
     * Initialize USB Host via EspUsbHost::begin() for FTDI communication at the configured baud rate.
     * Implements SerialInterface::initTransport().
     * @return true on success
     */
    bool initTransport() override;

    /**
     * Process USB Host events. Must be called in loop().
     * Delegates to EspUsbHostSerial_FTDI::task(), applies pending baud rate
     * changes, and periodically logs FTDI line errors.
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
     * If the buffer fills up without a terminator, its contents are returned
     * as one line so overlong or unterminated data can't stall reception.
     * @param line Output string (without the terminator)
     * @return true if a line was read, false if no complete line available
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

    /**
     * Called by EspUsbHost for each interface/endpoint descriptor.
     * Replaces the single-packet bulk IN transfer allocated by the base
     * class with a USB_RX_TRANSFER_SIZE multi-packet transfer.
     */
    void onConfig(const uint8_t bDescriptorType, const uint8_t *p) override;

    /**
     * Called by EspUsbHost when a bulk IN transfer completes.
     * Strips the 2 FTDI status bytes that lead every packet in the transfer
     * and records line errors.
     */
    void onReceive(usb_transfer_t *transfer) override;

    /** Called with the payload of each received FTDI packet. */
    void onReceive(const uint8_t* data, const size_t length) override;

private:
    uint32_t _baud;
    volatile bool _connected;
    volatile bool _baudChangePending;

    // Ring buffer for received data
    uint8_t _rxBuffer[USB_RX_BUFFER_SIZE];
    volatile size_t _rxHead;
    volatile size_t _rxTail;

    // FTDI packet framing and line error tracking
    size_t _rxPacketSize;
    uint32_t _rxOverrunErrors;
    uint32_t _rxFramingErrors;
    unsigned long _lastErrorReport;

    static const size_t FTDI_STATUS_BYTES = 2;
    static const uint8_t FTDI_LSR_OVERRUN = 0x02;
    static const uint8_t FTDI_LSR_FRAMING = 0x08;
    static const uint8_t FTDI_SIO_SET_BAUD_RATE = 0x03;
    static const unsigned long ERROR_REPORT_INTERVAL_MS = 30000;

    // Callbacks
    ConnectCallback _onConnected;
    ConnectCallback _onDisconnected;

    /**
     * Look up the FT232R baud rate divisor (same values as EspUsbHostSerial_FTDI).
     * @param baud Baud rate
     * @param divisor Output divisor for the SIO_SET_BAUD_RATE request
     * @return false if the rate has no divisor
     */
    static bool ftdiBaudDivisor(uint32_t baud, uint16_t& divisor);

    /** Send the current baud rate to the connected FTDI device. */
    void applyBaudRate();

    /**
     * Write a byte to the ring buffer.
     * @param byte The byte to write
     * @return true if written, false if buffer full
     */
    bool rxBufferWrite(uint8_t byte);

    /** Log accumulated FTDI line errors, at most once per ERROR_REPORT_INTERVAL_MS. */
    void reportLineErrors();
};

#endif // NO_USB_HOST
#endif // USB_HOST_SERIAL_H
