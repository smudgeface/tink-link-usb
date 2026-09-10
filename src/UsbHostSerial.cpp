#include "UsbHostSerial.h"

#ifndef NO_USB_HOST

#include "Logger.h"

UsbHostSerial::UsbHostSerial(uint32_t baud)
    : _baud(baud)
    , _connected(false)
    , _baudChangePending(false)
    , _rxHead(0)
    , _rxTail(0)
    , _rxPacketSize(64)
    , _rxOverrunErrors(0)
    , _rxFramingErrors(0)
    , _lastErrorReport(0)
    , _onConnected(nullptr)
    , _onDisconnected(nullptr)
{
    // EspUsbHostSerial leaves its receive poll timer uninitialized. Poll the
    // bulk IN endpoint on every update() so the FT232R's 256-byte receive
    // FIFO is drained before it can overflow at 2 Mbaud.
    interval = 0;
    lastCheck = 0;
}

UsbHostSerial::~UsbHostSerial() {
}

bool UsbHostSerial::ftdiBaudDivisor(uint32_t baud, uint16_t& divisor) {
    // Divisors from EspUsbHostSerial_FTDI::onConfig() (FT232R encoding)
    switch (baud) {
        case 300:     divisor = 0x2710; return true;
        case 600:     divisor = 0x1388; return true;
        case 1200:    divisor = 0x09c4; return true;
        case 2400:    divisor = 0x04e2; return true;
        case 4800:    divisor = 0x0271; return true;
        case 9600:    divisor = 0x4138; return true;
        case 19200:   divisor = 0x809c; return true;
        case 38400:   divisor = 0xc04e; return true;
        case 57600:   divisor = 0xc034; return true;
        case 115200:  divisor = 0x001a; return true;
        case 230400:  divisor = 0x000d; return true;
        case 460800:  divisor = 0x4006; return true;
        case 921600:  divisor = 0x8003; return true;
        case 1000000: divisor = 0x0003; return true;
        case 1500000: divisor = 0x0002; return true;
        case 2000000: divisor = 0x0001; return true;
        case 3000000: divisor = 0x0000; return true;
        default:      return false;
    }
}

bool UsbHostSerial::isSupportedBaud(uint32_t baud) {
    uint16_t divisor;
    return ftdiBaudDivisor(baud, divisor);
}

bool UsbHostSerial::setBaudRate(uint32_t baud) {
    if (!isSupportedBaud(baud)) {
        return false;
    }

    _baud = baud;
    baudSpeed = baud;           // Used by EspUsbHostSerial_FTDI when a device connects
    _baudChangePending = true;  // Applied to a connected device in update()
    return true;
}

void UsbHostSerial::applyBaudRate() {
    uint16_t divisor;
    if (!ftdiBaudDivisor(_baud, divisor)) {
        return;
    }

    esp_err_t err = submit_control(0x40, FTDI_SIO_SET_BAUD_RATE, divisor);
    if (err == ESP_OK) {
        LOG_INFO("UsbHostSerial: Baud rate changed to %lu", (unsigned long)_baud);
    } else {
        LOG_ERROR("UsbHostSerial: Failed to change baud rate (err 0x%x)", err);
    }
}

bool UsbHostSerial::initTransport() {
    LOG_INFO("UsbHostSerial: Initializing USB Host (FTDI @ %lu baud)...", (unsigned long)_baud);
    // Call base class begin() which returns void
    EspUsbHostSerial_FTDI::begin(_baud);
    LOG_INFO("UsbHostSerial: USB Host driver installed, waiting for device...");
    // Always return true - initialization is considered successful if no exception thrown
    return true;
}

void UsbHostSerial::update() {
    task();

    if (_baudChangePending) {
        _baudChangePending = false;
        if (_connected) {
            applyBaudRate();
        }
    }

    reportLineErrors();
}

bool UsbHostSerial::sendData(const String& data) {
    return sendData((const uint8_t*)data.c_str(), data.length());
}

bool UsbHostSerial::sendData(const uint8_t* data, size_t length) {
    if (!_connected) {
        LOG_WARN("UsbHostSerial: Cannot send - no device connected");
        return false;
    }

    if (length > 64) {
        LOG_WARN("UsbHostSerial: Data too large (%d bytes, max 64)", length);
        return false;
    }

    submit(data, length);
    return true;
}

String UsbHostSerial::getDeviceManufacturer() {
    if (!_connected) return "";
    return String(getManufacturer().c_str());
}

String UsbHostSerial::getDeviceProduct() {
    if (!_connected) return "";
    return String(getProduct().c_str());
}

size_t UsbHostSerial::available() const {
    if (_rxHead >= _rxTail) {
        return _rxHead - _rxTail;
    }
    return USB_RX_BUFFER_SIZE - _rxTail + _rxHead;
}

size_t UsbHostSerial::read(uint8_t* buf, size_t maxLen) {
    size_t count = 0;
    while (count < maxLen && _rxTail != _rxHead) {
        buf[count++] = _rxBuffer[_rxTail];
        _rxTail = (_rxTail + 1) % USB_RX_BUFFER_SIZE;
    }
    return count;
}

