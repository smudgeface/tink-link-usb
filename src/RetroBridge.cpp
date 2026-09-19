#include "RetroBridge.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <vector>
#include "RetroTink.h"
#include "SerialInterface.h"
#include "WifiManager.h"
#include "Logger.h"
#include "version.h"

// USB IDs of the FTDI FT232R on the RT4K's USB port. Apps check for these
// in the status report to confirm the bridge is talking to an RT4K.
static const uint16_t RT4K_USB_VID = 0x0403;
static const uint16_t RT4K_USB_PID = 0x6001;

RetroBridge::RetroBridge()
    : _server(nullptr)
    , _tink(nullptr)
    , _wifi(nullptr)
    , _enabled(true)
    , _mutex(xSemaphoreCreateMutex())
    , _clients{}
    , _nextClientId(1)
    , _lease{}
    , _lastReleasedLeaseId(0)
    , _lastReleasedLeaseClient(0)
    , _transactionActive(false)
{
}

void RetroBridge::begin(AsyncWebServer* server, RetroTink* tink, WifiManager* wifi) {
    _server = server;
    _tink = tink;
    _wifi = wifi;

    // One handler for the whole tree: the paths carry client and lease IDs
    _server->on("/api/v1", HTTP_ANY,
        [this](AsyncWebServerRequest* request) { handleRequest(request); },
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handleBody(request, data, len, index, total);
        });

    LOG_INFO("RetroBridge: Retro-Bridge compatible API %s", _enabled ? "enabled" : "disabled");
}

void RetroBridge::setEnabled(bool enabled) {
    if (_enabled == enabled) return;
    _enabled = enabled;

    if (!enabled) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        if (_lease.active) releaseLeaseLocked("API disabled");
        for (Client& client : _clients) client.id = 0;
        xSemaphoreGive(_mutex);
    }
    LOG_INFO("RetroBridge: API %s", enabled ? "enabled" : "disabled");
}

size_t RetroBridge::getClientCount() const {
    size_t count = 0;
    for (const Client& client : _clients) {
        if (client.id != 0) count++;
    }
    return count;
}

void RetroBridge::update() {
    if (!_lease.active && getClientCount() == 0) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Sampled with the mutex held: a timestamp a handler sets after an
    // earlier sample would make the unsigned differences below wrap
    unsigned long now = millis();

    if (_lease.active) {
        if (now - _lease.lastActivity > _lease.idleMs) {
            releaseLeaseLocked("client went idle");
        } else if (now - _lease.createdAt > _lease.lifetimeMs) {
            releaseLeaseLocked("lifetime exceeded");
        }
    }

    for (Client& client : _clients) {
        bool holdsLease = _lease.active && _lease.clientId == client.id;
        if (client.id != 0 && !holdsLease && now - client.lastSeen > CLIENT_EXPIRY_MS) {
            LOG_DEBUG("RetroBridge: Client %lu expired", (unsigned long)client.id);
            client.id = 0;
        }
    }

    xSemaphoreGive(_mutex);
}

// --- Routing ---

void RetroBridge::handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // The body is kept in _tempObject, which the web server free()s with the request
    if (index == 0) {
        bool tooLarge = total > LEASE_WRITE_MAX_BYTES;
        RequestBody* body = (RequestBody*)malloc(sizeof(RequestBody) + (tooLarge ? 0 : total));
        if (!body) return;
        body->length = 0;
        body->tooLarge = tooLarge;
        request->_tempObject = body;
    }

    RequestBody* body = (RequestBody*)request->_tempObject;
    if (!body || body->tooLarge || index + len > total) return;
    memcpy(body->data + index, data, len);
    body->length = index + len;
}

