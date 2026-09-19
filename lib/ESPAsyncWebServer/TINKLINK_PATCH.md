# ESPAsyncWebServer (vendored, patched)

This is [me-no-dev/ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) 1.2.4
(LGPL-3.0, see the notices in the source files), copied here because TinkLink-USB needs a small
change to it and PlatformIO restores registry packages to their pristine state on every build.

## The patch: opt-in HTTP keep-alive

Upstream answers every request with `Connection: close`, so each request costs a new TCP
connection. The apps served by TinkLink's Retro-Bridge compatible API (`/api/v1/*`) make dozens
of small requests per operation and abort any that takes longer than about two seconds. On a weak
WiFi link the extra round trip for the TCP handshake is what pushes requests over that limit.

Changes (all marked `TinkLink patch` in the source):

- `AsyncWebServerRequest::setKeepAlive(bool)` / `keepAlive()` — a handler opts in before `send()`.
- Responses send `Connection: keep-alive` for such requests (`close` otherwise, as before).
  Responses without a content length (chunked / stream-until-close) always fall back to `close`.
- When a keep-alive response has been fully acknowledged, the connection is handed to a fresh
  `AsyncWebServerRequest` instead of waiting for the client to disconnect. Idle keep-alive
  connections are closed after 10 seconds.

`library.json` no longer lists AsyncTCP as a registry dependency: TinkLink uses the patched copy in
`lib/AsyncTCP/`, and with the entry present PlatformIO downloaded a second, unused copy on every build.

Requests that don't opt in behave exactly as upstream. Only `RetroBridge` opts in.

To update the library: replace `src/` with the new upstream version and re-apply the changes
marked `TinkLink patch` in `ESPAsyncWebServer.h`, `WebRequest.cpp` and `WebResponses.cpp`.
