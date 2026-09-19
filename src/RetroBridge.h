#ifndef RETRO_BRIDGE_H
#define RETRO_BRIDGE_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class RetroTink;
class WifiManager;

/**
 * Retro-Bridge compatible HTTP API.
 *
 * Lets apps written for the Retro-Bridge WiFi adapter (the RetroTINK Profiler
 * and Remote web apps) connect to TinkLink by address and talk to the RT4K
 * through TinkLink's serial link. TinkLink is a transparent relay here: it
 * moves bytes between HTTP and the RT4K and has no knowledge of what the
 * apps send.
 *
 * API surface (all under /api/v1, CORS + Private Network Access enabled):
 * - GET    /api/v1/info                                  - identity, capabilities, limits
 * - GET    /api/v1/status                                - WiFi and RT4K serial link state
 * - POST   /api/v1/clients                               - open a client session
 * - DELETE /api/v1/clients/{id}                          - close a client session
 * - POST   /api/v1/clients/{id}/transactions             - send bytes, return the RT4K's reply
 * - POST   /api/v1/clients/{id}/leases                   - take exclusive use of the serial link
 * - GET    /api/v1/clients/{id}/leases/{lease}           - lease state
 * - POST   /api/v1/clients/{id}/leases/{lease}/write     - send bytes
 * - GET    /api/v1/clients/{id}/leases/{lease}/read      - read received bytes from a cursor
 * - POST   /api/v1/clients/{id}/leases/{lease}/cancel    - give the serial link back
 * - POST   /api/v1/clients/{id}/leases/{lease}/release   - give the serial link back
 *
 * Only one transaction or lease uses the serial link at a time; others get
 * a "serial_reserved" error telling them when to retry. While an app holds
 * the link, TinkLink's own RT4K commands are deferred (see RetroTink::rawOpen()).
 *
 * Request handlers run in the web server task. Transactions and lease reads
 * wait for serial data there (bounded, feeding the task watchdog), which is
 * acceptable because these apps issue their requests one at a time.
 *
 * Usage:
 *   RetroBridge bridge;
 *   bridge.begin(server, tink, wifi);   // before server->begin()
 *   // In loop():
 *   bridge.update();                    // expires idle leases and clients
 */
class RetroBridge {
public:
    RetroBridge();

    /**
     * Register the /api/v1 routes.
     * @param server Web server to attach to (before it is started)
     * @param tink RetroTINK controller providing the raw serial channel
     * @param wifi WiFi manager, for the status report
     */
    void begin(AsyncWebServer* server, RetroTink* tink, WifiManager* wifi);

    /** Expire idle leases and client sessions. Must be called in loop(). */
    void update();

    /**
     * Enable or disable the API at runtime. While disabled every /api/v1
     * request gets a 404, and any active lease is released.
     */
    void setEnabled(bool enabled);

    /** @return true if the API is enabled */
    bool isEnabled() const { return _enabled; }

    /** @return true if an app currently holds the serial link */
    bool isLinkInUse() const { return _lease.active || _transactionActive; }

    /** @return Number of open client sessions */
    size_t getClientCount() const;

private:
    static const size_t MAX_CLIENTS = 4;
    static const unsigned long CLIENT_EXPIRY_MS = 300000;
    static const size_t TRANSACTION_MAX_BYTES = 1024;
    static const size_t RESPONSE_MAX_BYTES = 16384;
    static const size_t LEASE_READ_MAX_BYTES = 8192;
    static const size_t LEASE_WRITE_MAX_BYTES = 4096;
    static const unsigned long LEASE_LIFETIME_MAX_MS = 300000;
    static const unsigned long LEASE_READ_WAIT_MAX_MS = 30;
    static const unsigned long TRANSACTION_TIMEOUT_MAX_MS = 15000;
    static const unsigned long DEFAULT_IDLE_GAP_MS = 180;
    static const unsigned long RETRY_AFTER_MS = 250;

    // "no_ack" leases: how long the link must stay quiet, with the app caught
    // up, before the exchange is reported complete without having seen the
    // RT4K finish it. Reporting completion early makes the app stop reading
    // mid-transfer, which is fatal; reporting it late only costs time. It has
    // to outlast a stalled HTTP request (a lost TCP SYN alone costs 1 s).
    static const unsigned long COMPLETION_IDLE_MIN_MS = 3000;
    static const unsigned long COMPLETION_SETTLE_MS = 30;

