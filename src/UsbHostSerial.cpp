#include "UsbHostSerial.h"

#ifndef NO_USB_HOST

#include "Logger.h"

UsbHostSerial::UsbHostSerial(uint32_t baud)
    : _baud(baud)
    , _connected(false)
    , _baudChangePending(false)
    , _flowControl(true)
    , _flowChangePending(false)
    , _rxHead(0)
    , _rxTail(0)
    , _rxClearPending(false)
    , _rxExtraTransfers{}
    , _libTask(nullptr)
    , _clientTask(nullptr)
    , _newRaised(false)
    , _txPumpMutex(xSemaphoreCreateMutex())
    , _deviceMutex(xSemaphoreCreateMutex())
    , _txHead(0)
    , _txTail(0)
    , _txMux(portMUX_INITIALIZER_UNLOCKED)
    , _txTransferSize(64)
    , _txInFlight(false)
    , _txSubmittedAt(0)
    , _rxLossCount(0)
    , _rxBreakCount(0)
    , _rxInBreak(false)
    , _rxPacketSize(64)
    , _rxOverrunErrors(0)
    , _rxFramingErrors(0)
    , _lastErrorReport(0)
    , _onConnected(nullptr)
    , _onDisconnected(nullptr)
{
    // Unused (task() is never called) but left uninitialized by the base class
    interval = 0;
    lastCheck = 0;
}

void UsbHostSerial::usbLibTask(void* /*arg*/) {
    while (true) {
        uint32_t eventFlags;
        usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);
    }
}