void RetroBridge::handleRequest(AsyncWebServerRequest* request) {
    if (!_enabled) {
        request->send(404, "text/plain", "Not Found");
        return;
    }

    // Apps make dozens of small requests per operation and give each only a
    // couple of seconds. Reusing the connection saves a TCP handshake - a
    // whole extra round trip - per request, which is what keeps them inside
    // that budget when the WiFi link is having a bad moment.
    if (request->version() >= 1) request->setKeepAlive(true);

    if (request->method() == HTTP_OPTIONS) {
        sendPreflight(request);
        return;
    }

    // Split "/api/v1/<a>/<b>/..." into its segments after "/api/v1"
    String path = request->url().substring(strlen("/api/v1"));
    std::vector<String> parts;
    int start = 1;
    while (start > 0 && start <= (int)path.length()) {
        int slash = path.indexOf('/', start);
        String part = (slash < 0) ? path.substring(start) : path.substring(start, slash);
        if (part.length() > 0) parts.push_back(part);
        start = (slash < 0) ? -1 : slash + 1;
    }

    WebRequestMethodComposite method = request->method();
    size_t n = parts.size();

    if (n == 1 && parts[0] == "info" && method == HTTP_GET) return handleInfo(request);
    if (n == 1 && parts[0] == "status" && method == HTTP_GET) return handleStatus(request);

    if (n >= 1 && parts[0] == "clients") {
        if (n == 1 && method == HTTP_POST) return handleCreateClient(request);

        uint32_t clientId = (n >= 2) ? strtoul(parts[1].c_str(), nullptr, 10) : 0;
        if (n == 2 && method == HTTP_DELETE) return handleDeleteClient(request, clientId);
        if (n == 3 && parts[2] == "transactions" && method == HTTP_POST) return handleTransaction(request, clientId);
        if (n == 3 && parts[2] == "leases" && method == HTTP_POST) return handleCreateLease(request, clientId);

        if (n >= 4 && parts[2] == "leases") {
            uint32_t leaseId = strtoul(parts[3].c_str(), nullptr, 10);
            if (n == 4 && method == HTTP_GET) return handleLeaseState(request, clientId, leaseId);
            if (n == 5 && parts[4] == "write" && method == HTTP_POST) return handleLeaseWrite(request, clientId, leaseId);
            if (n == 5 && parts[4] == "read" && method == HTTP_GET) return handleLeaseRead(request, clientId, leaseId);
            if (n == 5 && (parts[4] == "release" || parts[4] == "cancel") && method == HTTP_POST) {
                return handleLeaseRelease(request, clientId, leaseId);
            }
        }
    }

    sendError(request, 404, "not_found");
}

// --- Telemetry ---

bool RetroBridge::transferEndSeen() const {
    // Look at the tail of the captured stream: "[COM] <verb> <outcome>\n" as the very last line
    uint8_t tail[96];
    uint32_t total = _tink->rawTotal();
    uint32_t from = total > sizeof(tail) - 1 ? total - (sizeof(tail) - 1) : 0;
    bool overflow;
    uint32_t next;
    size_t count = _tink->rawRead(from, tail, sizeof(tail) - 1, overflow, next);
    if (count < 8 || tail[count - 1] != '\n') return false;
    tail[count] = 0;

    // Find the start of the last line (the data before it may be binary, NULs included)
    size_t lineStart = count - 1;
    while (lineStart > 0 && tail[lineStart - 1] != '\n') lineStart--;
    const char* line = strstr((const char*)tail + lineStart, "[COM] ");
    if (!line) {
        // A transfer's final binary frame isn't newline terminated, so the
        // text can follow it on the same "line": search the raw bytes instead
        for (size_t i = lineStart; i + 6 <= count; i++) {
            if (memcmp(tail + i, "[COM] ", 6) == 0) line = (const char*)tail + i;
        }
        if (!line) return false;
    }
    line += 6;

    static const char* const VERBS[] = {"get", "put", "sget"};
    for (const char* verb : VERBS) {
        size_t len = strlen(verb);
        if (strncmp(line, verb, len) != 0) continue;
        const char* rest = line + len;
        if (strncmp(rest, " done", 5) == 0 || strncmp(rest, " aborted", 8) == 0 ||
            strncmp(rest, " err", 4) == 0 || rest[0] == ':') {
            return true;
        }
    }
    return false;
}

bool RetroBridge::supportsStreamedDownloads() const {
    return _tink->rawCapacity() >= STREAMED_DOWNLOAD_MIN_WINDOW;
}

const char* RetroBridge::transportName() const {
    SerialInterface* serial = _tink->getSerial();
    // UART transports have no USB identity; the app treats them as the RT4K's HD-15 port
    return (serial && serial->isUsb()) ? "usb_ftdi" : "hd15_uart";
}