bool UsbHostSerial::readLine(String& line) {
    // Scan buffer for a newline or CR
    size_t pos = _rxTail;
    bool found = false;

    while (pos != _rxHead) {
        if (_rxBuffer[pos] == '\n' || _rxBuffer[pos] == '\r') {
            found = true;
            break;
        }
        pos = (pos + 1) % USB_RX_BUFFER_SIZE;
    }

    if (!found) {
        // No terminator yet. A full buffer means the line is too long to fit
        // (or is unterminated data) - return it as-is so it gets discarded
        // instead of stalling reception.
        if (available() < USB_RX_BUFFER_SIZE - 1) return false;
    }

    // Read characters up to the terminator
    line = "";
    while (_rxTail != _rxHead) {
        uint8_t ch = _rxBuffer[_rxTail];
        _rxTail = (_rxTail + 1) % USB_RX_BUFFER_SIZE;

        if (ch == '\n' || ch == '\r') {
            // Skip consecutive CR/LF
            while (_rxTail != _rxHead &&
                   (_rxBuffer[_rxTail] == '\n' || _rxBuffer[_rxTail] == '\r')) {
                _rxTail = (_rxTail + 1) % USB_RX_BUFFER_SIZE;
            }
            break;
        }
        line += (char)ch;
    }

    return true;
}

bool UsbHostSerial::rxBufferWrite(uint8_t byte) {
    size_t nextHead = (_rxHead + 1) % USB_RX_BUFFER_SIZE;
    if (nextHead == _rxTail) {
        // Buffer full - drop oldest byte
        _rxTail = (_rxTail + 1) % USB_RX_BUFFER_SIZE;
    }
    _rxBuffer[_rxHead] = byte;
    _rxHead = nextHead;
    return true;
}

void UsbHostSerial::reportLineErrors() {
    if (_rxOverrunErrors == 0 && _rxFramingErrors == 0) return;

    unsigned long now = millis();
    if (now - _lastErrorReport < ERROR_REPORT_INTERVAL_MS) return;

    // A few framing errors are normal while the RT4K powers up or down.
    // Persistent framing errors mean the baud rate doesn't match the RT4K's.
    LOG_WARN("UsbHostSerial: RX line errors - %lu overrun, %lu framing (persistent framing errors = baud mismatch)",
             (unsigned long)_rxOverrunErrors, (unsigned long)_rxFramingErrors);
    _rxOverrunErrors = 0;
    _rxFramingErrors = 0;
    _lastErrorReport = now;
}

// --- EspUsbHost virtual overrides ---

void UsbHostSerial::onNew() {
    _connected = true;
    LOG_INFO("UsbHostSerial: FTDI device connected!");
    LOG_INFO("UsbHostSerial:   Manufacturer: %s", getManufacturer().c_str());
    LOG_INFO("UsbHostSerial:   Product:      %s", getProduct().c_str());

    if (_onConnected) {
        _onConnected();
    }
}

void UsbHostSerial::onGone() {
    _connected = false;
    LOG_WARN("UsbHostSerial: FTDI device disconnected!");

    // Clear the receive buffer
    _rxHead = 0;
    _rxTail = 0;

    if (_onDisconnected) {
        _onDisconnected();
    }
}

void UsbHostSerial::onConfig(const uint8_t bDescriptorType, const uint8_t *p) {
    bool hadRecvTransfer = (usbTransfer_recv != nullptr);
    EspUsbHostSerial::onConfig(bDescriptorType, p);

    // Only act on the endpoint descriptor for which the base class just
    // allocated the bulk IN transfer
    if (hadRecvTransfer || !usbTransfer_recv || bDescriptorType != USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
        return;
    }

    const usb_ep_desc_t* endpoint = (const usb_ep_desc_t*)p;
    size_t maxPacketSize = endpoint->wMaxPacketSize & 0x07FF;
    if (maxPacketSize <= FTDI_STATUS_BYTES || USB_RX_TRANSFER_SIZE % maxPacketSize != 0) {
        LOG_WARN("UsbHostSerial: Unexpected max packet size %u - using single-packet transfers",
                 (unsigned)maxPacketSize);
        return;
    }
    _rxPacketSize = maxPacketSize;

    usb_transfer_t* transfer = nullptr;
    if (usb_host_transfer_alloc(USB_RX_TRANSFER_SIZE, 0, &transfer) != ESP_OK) {
        LOG_WARN("UsbHostSerial: Could not allocate %u byte RX transfer - using single-packet transfers",
                 (unsigned)USB_RX_TRANSFER_SIZE);
        return;
    }

    transfer->device_handle = usbTransfer_recv->device_handle;
    transfer->bEndpointAddress = usbTransfer_recv->bEndpointAddress;
    transfer->callback = usbTransfer_recv->callback;
    transfer->context = usbTransfer_recv->context;
    transfer->num_bytes = USB_RX_TRANSFER_SIZE;

    usb_host_transfer_free(usbTransfer_recv);
    usbTransfer_recv = transfer;
    LOG_DEBUG("UsbHostSerial: RX transfer size %u bytes (%u byte packets)",
              (unsigned)USB_RX_TRANSFER_SIZE, (unsigned)_rxPacketSize);
}

void UsbHostSerial::onReceive(usb_transfer_t *transfer) {
    // Every FTDI packet starts with 2 status bytes (modem status, line
    // status) followed by the payload. A multi-packet transfer holds several
    // full packets back to back, ending with a short one.
    const uint8_t* packet = transfer->data_buffer;
    size_t remaining = transfer->actual_num_bytes;

    while (remaining >= FTDI_STATUS_BYTES) {
        size_t packetLen = (remaining < _rxPacketSize) ? remaining : _rxPacketSize;
        uint8_t lineStatus = packet[1];

        if (lineStatus & FTDI_LSR_OVERRUN) _rxOverrunErrors++;
        if (lineStatus & FTDI_LSR_FRAMING) _rxFramingErrors++;

        if (packetLen > FTDI_STATUS_BYTES) {
            onReceive(packet + FTDI_STATUS_BYTES, packetLen - FTDI_STATUS_BYTES);
        }

        packet += packetLen;
        remaining -= packetLen;
    }
}

void UsbHostSerial::onReceive(const uint8_t* data, const size_t length) {
    for (size_t i = 0; i < length; i++) {
        rxBufferWrite(data[i]);
    }
}

#endif // NO_USB_HOST
