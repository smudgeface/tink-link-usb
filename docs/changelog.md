# Changelog

Release notes for every TinkLink-USB version, newest first. Releases are tagged in git (`v1.13.1`, ...).

## v1.13.1 — Instant `.local` Lookups (IPv6)

- **`tinklink.local` resolves instantly** — Every lookup of the `.local` name took exactly 5 seconds on macOS and iOS (by IP address: 2 ms). Apple's resolver asks for the IPv4 (A) and IPv6 (AAAA) address together and waits for both answers, and TinkLink never answered the AAAA query. Browsers hid most of this behind their DNS cache, but the RT4K Profiler and Remote apps give up on requests after 2–5 seconds, so connecting by name was unreliable. TinkLink now gives its WiFi interface (station and access point) an IPv6 link-local address, which makes the mDNS responder answer both queries at once: measured on macOS, lookups dropped from 5.00 s to about 3 ms (the first lookup after a few idle minutes needs one mDNS round trip, typically 0.1–0.4 s).
- **Web server listens on IPv6 as well** — Once the name has an IPv6 address clients prefer it, so the HTTP server has to accept it. AsyncTCP is now vendored in `lib/` with a small dual-stack patch (see [`lib/AsyncTCP/TINKLINK_PATCH.md`](../lib/AsyncTCP/TINKLINK_PATCH.md)). Everything still works by IPv4 address as before.
- **Actual `.local` name reported** — If another device on the network already answers to `tinklink.local`, the mDNS responder renames itself (`tinklink-2.local`, ...). TinkLink now notices, logs a warning, shows the name in use on the Status page, and reports it as `wifi.mdnsName` in `/api/status` (next to the new `wifi.ipv6`).

## v1.13.0 — Retro-Bridge App Support & Binary-Safe RetroTINK Serial

- **Retro-Bridge compatible API** — The RT4K Profiler and Remote web apps can connect to TinkLink as if it were a [Retro-Bridge](https://github.com/solidpipe/retro-bridge): choose *Connect via Retro-Bridge* in the app and enter TinkLink's IP address or `tinklink.local`. TinkLink relays bytes between the app and the RetroTINK (new `/api/v1/*` endpoints with CORS / Private Network Access headers). Verified on hardware with command exchanges, SD card listings, and file downloads and uploads (up to 867 KB, streamed and acknowledged) checked byte for byte. Can be switched off on the Config page (RetroTINK section) or with `retroBridgeApi`.
- **HTTP keep-alive for the app API** — The apps make dozens of small requests per operation and drop the connection if one takes more than about two seconds. The web server library closed the connection after every response, so each request paid for a TCP handshake as well; on a weak or noisy WiFi link that extra round trip regularly pushed requests over the limit. ESPAsyncWebServer is now vendored in `lib/` with a small opt-in keep-alive patch (see [`lib/ESPAsyncWebServer/TINKLINK_PATCH.md`](../lib/ESPAsyncWebServer/TINKLINK_PATCH.md)), used only by `/api/v1/*`. Tested with the real RT4K Profiler on a weak link (about -75 dBm) with periodic 1–1.5 s stalls: the slowest lease read dropped from over 2 s to about 1.3 s. A link that bad can still exceed the apps' limits occasionally (uploads need two round trips because of the browser's CORS preflight), so give TinkLink a decent WiFi signal if you use the apps.
- **Link sharing** — While an app is using the serial link, TinkLink's own commands (input triggers, keep-alives, `/api/tink/send`) are held back and sent the moment the link is free, so a profile switch can't corrupt a file transfer. Power state tracking keeps working during app sessions.
- **Binary-safe serial transport** — New raw read/write path next to the line-based one; writes of any length are queued and sent in order (previously limited to 64 bytes per call).
- **USB receive reliability at 2 Mbaud** — USB events are now handled in dedicated high-priority tasks with several bulk IN transfers kept queued. Previously the FT232R's 256-byte FIFO overran on most bursts over ~2 KB (the driver polled from `loop()` with 1–2 ms gaps); sustained 200 KB/s streams are now received without loss.
- **RTS/CTS flow control** — Enabled on the FTDI link. The RetroTINK relies on it when receiving at 2 Mbaud; without it anything longer than a short command overran its receiver.
- **WiFi modem sleep disabled** — Cuts typical request latency from hundreds of milliseconds to tens.
- **Power management reworked for RT4K firmware 1.75+** — That firmware no longer sends its "Powering Up" / "Boot Sequence Complete" status text over serial, so `full` mode never learned the power state and sent the profile command before a waking RT4K could accept it. TinkLink now uses the command replies instead: every input change first sends `pwr on`, whose reply tells whether the RT4K was asleep; if it was, TinkLink polls until it answers (~5 s) and then sends the profile command. A power-off is detected from the serial line dropping, and a periodic silent probe keeps the reported state current when the RT4K is switched with its remote or button. Older firmware's status text is still understood.
- **`POST /api/tink/trigger`** — Runs the trigger mapped to a switcher input without touching the switcher or AVR; handy for testing mappings.
- **Line parsing fix** — The RT4K's power-down line break (a run of NUL bytes) used to be glued to the next line of text and hide it.
- **Config backup format 1.2** — Adds `tink.retroBridgeApi`.