void RetroBridge::handleInfo(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["name"] = "Retro-Bridge";  // Apps refuse to connect to anything else
    doc["firmware"] = "TinkLink-USB " TINKLINK_VERSION_STRING;
    doc["api_version"] = 1;
    doc["hostname"] = WiFi.getHostname();
    String deviceId = WiFi.macAddress();
    deviceId.replace(":", "");
    doc["device_id"] = deviceId;

    JsonArray capabilities = doc["capabilities"].to<JsonArray>();
    capabilities.add("transactions");
    capabilities.add("binary_leases");
    capabilities.add("no_ack_transfers");
    capabilities.add("lease_cursor_64");
    capabilities.add("queued_lease_cancel");
    capabilities.add("http_keep_alive");
    if (supportsStreamedDownloads()) capabilities.add("bridge_paced_downloads");

    doc["serial"]["transport"] = transportName();

    JsonObject limits = doc["limits"].to<JsonObject>();
    limits["clients"] = MAX_CLIENTS;
    limits["transaction_bytes"] = TRANSACTION_MAX_BYTES;
    limits["response_bytes"] = RESPONSE_MAX_BYTES;
    limits["lease_read_bytes"] = LEASE_READ_MAX_BYTES;
    limits["lease_write_bytes"] = LEASE_WRITE_MAX_BYTES;
    limits["lease_lifetime_maximum_ms"] = LEASE_LIFETIME_MAX_MS;
    limits["lease_receive_bytes"] = _tink->rawCapacity();

    String json;
    serializeJson(doc, json);
    sendJson(request, 200, json);
}

void RetroBridge::handleStatus(AsyncWebServerRequest* request) {
    SerialInterface* serial = _tink->getSerial();
    bool ready = _tink->isConnected();
    bool usb = serial && serial->isUsb();

    JsonDocument doc;
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["mode"] = _wifi->isAPActive() ? "provisioning" : "station";
    wifi["connected"] = _wifi->isConnected();
    wifi["ssid"] = _wifi->getSSID();
    wifi["ip"] = _wifi->getIP();
    wifi["hostname"] = WiFi.getHostname();
    wifi["rssi"] = _wifi->getRSSI();

    JsonObject link = doc["serial"].to<JsonObject>();
    link["transport"] = transportName();
    link["state"] = ready ? "ready" : "disconnected";
    link["baud"] = _tink->getBaudRate();
    link["hardware_flow_control"] = serial ? serial->getHardwareFlowControl() : false;
    link["rx_overflows"] = serial ? serial->getRxLossCount() : 0;
    link["rx_breaks"] = serial ? serial->getRxBreakCount() : 0;

    // Unacknowledged (streaming) uploads are safe at any size: the transmit
    // queue pushes back on the app and RTS/CTS paces the RT4K. Whether
    // downloads may stream too is decided by supportsStreamedDownloads().
    bool streaming = usb && serial->getHardwareFlowControl();
    JsonObject bulk = doc["bulk_transfer"].to<JsonObject>();
    bulk["recommended_mode"] = streaming ? "streaming" : "acknowledged";
    bulk["streaming_reliable"] = streaming;

    if (usb) {
        JsonObject usbInfo = doc["usb"].to<JsonObject>();
        usbInfo["state"] = ready ? "ready" : "disconnected";
        usbInfo["vid"] = RT4K_USB_VID;
        usbInfo["pid"] = RT4K_USB_PID;
        usbInfo["baud"] = _tink->getBaudRate();
        usbInfo["hardware_flow_control"] = serial->getHardwareFlowControl();
    }

    JsonObject scheduler = doc["scheduler"].to<JsonObject>();
    scheduler["clients"] = getClientCount();
    scheduler["owner"] = _lease.active ? "lease" : (_transactionActive ? "transaction" : "none");
    scheduler["binary_transfer_active"] = _lease.active;

    JsonObject diagnostics = doc["diagnostics"].to<JsonObject>();
    diagnostics["uptime_ms"] = millis();
    diagnostics["free_heap"] = ESP.getFreeHeap();
    diagnostics["min_free_heap"] = ESP.getMinFreeHeap();
    diagnostics["max_alloc_heap"] = ESP.getMaxAllocHeap();

    String json;
    serializeJson(doc, json);
    sendJson(request, 200, json);
}

