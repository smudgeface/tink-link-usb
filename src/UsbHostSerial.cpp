#include "UsbHostSerial.h"

#ifndef NO_USB_HOST

#include "Logger.h"

UsbHostSerial::UsbHostSerial(uint32_t baud)
    : _baud(baud)
    , _connected(false)
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

bool UsbHostSerial::isSupportedBaud(uint32_t baud) {
    // Rates with a divisor in EspUsbHostSerial_FTDI::onConfig()
    switch (baud) {
        case 300:     case 600:     case 1200:    case 2400:
        case 4800:    case 9600:    case 19200:   case 38400:
        case 57600:   case 115200:  case 230400:  case 460800:
        case 921600:  case 1000000: case 1500000: case 2000000:
        case 3000000:
            return true;
        default:
            return false;
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
