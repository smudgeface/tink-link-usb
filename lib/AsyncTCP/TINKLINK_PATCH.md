# AsyncTCP (vendored, patched)

This is [me-no-dev/AsyncTCP](https://github.com/me-no-dev/AsyncTCP) 1.1.1 (LGPL-3.0, see `LICENSE`
and the notices in the source files), copied here because TinkLink-USB needs a small change to it
and PlatformIO restores registry packages to their pristine state on every build.

## The patch: dual-stack (IPv4 + IPv6) listening sockets

TinkLink gives its WiFi interface an IPv6 link-local address so that the mDNS responder answers
AAAA queries for `tinklink.local`. Without that answer Apple's resolver waits out a 5 second
timeout on every lookup of the name. Once the name has an IPv6 address, clients prefer it, so the
web server has to accept IPv6 connections as well. Upstream `AsyncServer` creates an IPv4-only
socket.

Changes (all marked `TinkLink patch` in `src/AsyncTCP.cpp`):

- `AsyncServer::begin()` — a server bound to "any" address (the usual `AsyncServer(port)` /
  `AsyncWebServer(port)`) listens with `IPADDR_TYPE_ANY`, accepting IPv4 and IPv6. A server bound
  to a specific IPv4 address is unchanged.
- `AsyncClient::getRemoteAddress()` / `getLocalAddress()` (and so `remoteIP()` / `localIP()`)
  return `0.0.0.0` for an IPv6 connection instead of the first four bytes of the IPv6 address.
  The API has no IPv6 accessors; TinkLink doesn't need any.

Outgoing connections (`AsyncClient::connect()`) are still IPv4-only, as upstream.

To update the library: replace `src/` with the new upstream version and re-apply the changes
marked `TinkLink patch`. Newer forks of AsyncTCP have their own IPv6 support, which may make this
patch unnecessary.