// --- Client sessions ---

RetroBridge::Client* RetroBridge::findClient(uint32_t clientId) {
    if (clientId == 0) return nullptr;
    for (Client& client : _clients) {
        if (client.id == clientId) {
            client.lastSeen = millis();
            return &client;
        }
    }
    return nullptr;
}

void RetroBridge::handleCreateClient(AsyncWebServerRequest* request) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Use a free slot, or else replace the client that has been quiet the
    // longest: apps that are closed or reloaded don't always say goodbye
    Client* slot = nullptr;
    unsigned long now = millis();
    for (Client& client : _clients) {
        if (client.id == 0) {
            slot = &client;
            break;
        }
        bool holdsLease = _lease.active && _lease.clientId == client.id;
        if (!holdsLease && (!slot || now - client.lastSeen > now - slot->lastSeen)) {
            slot = &client;
        }
    }

    if (!slot) {
        xSemaphoreGive(_mutex);
        sendError(request, 429, "client_limit");
        return;
    }

    slot->id = _nextClientId++;
    if (_nextClientId == 0) _nextClientId = 1;
    slot->lastSeen = now;
    uint32_t clientId = slot->id;
    xSemaphoreGive(_mutex);

    LOG_INFO("RetroBridge: Client %lu connected", (unsigned long)clientId);
    sendJson(request, 200, "{\"client_id\":" + String((unsigned long)clientId) +
                           ",\"expires_ms\":" + String(CLIENT_EXPIRY_MS) + "}");
}

void RetroBridge::handleDeleteClient(AsyncWebServerRequest* request, uint32_t clientId) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    Client* client = findClient(clientId);
    if (client) {
        if (_lease.active && _lease.clientId == clientId) releaseLeaseLocked("client closed");
        client->id = 0;
    }
    xSemaphoreGive(_mutex);

    if (!client) {
        sendError(request, 404, "no_client");
        return;
    }
    LOG_INFO("RetroBridge: Client %lu disconnected", (unsigned long)clientId);
    sendJson(request, 200, "{\"status\":\"ok\"}");
}

// --- Transactions ---

