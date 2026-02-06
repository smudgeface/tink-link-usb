#include "UsbHostSerial.h"
#include "Logger.h"

UsbHostSerial::UsbHostSerial()
    : _connected(false)
    , _rxHead(0)
    , _rxTail(0)
    , _onConnected(nullptr)
    , _onDisconnected(nullptr)
{
}

UsbHostSerial::~UsbHostSerial() {
}

void UsbHostSerial::begin() {
    LOG_INFO("UsbHostSerial: Initializing USB Host (FTDI @ 115200 baud)...");
    EspUsbHostSerial_FTDI::begin(115200);
    LOG_INFO("UsbHostSerial: USB Host driver installed, waiting for device...");
}

void UsbHostSerial::update() {
    task();
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

    if (!found) return false;

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

void UsbHostSerial::onReceive(const uint8_t* data, const size_t length) {
    for (size_t i = 0; i < length; i++) {
        rxBufferWrite(data[i]);
    }
}
