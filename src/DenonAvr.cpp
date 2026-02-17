#include "DenonAvr.h"
#include "SerialInterface.h"
#include "TelnetSerial.h"
#include "Logger.h"
#include <WiFi.h>

DenonAvr::DenonAvr()
    : _serial(nullptr)
    , _siPending(false)
    , _siPendingTime(0)
{
}

DenonAvr::~DenonAvr() {
    if (_serial) {
        delete _serial;
        _serial = nullptr;
    }
}

void DenonAvr::configure(const JsonObject& config) {
    _input = config["input"] | "DVD";
    String ip = config["ip"] | "";

    LOG_DEBUG("DenonAvr: Configuring (ip=%s, input=%s)", ip.c_str(), _input.c_str());

    // Clean up existing serial
    if (_serial) {
        delete _serial;
        _serial = nullptr;
    }

    // Create TelnetSerial if IP is provided
    if (ip.length() > 0) {
        _serial = new TelnetSerial(ip, 23);
    }
}

bool DenonAvr::begin() {
    if (!_serial) {
        LOG_ERROR("DenonAvr: Cannot begin - not configured");
        return false;
    }

    if (!_serial->initTransport()) {
        LOG_ERROR("DenonAvr: Failed to initialize serial");
        return false;
    }

    LOG_INFO("DenonAvr: Initialized (input: %s)", _input.c_str());
    return true;
}

void DenonAvr::update() {
    // Process SSDP discovery
    if (_discovering) {
        processDiscoveryResponses();
        if (millis() - _discoveryStartTime >= DISCOVERY_TIMEOUT_MS) {
            _discoveryUdp.stop();
            _discovering = false;
            LOG_INFO("DenonAvr: Discovery complete, found %d device(s)", _discoveredDevices.size());
        }
    }

    // Check for pending SI command
    if (_siPending && (millis() - _siPendingTime >= SI_DELAY_MS)) {
        String siCommand = "SI" + _input;
        sendCommand(siCommand);
        _siPending = false;
        LOG_INFO("DenonAvr: Sent delayed input select: %s", siCommand.c_str());
    }

    // Read any available responses
    readResponse();
}

void DenonAvr::onInputChange() {
    // Send power on immediately
    sendCommand("PWON");
    LOG_INFO("DenonAvr: Input change - sent PWON, queuing SI%s", _input.c_str());

    // Queue input select after delay
    _siPending = true;
    _siPendingTime = millis();
}

bool DenonAvr::sendRawCommand(const String& command) {
    return sendCommand(command);
}

bool DenonAvr::isConnected() const {
    return _serial && _serial->isConnected();
}


bool DenonAvr::startDiscovery() {
    if (_discovering) {
        return false;
    }

    // SSDP discovery requires active WiFi connection (not just AP mode)
    if (WiFi.status() != WL_CONNECTED) {
        LOG_ERROR("DenonAvr: Cannot start discovery - WiFi not connected (status: %d)", WiFi.status());
        return false;
    }

    _discoveredDevices.clear();

    // Stop any existing socket from a previous discovery attempt
    _discoveryUdp.stop();

    // Use a regular UDP socket (not multicast) for SSDP M-SEARCH.
    // M-SEARCH responses are sent as UNICAST back to the requester,
    // so beginMulticast() won't receive them — it binds to the multicast
    // group address and only sees multicast traffic.
    if (!_discoveryUdp.begin(0)) {  // 0 = ephemeral port
        LOG_ERROR("DenonAvr: Failed to start UDP socket for discovery");
        return false;
    }

    // Build M-SEARCH packet
    const char* msearch =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-denon-com:device:ACT-Denon:1\r\n"
        "\r\n";

    _discoveryUdp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
    _discoveryUdp.write((const uint8_t*)msearch, strlen(msearch));
    _discoveryUdp.endPacket();

    _discovering = true;
    _discoveryStartTime = millis();

    LOG_INFO("DenonAvr: SSDP discovery started");
    return true;
}

bool DenonAvr::isDiscoveryComplete() const {
    return !_discovering;
}

std::vector<DiscoveredAvr> DenonAvr::getDiscoveryResults() const {
    return _discoveredDevices;
}