void RetroBridge::handleTransaction(AsyncWebServerRequest* request, uint32_t clientId) {
    RequestBody* body = (RequestBody*)request->_tempObject;
    if (!body || body->length == 0) return sendError(request, 400, "invalid_request");
    if (body->tooLarge || body->length > TRANSACTION_MAX_BYTES) return sendError(request, 413, "transaction_too_large");

    String completion = request->hasParam("completion") ? request->getParam("completion")->value() : "idle_gap";
    if (completion != "idle_gap" && completion != "first_reply_line" && completion != "no_reply") {
        return sendError(request, 400, "invalid_completion");
    }
    unsigned long idleMs = queryULong(request, "idle_ms", DEFAULT_IDLE_GAP_MS);
    unsigned long timeoutMs = queryULong(request, "timeout_ms", TRANSACTION_TIMEOUT_MAX_MS);
    if (timeoutMs > TRANSACTION_TIMEOUT_MAX_MS) timeoutMs = TRANSACTION_TIMEOUT_MAX_MS;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool known = findClient(clientId) != nullptr;
    bool busy = _lease.active || _transactionActive;
    if (known && !busy) _transactionActive = true;
    xSemaphoreGive(_mutex);

    if (!known) return sendError(request, 404, "no_client");
    if (busy) return sendError(request, 409, "serial_reserved", RETRY_AFTER_MS);

    if (!_tink->isConnected()) {
        _transactionActive = false;
        return sendError(request, 503, "serial_unavailable");
    }
    if (!_tink->rawOpen()) {
        _transactionActive = false;
        return sendError(request, 409, "serial_busy", RETRY_AFTER_MS);
    }

    _tink->rawWrite(body->data, body->length);

    // Wait for the reply in this (web server) task. The loop task keeps
    // running and captures the RT4K's output meanwhile.
    unsigned long start = millis();
    bool timedOut = false;
    if (completion != "no_reply") {
        uint32_t scanned = 0;
        bool sawLine = false;
        while (true) {
            unsigned long now = millis();
            uint32_t total = _tink->rawTotal();

            if (completion == "first_reply_line" && total > scanned) {
                uint8_t chunk[64];
                bool overflow;
                uint32_t next;
                size_t count;
                while (!sawLine && (count = _tink->rawRead(scanned, chunk, sizeof(chunk), overflow, next)) > 0) {
                    scanned = next;
                    sawLine = memchr(chunk, '\n', count) != nullptr;
                }
                if (sawLine) break;
            }

            if (completion == "idle_gap" && total > 0 && now - _tink->rawLastRxTime() >= idleMs) break;
            if (total >= RESPONSE_MAX_BYTES) break;
            if (now - start >= timeoutMs) {
                timedOut = true;
                break;
            }

            esp_task_wdt_reset();
            delay(2);
        }
    }

    uint32_t total = _tink->rawTotal();
    size_t length = (total > RESPONSE_MAX_BYTES) ? RESPONSE_MAX_BYTES : total;

    if (timedOut && length == 0) {
        _tink->rawClose();
        _transactionActive = false;
        AsyncWebServerResponse* response = request->beginResponse(504, "application/json", "{\"error\":\"timeout\"}");
        response->addHeader("X-Retro-Bridge-Result", "timeout");
        addCommonHeaders(response);
        request->send(response);
        return;
    }

    AsyncResponseStream* response = request->beginResponseStream("application/octet-stream", length + 1);
    uint8_t* buffer = (uint8_t*)malloc(length + 1);
    if (buffer) {
        bool overflow;
        uint32_t next;
        size_t count = _tink->rawRead(0, buffer, length, overflow, next);
        response->write(buffer, count);
        free(buffer);
    }
    _tink->rawClose();
    _transactionActive = false;

    response->addHeader("X-Retro-Bridge-Result", timedOut ? "timeout" : "ok");
    response->addHeader("X-Retro-Bridge-Transport", transportName());
    addCommonHeaders(response);
    request->send(response);
}

// --- Leases ---

String RetroBridge::leaseJson() const {
    return "{\"lease_id\":" + String((unsigned long)_lease.id) +
           ",\"state\":\"" + (_lease.active ? "active" : "released") + "\"" +
           ",\"mode\":\"" + (_lease.mode == LeaseMode::NO_ACK ? "no_ack" : "exclusive") + "\"" +
           ",\"transport\":\"" + transportName() + "\"" +
           ",\"automatic_release\":false,\"blocked\":false}";
}

void RetroBridge::releaseLeaseLocked(const char* reason) {
    if (!_lease.active) return;
    _lease.active = false;
    _lastReleasedLeaseId = _lease.id;
    _lastReleasedLeaseClient = _lease.clientId;
    _tink->rawClose();
    LOG_DEBUG("RetroBridge: Lease %lu of client %lu released (%s)",
              (unsigned long)_lease.id, (unsigned long)_lease.clientId, reason);
}