## v1.12.0 — RetroTINK Config Page & API Reference Cleanup

- **RetroTINK settings on the Config page** — New RetroTINK section shows the serial connection and a baud rate selector (2,000,000 / 115,200). Changes apply immediately, no reboot needed.
- **Live baud rate changes** — The USB (FTDI) and UART transports can change baud rate at runtime.
- **New API endpoints** — `GET`/`POST /api/config/tink` for RetroTINK serial settings; `GET /api/config/triggers` for trigger mappings (the Config page now uses it instead of `/api/status`).
- **API reference overhaul** — All `/api/config/*` endpoints are grouped under Configuration; fixed the System section's broken styling; corrected response examples, error behavior and parameter details; removed the "Try" button from `/api/wifi/disconnect`, which would cut off access to the device.
- **ESP32-C3 web pages synced** — `data_c3/` still had the v1.9.0 web pages; they now match `data/`.

## v1.11.0 — RetroTINK 2 Mbaud USB Serial & AP Mode Recovery Fix

- **RetroTINK 4K firmware 1.75+ support** — USB serial now runs at 2,000,000 baud, the RT4K's new USB-serial default (previously 115,200). New optional `tink.baudRate` setting for older RT4K firmware or custom rates; unsupported USB rates fall back to 2,000,000.
- **Faster USB receive path** — The FTDI bulk IN endpoint is polled on every loop with 512-byte multi-packet transfers (FTDI status bytes stripped per packet) and a 2 KB receive buffer, so 2 Mbaud bursts don't overflow the FT232R's 256-byte FIFO. FTDI overrun/framing errors are reported in the log (persistent framing errors indicate a baud mismatch).
- **Tolerates unknown RT4K output** — Firmware 1.75 expanded the serial interface. Unrecognized lines are logged and ignored; overlong or unterminated data is discarded instead of stalling the USB buffer or growing the UART line buffer without bound. UART mode now treats CR or LF as a line end.
- **AP mode recovery fix** — After a long network outage (e.g. power failure), the device could stay stuck in AP mode indefinitely. `WiFi.status()` kept the previous attempt's "SSID not found" code, so every periodic reconnection attempt after the first was aborted within milliseconds. Attempts now run until connected or timed out.
- **mDNS restart** — The mDNS responder is restarted on every (re)connect instead of failing with "mDNS setup failed" when already running.
- **Config backup format 1.1** — Backups now report format version `1.1` for the new `baudRate` field.

## v1.10.0 — UI Improvements, Config Versioning & Robustness

