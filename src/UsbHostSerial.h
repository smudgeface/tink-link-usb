#ifndef USB_HOST_SERIAL_H
#define USB_HOST_SERIAL_H

#ifndef NO_USB_HOST

#include <Arduino.h>
#include <EspUsbHostSerial_FTDI.h>
#include <functional>
#include "SerialInterface.h"

/**
 * Ring buffer size for incoming USB serial data.
 * Stores data received from the FTDI device until consumed. Sized to hold
 * several full bulk IN transfers of RT4K output at 2 Mbaud.
 */
static const size_t USB_RX_BUFFER_SIZE = 16384;

/**
 * Ring buffer size for outgoing USB serial data.
 * write() queues data here; update() feeds it to the FTDI device in
 * USB_TX_TRANSFER_SIZE pieces. Sized for a few RT4K transfer frames.
 */
static const size_t USB_TX_BUFFER_SIZE = 8192;

/**
 * Size of each bulk IN transfer from the FTDI device.
 * Must be a multiple of the endpoint's max packet size (64 bytes on the
 * FT232R). The FT232R's receive FIFO is only 256 bytes - 1.3 ms of data at
 * 2 Mbaud - so it must be polled without gaps while data is flowing. The USB
 * controller does that in hardware for the duration of one transfer, so the
 * transfer is sized to take a whole RT4K binary frame (2058 bytes, carried
 * in 34 packets of 62 payload bytes) without software involvement.
 */
static const size_t USB_RX_TRANSFER_SIZE = 4096;

/**
 * Number of bulk IN transfers kept queued on the endpoint. With more than
 * one queued, the USB controller driver starts the next transfer from its
 * interrupt handler when one completes, so polling continues while the
 * completed transfer's data is still being copied out. This is what keeps
 * back-to-back frames (RT4K streaming transfers) from overrunning the FT232R.
 */
static const size_t USB_RX_TRANSFER_COUNT = 4;

/**
 * Size of each bulk OUT transfer to the FTDI device.
 * The USB host stack splits it into max-packet-size packets; the FT232R
 * NAKs when its transmit FIFO is full, so larger transfers are paced by
 * the UART baud rate (and by CTS when hardware flow control is enabled).
 */
static const size_t USB_TX_TRANSFER_SIZE = 512;

/**
 * USB Host serial driver for FTDI devices (RetroTINK 4K).
 *
 * Subclasses EspUsbHostSerial_FTDI to provide:
 * - Automatic FTDI device detection and initialization at a configurable baud rate
 * - Runtime baud rate changes
 * - Connection/disconnection tracking with callbacks
 * - USB events handled in dedicated high-priority tasks, so the FT232R is
 *   polled without the gaps a loop()-driven driver would leave
 * - Multi-packet bulk IN transfers, resubmitted as soon as each one completes
 * - Ring buffer for incoming data
 * - FTDI line error (overrun/framing) reporting via the logger
 * - Queued transmission of any length
 * - Raw (binary-safe) read/write alongside the line-based API
 * - RTS/CTS hardware flow control (on by default)
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
     * Housekeeping. Must be called in loop().
     * Raises connection events, keeps the receive and transmit transfers
     * going, applies pending baud rate / flow control changes, and
     * periodically logs FTDI line errors. USB events themselves are handled
     * by the driver's own tasks, not here.
     */
    void update() override;

    /**
     * Send data to the connected FTDI device.
     * The data is queued and transmitted from update(). Safe to call from
     * any task.
     * @param data The data string to send
     * @return true if all data was queued, false if device not connected or the queue is full
     */
    bool sendData(const String& data) override;

    /**
     * Send raw bytes to the connected FTDI device (all or nothing).
     * @param data Pointer to byte buffer
     * @param length Number of bytes to send
     * @return true if all data was queued, false if device not connected or the queue is full
     */
    bool sendData(const uint8_t* data, size_t length);

    /**
     * Queue raw bytes for transmission. Safe to call from any task.
     * @param data Bytes to send
     * @param length Number of bytes
     * @return Number of bytes queued (may be less than length if the queue is full)
     */
    size_t write(const uint8_t* data, size_t length) override;

    /** @return Free space in the transmit queue, in bytes */
    size_t writeAvailable() const override;

    /**
     * Enable or disable RTS/CTS hardware flow control on the FTDI device
     * (enabled by default). The RT4K relies on it to pace data sent to it at
     * 2 Mbaud: without it, anything longer than a short command (e.g. a file
     * upload frame) overruns the RT4K's receiver. In the other direction the
     * RT4K does not appear to honor it, which is why the receive path is
     * built to never need it.
     * Applied to a connected device on the next update(), and to devices
     * connected later.
     * @param enabled true for RTS/CTS, false for no flow control
     * @return true (always supported)
     */
    bool setHardwareFlowControl(bool enabled) override;

    /** @return true if RTS/CTS hardware flow control is enabled */
    bool getHardwareFlowControl() const override { return _flowControl; }

    /**
     * @return Number of received bytes lost since boot: ring buffer overflows
     *         plus FTDI-reported UART overruns
     */
    uint32_t getRxLossCount() const override { return _rxLossCount; }

    /** @return Number of line breaks reported by the FTDI device since boot */
    uint32_t getRxBreakCount() const override { return _rxBreakCount; }

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

    /** SerialInterface: this transport is the RT4K's USB port. */
    bool isUsb() const override { return true; }

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
     * Read raw data from the receive buffer. Loop task only.
     * @param buf Destination buffer
     * @param maxLen Maximum bytes to read
     * @return Number of bytes actually read
     */
    size_t read(uint8_t* buf, size_t maxLen) override;

    /**
     * Read a line from the receive buffer (up to newline, CR or NUL - the
     * RT4K's power-down line break arrives as a run of NUL bytes, which
     * would otherwise be glued to the front of the next line of text).
     * If MAX_LINE_LENGTH bytes arrive without a terminator, they are returned
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
     * Called by EspUsbHost to release the device's transfers. Holds
     * _deviceMutex so update() can't be using them at the same time.
     */
    void onGone(const usb_device_handle_t *dev_hdl) override;

    /**
     * Called by EspUsbHost for each interface/endpoint descriptor.
     * Replaces the single-packet bulk IN and OUT transfers allocated by the
     * base class with USB_RX_TRANSFER_SIZE / USB_TX_TRANSFER_SIZE
     * multi-packet transfers.
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

    /** Called by EspUsbHost when a bulk OUT transfer completes. Sends the next queued chunk. */
    void onSend(usb_transfer_t *transfer) override;