void UsbHostSerial::usbClientTask(void* arg) {
    UsbHostSerial* self = (UsbHostSerial*)arg;
    while (true) {
        usb_host_client_handle_events(self->clientHandle, portMAX_DELAY);
    }
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

bool UsbHostSerial::setHardwareFlowControl(bool enabled) {
    _flowControl = enabled;
    _flowChangePending = true;  // Applied to a connected device in update()
    return true;
}

void UsbHostSerial::applyFlowControl() {
    uint16_t flow = _flowControl ? FTDI_FLOW_RTS_CTS : FTDI_FLOW_NONE;
    esp_err_t err = submit_control(0x40, FTDI_SIO_SET_FLOW_CTRL, 0x0000, flow, 0, nullptr);
    if (err == ESP_OK) {
        LOG_INFO("UsbHostSerial: Flow control set to %s", _flowControl ? "RTS/CTS" : "none");
    } else {
        LOG_ERROR("UsbHostSerial: Failed to set flow control (err 0x%x)", err);
    }
}

bool UsbHostSerial::initTransport() {
    LOG_INFO("UsbHostSerial: Initializing USB Host (FTDI @ %lu baud)...", (unsigned long)_baud);
    // Call base class begin() which returns void
    EspUsbHostSerial_FTDI::begin(_baud);

    // EspUsbHost::task() services USB events from loop() with 1 ms blocking
    // calls, which leaves the FT232R unpolled for 1-2 ms after every transfer
    // - longer than its 256-byte FIFO lasts at 2 Mbaud. Dedicated tasks that
    // block on the event queues react within microseconds instead.
    xTaskCreatePinnedToCore(usbLibTask, "usb_lib", USB_TASK_STACK, this,
                            USB_TASK_PRIORITY, &_libTask, ARDUINO_RUNNING_CORE);
    xTaskCreatePinnedToCore(usbClientTask, "usb_client", USB_TASK_STACK, this,
                            USB_TASK_PRIORITY, &_clientTask, ARDUINO_RUNNING_CORE);

    LOG_INFO("UsbHostSerial: USB Host driver installed, waiting for device...");
    // Always return true - initialization is considered successful if no exception thrown
    return true;
}

void UsbHostSerial::update() {
    // The USB client task flags a disconnect; the receive buffer is cleared
    // here because only this task may move _rxTail
    if (_rxClearPending) {
        _rxClearPending = false;
        _rxTail = _rxHead;
    }

    xSemaphoreTake(_deviceMutex, portMAX_DELAY);

    if (!isReady()) {
        _newRaised = false;
    } else {
        if (!_newRaised) {
            _newRaised = true;
            onNew();
        }
        // Start receiving, or restart if a failed transfer broke the chain
        // that onReceive() keeps going (fails harmlessly while one is in flight)
        usb_host_transfer_submit(usbTransfer_recv);
        for (usb_transfer_t* transfer : _rxExtraTransfers) {
            if (transfer) usb_host_transfer_submit(transfer);
        }
    }

    if (_baudChangePending) {
        _baudChangePending = false;
        if (_connected) {
            applyBaudRate();
        }
    }

    if (_flowChangePending) {
        _flowChangePending = false;
        if (_connected) {
            applyFlowControl();
        }
    }

    pumpTx();
    xSemaphoreGive(_deviceMutex);

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

    if (length > writeAvailable()) {
        LOG_WARN("UsbHostSerial: Transmit queue full (%u bytes dropped)", (unsigned)length);
        return false;
    }

    return write(data, length) == length;
}

size_t UsbHostSerial::writeAvailable() const {
    portENTER_CRITICAL(&_txMux);
    size_t used = (_txHead + USB_TX_BUFFER_SIZE - _txTail) % USB_TX_BUFFER_SIZE;
    portEXIT_CRITICAL(&_txMux);
    return USB_TX_BUFFER_SIZE - 1 - used;
}

size_t UsbHostSerial::write(const uint8_t* data, size_t length) {
    if (!_connected) return 0;

    size_t count = 0;
    portENTER_CRITICAL(&_txMux);
    while (count < length) {
        size_t nextHead = (_txHead + 1) % USB_TX_BUFFER_SIZE;
        if (nextHead == _txTail) break;  // Queue full
        _txBuffer[_txHead] = data[count++];
        _txHead = nextHead;
    }
    portEXIT_CRITICAL(&_txMux);

    // Transmitted by pumpTx() from the loop task
    return count;
}

void UsbHostSerial::pumpTx() {
    if (!_connected || !usbTransfer_send) return;
    if (xSemaphoreTake(_txPumpMutex, 0) != pdTRUE) return;  // The other task is pumping

    if (_txInFlight) {
        // The completion callback only fires for successful transfers, so a
        // failed or stalled one would otherwise block the queue forever
        if (millis() - _txSubmittedAt < TX_TIMEOUT_MS) {
            xSemaphoreGive(_txPumpMutex);
            return;
        }
        LOG_WARN("UsbHostSerial: TX transfer timed out");
        _txInFlight = false;
    }

    size_t count = 0;
    portENTER_CRITICAL(&_txMux);
    while (count < _txTransferSize && _txTail != _txHead) {
        usbTransfer_send->data_buffer[count++] = _txBuffer[_txTail];
        _txTail = (_txTail + 1) % USB_TX_BUFFER_SIZE;
    }
    portEXIT_CRITICAL(&_txMux);

    if (count == 0) {
        xSemaphoreGive(_txPumpMutex);
        return;
    }

    usbTransfer_send->num_bytes = count;
    _txInFlight = true;
    _txSubmittedAt = millis();
    esp_err_t err = usb_host_transfer_submit(usbTransfer_send);
    if (err != ESP_OK) {
        _txInFlight = false;
        LOG_ERROR("UsbHostSerial: TX submit failed (err 0x%x) - %u bytes dropped", err, (unsigned)count);
    }
    xSemaphoreGive(_txPumpMutex);
}

void UsbHostSerial::onSend(usb_transfer_t* /*transfer*/) {
    // Send the next chunk immediately instead of waiting for the next update()
    _txInFlight = false;
    pumpTx();
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
    // Scan buffer for a line terminator
    size_t pos = _rxTail;
    bool found = false;

    while (pos != _rxHead) {
        if (isLineTerminator(_rxBuffer[pos])) {
            found = true;
            break;
        }
        pos = (pos + 1) % USB_RX_BUFFER_SIZE;
    }

    if (!found) {
        // No terminator yet. A full buffer means the line is too long to fit
        // (or is unterminated data) - return it as-is so it gets discarded
        // instead of stalling reception.
        if (available() < MAX_LINE_LENGTH) return false;
    }

    // Read characters up to the terminator
    line = "";
    while (_rxTail != _rxHead) {
        uint8_t ch = _rxBuffer[_rxTail];
        _rxTail = (_rxTail + 1) % USB_RX_BUFFER_SIZE;

        if (isLineTerminator(ch)) {
            // Skip consecutive terminators
            while (_rxTail != _rxHead && isLineTerminator(_rxBuffer[_rxTail])) {
                _rxTail = (_rxTail + 1) % USB_RX_BUFFER_SIZE;
            }
            break;
        }
        line += (char)ch;
    }

    return true;
}

void UsbHostSerial::rxBufferWrite(const uint8_t* data, size_t length) {
    size_t head = _rxHead;
    size_t space = (_rxTail + USB_RX_BUFFER_SIZE - head - 1) % USB_RX_BUFFER_SIZE;
    if (length > space) {
        // Buffer full - drop the excess (the consumer owns _rxTail)
        _rxLossCount += length - space;
        length = space;
    }

    size_t first = USB_RX_BUFFER_SIZE - head;
    if (first > length) first = length;
    memcpy(_rxBuffer + head, data, first);
    memcpy(_rxBuffer, data + first, length - first);
    _rxHead = (head + length) % USB_RX_BUFFER_SIZE;
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
    _txInFlight = false;
    _flowChangePending = _flowControl;  // The base class configured no-flow
    LOG_INFO("UsbHostSerial: FTDI device connected!");
    LOG_INFO("UsbHostSerial:   Manufacturer: %s", getManufacturer().c_str());
    LOG_INFO("UsbHostSerial:   Product:      %s", getProduct().c_str());

    if (_onConnected) {
        _onConnected();
    }
}

void UsbHostSerial::onGone(const usb_device_handle_t *dev_hdl) {
    xSemaphoreTake(_deviceMutex, portMAX_DELAY);
    _connected = false;
    EspUsbHostSerial_FTDI::onGone(dev_hdl);
    for (usb_transfer_t*& transfer : _rxExtraTransfers) {
        if (transfer) {
            usb_host_transfer_free(transfer);
            transfer = nullptr;
        }
    }
    xSemaphoreGive(_deviceMutex);
}

void UsbHostSerial::onGone() {
    _connected = false;
    LOG_WARN("UsbHostSerial: FTDI device disconnected!");

    // Clear the receive buffer (done by update()) and the transmit queue
    _rxClearPending = true;
    portENTER_CRITICAL(&_txMux);
    _txHead = 0;
    _txTail = 0;
    portEXIT_CRITICAL(&_txMux);
    _txInFlight = false;
    _txTransferSize = 64;

    if (_onDisconnected) {
        _onDisconnected();
    }
}

bool UsbHostSerial::resizeTransfer(usb_transfer_t*& transfer, size_t size) {
    usb_transfer_t* larger = nullptr;
    if (usb_host_transfer_alloc(size, 0, &larger) != ESP_OK) {
        return false;
    }

    larger->device_handle = transfer->device_handle;
    larger->bEndpointAddress = transfer->bEndpointAddress;
    larger->callback = transfer->callback;
    larger->context = transfer->context;
    larger->num_bytes = size;

    usb_host_transfer_free(transfer);
    transfer = larger;
    return true;
}

void UsbHostSerial::allocExtraRxTransfers() {
    for (usb_transfer_t*& transfer : _rxExtraTransfers) {
        if (transfer) continue;
        if (usb_host_transfer_alloc(USB_RX_TRANSFER_SIZE, 0, &transfer) != ESP_OK) {
            transfer = nullptr;
            LOG_WARN("UsbHostSerial: Could not allocate all queued RX transfers");
            return;
        }
        transfer->device_handle = usbTransfer_recv->device_handle;
        transfer->bEndpointAddress = usbTransfer_recv->bEndpointAddress;
        transfer->callback = usbTransfer_recv->callback;
        transfer->context = usbTransfer_recv->context;
        transfer->num_bytes = USB_RX_TRANSFER_SIZE;
    }
}

void UsbHostSerial::onConfig(const uint8_t bDescriptorType, const uint8_t *p) {
    bool hadRecvTransfer = (usbTransfer_recv != nullptr);
    bool hadSendTransfer = (usbTransfer_send != nullptr);
    EspUsbHostSerial::onConfig(bDescriptorType, p);

    if (bDescriptorType != USB_B_DESCRIPTOR_TYPE_ENDPOINT) return;

    const usb_ep_desc_t* endpoint = (const usb_ep_desc_t*)p;
    size_t maxPacketSize = endpoint->wMaxPacketSize & 0x07FF;

    // Only act on the endpoint descriptors for which the base class just
    // allocated a bulk transfer
    if (!hadRecvTransfer && usbTransfer_recv) {
        if (maxPacketSize <= FTDI_STATUS_BYTES || USB_RX_TRANSFER_SIZE % maxPacketSize != 0) {
            LOG_WARN("UsbHostSerial: Unexpected max packet size %u - using single-packet transfers",
                     (unsigned)maxPacketSize);
            return;
        }
        _rxPacketSize = maxPacketSize;

        if (!resizeTransfer(usbTransfer_recv, USB_RX_TRANSFER_SIZE)) {
            LOG_WARN("UsbHostSerial: Could not allocate %u byte RX transfer - using single-packet transfers",
                     (unsigned)USB_RX_TRANSFER_SIZE);
            return;
        }
        allocExtraRxTransfers();
        LOG_DEBUG("UsbHostSerial: RX transfer size %u bytes (%u byte packets)",
                  (unsigned)USB_RX_TRANSFER_SIZE, (unsigned)_rxPacketSize);
    }

    if (!hadSendTransfer && usbTransfer_send) {
        _txTransferSize = maxPacketSize;
        if (!resizeTransfer(usbTransfer_send, USB_TX_TRANSFER_SIZE)) {
            LOG_WARN("UsbHostSerial: Could not allocate %u byte TX transfer - using single-packet transfers",
                     (unsigned)USB_TX_TRANSFER_SIZE);
            return;
        }
        _txTransferSize = USB_TX_TRANSFER_SIZE;
        LOG_DEBUG("UsbHostSerial: TX transfer size %u bytes", (unsigned)USB_TX_TRANSFER_SIZE);
    }
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

        if (lineStatus & FTDI_LSR_OVERRUN) { _rxOverrunErrors++; _rxLossCount++; }
        if (lineStatus & FTDI_LSR_FRAMING) _rxFramingErrors++;

        // Count each break once, however many packets it spans
        bool inBreak = (lineStatus & FTDI_LSR_BREAK) != 0;
        if (inBreak && !_rxInBreak) _rxBreakCount++;
        _rxInBreak = inBreak;

        if (packetLen > FTDI_STATUS_BYTES) {
            onReceive(packet + FTDI_STATUS_BYTES, packetLen - FTDI_STATUS_BYTES);
        }

        packet += packetLen;
        remaining -= packetLen;
    }

    // Poll again right away rather than on the next update(): the FT232R
    // overruns if left unpolled for ~1.3 ms at 2 Mbaud. The FTDI completes
    // every transfer with at least its status bytes, so this keeps the
    // endpoint polled continuously; update() restarts the chain if it breaks.
    if (_connected) {
        usb_host_transfer_submit(transfer);
    }
}

void UsbHostSerial::onReceive(const uint8_t* data, const size_t length) {
    rxBufferWrite(data, length);
}

#endif // NO_USB_HOST