- **Responsive UI for System Page** — All buttons (OTA upload, config backup/restore, LED controls, reboot) on the `/system.html` page now use a responsive flex layout (`.btn-row` class). They appear side-by-side in landscape view and stack vertically with full width in portrait view for better mobile usability.
- **Config Backup Versioning** — Configuration backups (`/api/config/backup`) now include a `"version": "1.0"` field. The restore function (`/api/config/restore`) validates this version: it accepts legacy backups (no version field) and compatible major versions (e.g., any 1.x version), but rejects incompatible major versions (e.g., 2.x+) to prevent restoring corrupted settings.
- **Robust WiFi Scan/AVR Discovery** — Fixed an issue where initial WiFi scans and AVR discoveries on `/config.html` and the UI required two clicks to show results. The first click now correctly initiates the scan/discovery and reports a "scanning"/"discovering" status, allowing the UI to poll for results.
- **Default AVR Input** — Changed the default AVR input to `"DVD"` from `"GAME"`.
- **Improved UI for OTA Uploads** — Shortened "Upload Firmware" and "Upload Filesystem" button text to simply "Upload" on the `/system.html` page to prevent overflow with the file input field, particularly on smaller screens.

## v1.9.5 — Live Config, Reboot API & Config Backup

- **SSDP discovery fix** — Fixed Denon/Marantz AVR discovery failure caused by using `beginMulticast()` for SSDP M-SEARCH. SSDP responses are unicast, so the multicast-bound socket never received them. Switched to a regular UDP socket that can send to the multicast group and receive unicast replies.
- **Live AVR configuration** — Enabling or disabling the AVR now takes effect immediately without a reboot. The AVR instance is created or destroyed at runtime via a pointer-to-pointer pattern. All user-facing configuration (WiFi, triggers, AVR) now applies live.
- **Reboot API** — New `POST /api/system/reboot` endpoint for remote device restart. Reboot button added to the debug page with confirmation dialog.
- **Config backup & restore** — New `GET /api/config/backup` and `POST /api/config/restore` endpoints to download and restore all device settings (WiFi credentials, triggers, AVR config) as a single JSON object.
- **Safe filesystem OTA** — The `ota_upload.py` script now automatically backs up device configuration before filesystem uploads and restores it after reboot, preventing settings loss.
- **Improved error messages** — AVR API endpoints now return actionable error messages (e.g., "AVR is disabled. Enable it in Config.") instead of generic errors.

## v1.9.3 — WiFi Resilience & DHCP Hostname

- **AP mode reconnection** — When the device falls back to Access Point mode after losing WiFi, it now periodically attempts to reconnect to the saved network (every 30 seconds) using AP+STA mode. The AP remains accessible during reconnection attempts so the web UI is always reachable. On successful reconnection, the device transitions back to STA-only mode.
- **DHCP hostname registration** — The device now properly registers its hostname (`tinklink`) with the router's DHCP server. Requires `WiFi.config()` before `WiFi.setHostname()` on ESP32 Arduino to ensure the hostname is included in DHCP requests. Hostname is re-set after every `WiFi.mode()` change to prevent the ESP32 from reverting to the default name.
- **Disabled ESP32 auto-reconnect** — `WiFi.setAutoReconnect()` is now disabled. The ESP32's built-in auto-reconnect silently reconnects with the default hostname (`esp32s3-XXXX`), bypassing the application's hostname configuration. All reconnection is now handled by WifiManager's state machine, which properly configures the hostname before each connection attempt.
- **Correct AP_STA mode tracking** — `WifiManager::Mode` now correctly reflects `AP_STA` when the device is in dual AP+STA mode. `getMode()` and the `/api/status` `wifi.mode` field report `"ap_sta"` during AP reconnection attempts. All mode checks (`connect()`, `disconnect()`, `stopAccessPoint()`) handle both `AP` and `AP_STA`.
- **Hostname in status API** — `GET /api/status` now includes `wifi.hostname` and correct `wifi.mode` fields for diagnostics.