private:
    uint32_t _baud;
    volatile bool _connected;
    volatile bool _baudChangePending;
    volatile bool _flowControl;
    volatile bool _flowChangePending;

    // Ring buffer for received data. Single producer (USB client task,
    // owns _rxHead) and single consumer (loop task, owns _rxTail), so no
    // lock is needed; when full, new bytes are dropped and counted.
    uint8_t _rxBuffer[USB_RX_BUFFER_SIZE];
    volatile size_t _rxHead;
    volatile size_t _rxTail;
    volatile bool _rxClearPending;

    // Additional queued bulk IN transfers (the base class owns the first)
    usb_transfer_t* _rxExtraTransfers[USB_RX_TRANSFER_COUNT - 1];

    // USB event tasks (see usbLibTask / usbClientTask)
    TaskHandle_t _libTask;
    TaskHandle_t _clientTask;
    bool _newRaised;
    SemaphoreHandle_t _txPumpMutex;
    SemaphoreHandle_t _deviceMutex;  // Guards the transfers against disconnect during update()
    static const size_t MAX_LINE_LENGTH = 1024;
    static const UBaseType_t USB_TASK_PRIORITY = 10;
    static const uint32_t USB_TASK_STACK = 4096;

    // Ring buffer for data waiting to be transmitted. Written from any task,
    // drained by the loop task, guarded by _txMux.
    uint8_t _txBuffer[USB_TX_BUFFER_SIZE];
    size_t _txHead;
    size_t _txTail;
    mutable portMUX_TYPE _txMux;
    size_t _txTransferSize;
    volatile bool _txInFlight;
    unsigned long _txSubmittedAt;
    volatile uint32_t _rxLossCount;
    volatile uint32_t _rxBreakCount;
    bool _rxInBreak;  // Line status of the previous packet had the break bit set

    // FTDI packet framing and line error tracking
    size_t _rxPacketSize;
    uint32_t _rxOverrunErrors;
    uint32_t _rxFramingErrors;
    unsigned long _lastErrorReport;

    static const size_t FTDI_STATUS_BYTES = 2;
    static const uint8_t FTDI_LSR_OVERRUN = 0x02;
    static const uint8_t FTDI_LSR_FRAMING = 0x08;
    static const uint8_t FTDI_LSR_BREAK = 0x10;
    static const uint8_t FTDI_SIO_SET_FLOW_CTRL = 0x02;
    static const uint8_t FTDI_SIO_SET_BAUD_RATE = 0x03;
    static const uint16_t FTDI_FLOW_NONE = 0x0000;
    static const uint16_t FTDI_FLOW_RTS_CTS = 0x0100;  // wIndex high byte
    static const unsigned long TX_TIMEOUT_MS = 1000;
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

    /** @return true for the bytes that end a line in readLine(): LF, CR and NUL */
    static bool isLineTerminator(uint8_t ch) { return ch == '\n' || ch == '\r' || ch == '\0'; }

    /** Send the current baud rate to the connected FTDI device. */
    void applyBaudRate();

    /** Send the current flow control setting to the connected FTDI device. */
    void applyFlowControl();

    /**
     * Submit the next chunk of queued transmit data, if no transfer is in
     * flight. Called from the loop task and from the USB client task.
     */
    void pumpTx();

    /** Task body: services USB host library events (enumeration, pipe events). */
    static void usbLibTask(void* arg);

    /** Task body: services this client's events - transfer callbacks run here. */
    static void usbClientTask(void* arg);

    /**
     * Replace a single-packet bulk transfer allocated by the base class with
     * a larger one for the same endpoint.
     * @param transfer Transfer to replace (updated in place on success)
     * @param size New data buffer size
     * @return true if the transfer was replaced
     */
    bool resizeTransfer(usb_transfer_t*& transfer, size_t size);

    /**
     * Append received data to the ring buffer. Bytes that don't fit are
     * dropped and counted in _rxLossCount.
     * @param data Received bytes
     * @param length Number of bytes
     */
    void rxBufferWrite(const uint8_t* data, size_t length);

    /** Allocate the additional queued bulk IN transfers, cloned from the base class's. */
    void allocExtraRxTransfers();

    /** Log accumulated FTDI line errors, at most once per ERROR_REPORT_INTERVAL_MS. */
    void reportLineErrors();
};

#endif // NO_USB_HOST
#endif // USB_HOST_SERIAL_H