void RetroBridge::handleCreateLease(AsyncWebServerRequest* request, uint32_t clientId) {
    uint32_t leaseId = queryULong(request, "lease_id", 0);
    String mode = request->hasParam("mode") ? request->getParam("mode")->value() : "exclusive";
    if (leaseId == 0) return sendError(request, 400, "invalid_request");
    if (mode != "exclusive" && mode != "no_ack") return sendError(request, 400, "invalid_lease_mode");

    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (!findClient(clientId)) {
        xSemaphoreGive(_mutex);
        return sendError(request, 404, "no_client");
    }

    // A client replacing its own lease is fine; another client's lease is not
    if (_lease.active && _lease.clientId == clientId) releaseLeaseLocked("replaced");
    if (_lease.active || _transactionActive) {
        xSemaphoreGive(_mutex);
        return sendError(request, 409, "serial_reserved", RETRY_AFTER_MS);
    }
    if (!_tink->isConnected()) {
        xSemaphoreGive(_mutex);
        return sendError(request, 503, "serial_unavailable");
    }
    if (!_tink->rawOpen()) {
        xSemaphoreGive(_mutex);
        return sendError(request, 409, "serial_busy", RETRY_AFTER_MS);
    }

    unsigned long now = millis();
    _lease.active = true;
    _lease.id = leaseId;
    _lease.clientId = clientId;
    _lease.mode = (mode == "no_ack") ? LeaseMode::NO_ACK : LeaseMode::EXCLUSIVE;
    _lease.lifetimeMs = queryULong(request, "lifetime_ms", LEASE_LIFETIME_MAX_MS);
    if (_lease.lifetimeMs > LEASE_LIFETIME_MAX_MS) _lease.lifetimeMs = LEASE_LIFETIME_MAX_MS;
    _lease.idleMs = queryULong(request, "idle_ms", 15000);
    _lease.completionIdleMs = queryULong(request, "completion_idle_ms", 1000);
    _lease.createdAt = now;
    _lease.lastActivity = now;
    _lease.lastWrite = now;
    _lease.lastDelivery = now;
    _lease.written = false;
    String json = leaseJson();

    xSemaphoreGive(_mutex);

    LOG_DEBUG("RetroBridge: Lease %lu (%s) granted to client %lu",
              (unsigned long)leaseId, mode.c_str(), (unsigned long)clientId);
    sendJson(request, 200, json);
}

bool RetroBridge::requireLease(AsyncWebServerRequest* request, uint32_t clientId, uint32_t leaseId) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool known = findClient(clientId) != nullptr;
    bool active = _lease.active && _lease.clientId == clientId && _lease.id == leaseId;
    bool released = !active && _lastReleasedLeaseClient == clientId && _lastReleasedLeaseId == leaseId;
    if (active) _lease.lastActivity = millis();
    xSemaphoreGive(_mutex);

    if (!known) {
        sendError(request, 404, "no_client");
    } else if (released) {
        sendError(request, 409, "lease_not_active");
    } else if (!active) {
        sendError(request, 404, "lease_not_found");
    }
    return known && active;
}

void RetroBridge::handleLeaseState(AsyncWebServerRequest* request, uint32_t clientId, uint32_t leaseId) {
    if (!requireLease(request, clientId, leaseId)) return;
    sendJson(request, 200, leaseJson());
}

void RetroBridge::handleLeaseWrite(AsyncWebServerRequest* request, uint32_t clientId, uint32_t leaseId) {
    RequestBody* body = (RequestBody*)request->_tempObject;
    if (!body) return sendError(request, 400, "invalid_request");
    if (body->tooLarge) return sendError(request, 413, "lease_write_too_large");
    if (!requireLease(request, clientId, leaseId)) return;

    // All or nothing: the app resends the whole write when told to retry
    SerialInterface* serial = _tink->getSerial();
    if (!serial || serial->writeAvailable() < body->length) {
        return sendError(request, 429, "lease_not_writable");
    }

    size_t written = _tink->rawWrite(body->data, body->length);
    _lease.lastWrite = millis();
    _lease.written = true;
    sendJson(request, 200, "{\"written\":" + String((unsigned)written) + "}");
}