## v1.9.0 — Pluggable Switcher Architecture, ESP32-C3 Support & Power Management

- **Pluggable switcher architecture** — Abstract `Switcher` base class with `SwitcherFactory`; renamed `ExtronSwVga` → `ExtronSwVgaSwitcher`; all Extron-specific naming generalized throughout codebase
- **Serial transport refactoring** — New `UartSerial` class wrapping `HardwareSerial` with configurable UART number, TX/RX pins, and baud rate; each device class now owns its serial transport internally
- **ConfigManager refactoring** — Replaced typed config structs with raw `JsonDocument` storage; new accessors (`getSwitcherType()`, `getSwitcherConfig()`, `getAvrConfig()`, `getRetroTinkConfig()`); new `"tink"` config section for RetroTINK serial and power settings
- **RetroTINK power management** — Configurable `serialMode` (`"usb"` or `"uart"`) and `powerManagementMode` with three modes: `"off"` (commands sent immediately), `"simple"` (sends `pwr on` once then waits for boot), `"full"` (complete power state tracking via serial status messages)
- **ESP32-C3 support** — New `[env:esp32c3]` PlatformIO environment; `NO_USB_HOST` build flag excludes USB Host code; separate `data_c3/` filesystem directory with C3-specific pin and UART config; pre-build script overrides data directory per environment
- **LED configuration** — New `ledColorOrder` config field (`"RGB"` or `"GRB"`); runtime color order selection; added GPIO8 support for C3 boards
- **Documentation** — New [ALTERNATIVE_BOARDS.md](alternative-boards.md) guide covering ESP32-C3 Super Mini Plus wiring, configuration, and build instructions

## v1.8.0 — AVR Network Discovery

- **SSDP Discovery** — "Discover" button on config page finds Denon/Marantz AVRs on the local network via UPnP multicast, fetches friendly names from device XML descriptions
- **New API endpoint** — `GET /api/avr/discover` with polling pattern (matches WiFi scan UX)
- **Reference docs** — Added [HEOS CLI Protocol Specification](../assets/docs/HEOS_CLI_Protocol_Specification.pdf) to `assets/docs/`

## v1.7.0 — Denon/Marantz AVR Control

- **AVR telnet control** — Sends power-on (`PWON`) and input select (`SI<input>`) commands via TCP port 23 when video switcher input changes
- **Modular serial transport** — `SerialInterface` abstraction with `TelnetSerial` implementation for TCP-based serial communication
- **AVR configuration UI** — Config page section for enabling AVR control, setting IP address, selecting input source, and testing the connection
- **AVR status API** — `GET /api/config/avr`, `POST /api/config/avr`, `POST /api/avr/send` endpoints
- **Denon protocol** — ASCII commands terminated with CR; supports power, input, and volume queries

## v1.6.0 — Signal Detection Auto-Switch

- **Extron signal parsing** — Parses `Sig` messages from Extron switcher to detect video source power-on/off events
- **Auto-switch with debounce** — 2-second debounce filters signal glitches; highest active input wins
- **Signal restore handling** — Re-triggers input callback when signal returns on current input (enables RT4K auto-wake after power cycle)

## v1.5.0 — USB Host Communication

- **USB Host FTDI driver** — `UsbHostSerial` class communicates with RetroTINK 4K via FTDI FT232R at 115200 baud
- **RT4K power state tracking** — Detects boot complete, power-off, and sleep events via serial
- **Auto-wake** — Automatically powers on RT4K and queues commands when input changes arrive while sleeping
- **SVS keep-alive** — Sends follow-up `SVS CURRENT` command after `SVS NEW` to maintain connection
- **USB OTG mode** — Switched from CDC to OTG mode; all debugging via web console and `scripts/logs.py`
