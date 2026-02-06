#include "TelnetSerial.h"
#include "Logger.h"

TelnetSerial::TelnetSerial()
    : _port(23)
{
}

void TelnetSerial::begin(const String& ip, uint16_t port) {
    _ip = ip;
    _port = port;
    LOG_DEBUG("TelnetSerial: Configured for %s:%d", _ip.c_str(), _port);
}

void TelnetSerial::configure(const String& ip, uint16_t port) {
    if (ip != _ip || port != _port) {
        LOG_DEBUG("TelnetSerial: Reconfiguring from %s:%d to %s:%d",
                  _ip.c_str(), _port, ip.c_str(), port);
        disconnect();
        _ip = ip;
        _port = port;
    }
}

void TelnetSerial::update() {
    // No-op: WiFiClient buffers internally
}

bool TelnetSerial::isConnected() const {
    return _client.connected();
}

bool TelnetSerial::sendData(const String& data) {
    if (_ip.length() == 0) {
        LOG_DEBUG("TelnetSerial: No IP configured, cannot send");
        return false;
    }

    if (!ensureConnected()) {
        return false;
    }

    size_t written = _client.print(data);
    if (written == data.length()) {
        LOG_DEBUG("TelnetSerial TX: [%s]", data.c_str());
        return true;
    } else {
        LOG_ERROR("TelnetSerial: Failed to send data (wrote %d of %d bytes)",
                  written, data.length());
        return false;
    }
}

bool TelnetSerial::readLine(String& line) {
    while (_client.available()) {
        char c = _client.read();

        if (c == '\r') {
            // Denon responses terminate with CR
            if (_lineBuffer.length() > 0) {
                line = _lineBuffer;
                _lineBuffer = "";
                return true;
            }
        } else if (c == '\n') {
            // Ignore LF
        } else {
            _lineBuffer += c;
        }
    }
    return false;
}

size_t TelnetSerial::available() const {
    return _client.available();
}

bool TelnetSerial::ensureConnected() {
    if (_client.connected()) {
        return true;
    }

    LOG_DEBUG("TelnetSerial: Connecting to %s:%d...", _ip.c_str(), _port);

    if (_client.connect(_ip.c_str(), _port, CONNECT_TIMEOUT_MS)) {
        LOG_INFO("TelnetSerial: Connected to %s:%d", _ip.c_str(), _port);
        _lineBuffer = "";
        return true;
    } else {
        LOG_ERROR("TelnetSerial: Failed to connect to %s:%d", _ip.c_str(), _port);
        return false;
    }
}

void TelnetSerial::disconnect() {
    if (_client.connected()) {
        _client.stop();
        LOG_DEBUG("TelnetSerial: Disconnected");
    }
    _lineBuffer = "";
}