void RetroBridge::handleLeaseRead(AsyncWebServerRequest* request, uint32_t clientId, uint32_t leaseId) {
    if (!requireLease(request, clientId, leaseId)) return;

    uint32_t cursor = queryULong(request, "cursor", 0);
    unsigned long waitMs = queryULong(request, "wait_ms", 0);
    if (waitMs > LEASE_READ_WAIT_MAX_MS) waitMs = LEASE_READ_WAIT_MAX_MS;
    if (cursor > _tink->rawTotal()) return sendError(request, 400, "invalid_cursor");

    // Wait briefly for data so an app polling an idle link doesn't spin, but
    // not for the app's full wait_ms: this task has other requests to serve
    unsigned long start = millis();
    while (_tink->rawTotal() == cursor && millis() - start < waitMs) {
        delay(2);
    }

    uint8_t* buffer = (uint8_t*)malloc(LEASE_READ_MAX_BYTES);
    if (!buffer) return sendError(request, 500, "out_of_memory");

    bool overflow = false;
    uint32_t next = cursor;
    size_t count = _tink->rawRead(cursor, buffer, LEASE_READ_MAX_BYTES, overflow, next);

    // A "no_ack" exchange is complete once the app has everything and the
    // RT4K has ended the transfer - or, failing that, after a long silence.
    // The silence is measured from the last activity in either direction,
    // including this delivery: a stalled request must not look like an idle link.
    unsigned long now = millis();
    if (count > 0) _lease.lastDelivery = now;
    bool complete = false;
    if (_lease.mode == LeaseMode::NO_ACK && _lease.written && next == _tink->rawTotal()) {
        unsigned long quietSince = _tink->rawLastRxTime();
        if ((long)(_lease.lastWrite - quietSince) > 0) quietSince = _lease.lastWrite;
        if ((long)(_lease.lastDelivery - quietSince) > 0) quietSince = _lease.lastDelivery;
        unsigned long quietFor = now - quietSince;

        unsigned long idleLimit = _lease.completionIdleMs > COMPLETION_IDLE_MIN_MS ? _lease.completionIdleMs
                                                                                     : COMPLETION_IDLE_MIN_MS;
        complete = quietFor >= idleLimit ||
                   (now - _tink->rawLastRxTime() >= COMPLETION_SETTLE_MS && transferEndSeen());
    }

    AsyncResponseStream* response = request->beginResponseStream("application/octet-stream", count + 1);
    response->write(buffer, count);
    free(buffer);

    response->addHeader("X-Retro-Bridge-Cursor", String((unsigned long)next));
    response->addHeader("X-Retro-Bridge-Overflow", overflow ? "true" : "false");
    response->addHeader("X-Retro-Bridge-Complete", complete ? "true" : "false");
    response->addHeader("X-Retro-Bridge-Transport", transportName());
    addCommonHeaders(response);
    request->send(response);
}

void RetroBridge::handleLeaseRelease(AsyncWebServerRequest* request, uint32_t clientId, uint32_t leaseId) {
    if (!requireLease(request, clientId, leaseId)) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_lease.active && _lease.id == leaseId && _lease.clientId == clientId) {
        releaseLeaseLocked("released by client");
    }
    String json = leaseJson();
    xSemaphoreGive(_mutex);

    sendJson(request, 200, json);
}

// --- Responses ---

void RetroBridge::addCommonHeaders(AsyncWebServerResponse* response) {
    // The apps are public HTTPS pages calling a device on the LAN: browsers
    // only allow that with CORS plus the Private Network Access opt-in
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Private-Network", "true");
    response->addHeader("Access-Control-Expose-Headers",
                        "X-Retro-Bridge-Result, X-Retro-Bridge-Cursor, X-Retro-Bridge-Overflow, "
                        "X-Retro-Bridge-Complete, X-Retro-Bridge-Transport, Retry-After");
    response->addHeader("Cache-Control", "no-store");
}

void RetroBridge::sendJson(AsyncWebServerRequest* request, int code, const String& json) {
    AsyncWebServerResponse* response = request->beginResponse(code, "application/json", json);
    addCommonHeaders(response);
    request->send(response);
}

void RetroBridge::sendError(AsyncWebServerRequest* request, int code, const char* error, unsigned long retryAfterMs) {
    String json = String("{\"error\":\"") + error + "\"";
    if (retryAfterMs > 0) json += ",\"retry_after_ms\":" + String(retryAfterMs);
    json += "}";

    AsyncWebServerResponse* response = request->beginResponse(code, "application/json", json);
    if (retryAfterMs > 0) response->addHeader("Retry-After", "1");
    addCommonHeaders(response);
    request->send(response);
}

void RetroBridge::sendPreflight(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(204);
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type, X-Request-ID");
    response->addHeader("Access-Control-Max-Age", "600");
    addCommonHeaders(response);
    request->send(response);
}

unsigned long RetroBridge::queryULong(AsyncWebServerRequest* request, const char* name, unsigned long fallback) {
    if (!request->hasParam(name)) return fallback;
    const String& value = request->getParam(name)->value();
    if (value.length() == 0 || !isDigit(value[0])) return fallback;
    return strtoul(value.c_str(), nullptr, 10);
}