    // Smallest capture window with which apps may stream downloads without
    // per-frame acknowledgement (see supportsStreamedDownloads())
    static const size_t STREAMED_DOWNLOAD_MIN_WINDOW = 1048576;

    struct Client {
        uint32_t id;  // 0 = free slot
        unsigned long lastSeen;
    };

    enum class LeaseMode { EXCLUSIVE, NO_ACK };

    struct Lease {
        bool active;
        uint32_t id;
        uint32_t clientId;
        LeaseMode mode;
        unsigned long lifetimeMs;
        unsigned long idleMs;
        unsigned long completionIdleMs;
        unsigned long createdAt;
        unsigned long lastActivity;  // Last read/write request from the client
        unsigned long lastWrite;
        unsigned long lastDelivery;  // Last read that handed data to the client
        bool written;                // The client has sent something
    };

    /** Body of a POST request, collected across onBody() calls (freed by the web server). */
    struct RequestBody {
        size_t length;
        bool tooLarge;
        uint8_t data[];
    };

    AsyncWebServer* _server;
    RetroTink* _tink;
    WifiManager* _wifi;
    volatile bool _enabled;

    SemaphoreHandle_t _mutex;  // Guards _clients and _lease (web server task vs loop task)
    Client _clients[MAX_CLIENTS];
    uint32_t _nextClientId;
    Lease _lease;
    uint32_t _lastReleasedLeaseId;
    uint32_t _lastReleasedLeaseClient;
    volatile bool _transactionActive;

    // Routing
    void handleRequest(AsyncWebServerRequest* request);
    void handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
    void handleInfo(AsyncWebServerRequest* request);
    void handleStatus(AsyncWebServerRequest* request);
    void handleCreateClient(AsyncWebServerRequest* request);
    void handleDeleteClient(AsyncWebServerRequest* request, uint32_t clientId);
    void handleTransaction(AsyncWebServerRequest* request, uint32_t clientId);
    void handleCreateLease(AsyncWebServerRequest* request, uint32_t clientId);
    void handleLeaseState(AsyncWebServerRequest* request, uint32_t clientId, uint32_t leaseId);
    void handleLeaseWrite(AsyncWebServerRequest* request, uint32_t clientId, uint32_t leaseId);
    void handleLeaseRead(AsyncWebServerRequest* request, uint32_t clientId, uint32_t leaseId);
    void handleLeaseRelease(AsyncWebServerRequest* request, uint32_t clientId, uint32_t leaseId);

    // Responses (all add the CORS / Private Network Access headers)
    void addCommonHeaders(AsyncWebServerResponse* response);
    void sendJson(AsyncWebServerRequest* request, int code, const String& json);
    void sendError(AsyncWebServerRequest* request, int code, const char* error, unsigned long retryAfterMs = 0);
    void sendPreflight(AsyncWebServerRequest* request);

    // State helpers (call with _mutex held)
    Client* findClient(uint32_t clientId);
    void releaseLeaseLocked(const char* reason);
    String leaseJson() const;

    /**
     * Check that a lease request refers to the active lease.
     * Sends the error response if not.
     * @return true if the lease is active and owned by the client
     */
    bool requireLease(AsyncWebServerRequest* request, uint32_t clientId, uint32_t leaseId);

    /**
     * Whether apps may download files in streaming (unacknowledged) mode.
     * The RT4K then sends at full serial speed - faster than an app can read
     * over HTTP - so the capture window has to absorb the difference. A
     * window that holds typical files whole makes this the more robust mode:
     * unlike acknowledged transfers it doesn't depend on every HTTP round
     * trip beating the RT4K's acknowledgement timeout.
     */
    bool supportsStreamedDownloads() const;

    /**
     * Whether the last thing the RT4K sent is the text line with which it
     * ends a file transfer (done / aborted / error). The one piece of RT4K
     * output the bridge looks at: it lets a "no_ack" lease report completion
     * the moment the transfer ends instead of guessing from silence.
     */
    bool transferEndSeen() const;

    static unsigned long queryULong(AsyncWebServerRequest* request, const char* name, unsigned long fallback);
    const char* transportName() const;
};

#endif // RETRO_BRIDGE_H