void DenonAvr::processDiscoveryResponses() {
    int packetSize;
    while ((packetSize = _discoveryUdp.parsePacket()) > 0) {
        // Read response
        char buf[1024];
        int len = _discoveryUdp.read(buf, sizeof(buf) - 1);
        if (len <= 0) continue;
        buf[len] = '\0';

        String response(buf);

        // Find LOCATION header
        int locIdx = response.indexOf("LOCATION:");
        if (locIdx < 0) locIdx = response.indexOf("Location:");
        if (locIdx < 0) continue;

        int locStart = locIdx + 9; // length of "LOCATION:"
        // Skip whitespace
        while (locStart < len && (buf[locStart] == ' ' || buf[locStart] == '\t')) locStart++;
        int locEnd = response.indexOf("\r\n", locStart);
        if (locEnd < 0) locEnd = response.indexOf("\n", locStart);
        if (locEnd < 0) continue;

        String locationUrl = response.substring(locStart, locEnd);
        locationUrl.trim();

        // Extract IP from location URL
        String ip = extractIPFromLocation(locationUrl);
        if (ip.isEmpty()) continue;

        // Skip duplicates
        bool found = false;
        for (const auto& dev : _discoveredDevices) {
            if (dev.ip == ip) { found = true; break; }
        }
        if (found) continue;

        // Fetch friendly name from the UPnP description XML
        String friendlyName = fetchFriendlyName(locationUrl);

        DiscoveredAvr avr;
        avr.ip = ip;
        avr.friendlyName = friendlyName;
        _discoveredDevices.push_back(avr);

        LOG_INFO("DenonAvr: Discovered %s at %s", friendlyName.c_str(), ip.c_str());
    }
}

String DenonAvr::extractIPFromLocation(const String& location) {
    // Parse "http://192.168.1.100:60006/upnp/desc/..."
    int schemeEnd = location.indexOf("://");
    if (schemeEnd < 0) return "";

    int hostStart = schemeEnd + 3;
    int hostEnd = location.indexOf(":", hostStart);
    int slashEnd = location.indexOf("/", hostStart);

    if (hostEnd < 0 || (slashEnd >= 0 && slashEnd < hostEnd)) {
        hostEnd = slashEnd;
    }
    if (hostEnd < 0) {
        hostEnd = location.length();
    }

    return location.substring(hostStart, hostEnd);
}

String DenonAvr::fetchFriendlyName(const String& locationUrl) {
    // Parse host and port from URL: "http://192.168.1.100:60006/path..."
    String ip = extractIPFromLocation(locationUrl);
    if (ip.isEmpty()) return "Denon/Marantz AVR";

    uint16_t port = 80;
    int schemeEnd = locationUrl.indexOf("://");
    if (schemeEnd >= 0) {
        int hostStart = schemeEnd + 3;
        int colonIdx = locationUrl.indexOf(":", hostStart);
        int slashIdx = locationUrl.indexOf("/", hostStart);
        if (colonIdx >= 0 && (slashIdx < 0 || colonIdx < slashIdx)) {
            port = locationUrl.substring(colonIdx + 1, slashIdx >= 0 ? slashIdx : locationUrl.length()).toInt();
        }
    }

    String path = "/";
    int pathStart = locationUrl.indexOf("/", locationUrl.indexOf("://") + 3);
    if (pathStart >= 0) {
        path = locationUrl.substring(pathStart);
    }

    WiFiClient client;
    client.setTimeout(2);  // 2 second timeout
    if (!client.connect(ip.c_str(), port)) {
        return "Denon/Marantz AVR";
    }

    client.printf("GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
                  path.c_str(), ip.c_str(), port);

    // Read response (up to 4KB is enough for the friendlyName)
    String body;
    unsigned long start = millis();
    while (client.connected() && millis() - start < 2000) {
        while (client.available()) {
            body += (char)client.read();
            if (body.length() > 4096) break;
        }
        if (body.length() > 4096) break;
    }
    client.stop();

    // Simple string search for <friendlyName>...</friendlyName>
    int startTag = body.indexOf("<friendlyName>");
    if (startTag < 0) return "Denon/Marantz AVR";

    int nameStart = startTag + 14; // length of "<friendlyName>"
    int nameEnd = body.indexOf("</friendlyName>", nameStart);
    if (nameEnd < 0) return "Denon/Marantz AVR";

    return body.substring(nameStart, nameEnd);
}

bool DenonAvr::sendCommand(const String& command) {
    if (!_serial) {
        LOG_ERROR("DenonAvr: No serial interface");
        return false;
    }

    // Denon protocol: commands terminated with CR
    String data = command + "\r";
    _lastCommand = command;

    if (_serial->sendData(data)) {
        LOG_DEBUG("DenonAvr TX: [%s]", command.c_str());
        return true;
    } else {
        LOG_ERROR("DenonAvr: Failed to send command: %s", command.c_str());
        return false;
    }
}

void DenonAvr::readResponse() {
    if (!_serial) return;

    String line;
    while (_serial->readLine(line)) {
        _lastResponse = line;
        LOG_DEBUG("DenonAvr RX: %s", line.c_str());
    }
}
