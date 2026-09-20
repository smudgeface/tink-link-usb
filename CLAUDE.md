# Claude Development Guide for TinkLink-USB

This document provides guidelines and conventions for Claude (AI assistant) when working on the TinkLink-USB project.

## Project Overview

TinkLink-USB is an ESP32-S3 USB bridge between video switchers and the RetroTINK 4K. It automatically triggers RetroTINK profile changes when video switcher inputs change.

**Current Status:** Active development. USB Host, WiFi, LED, Web Console, OTA, Denon AVR control, SSDP discovery, config backup/restore, reboot API, and the Retro-Bridge compatible API (RT4K Profiler / Remote app support) all functional. Version 1.13.1.

**Tech Stack:**
- Platform: ESP32-S3 (Arduino framework, USB OTG mode)
- Build System: PlatformIO
- Filesystem: LittleFS
- Web Server: ESPAsyncWebServer
- LED Control: FastLED (WS2812)
- USB Host: EspUsbHost (FTDI FT232R for RetroTINK 4K)
- Configuration: ArduinoJson

## Git & Version Control Rules

### Before Pushing to GitHub

**CRITICAL:** Always ask for user approval before pushing to GitHub. Never push without explicit permission.

Example workflow:
1. Make changes and commit locally
2. Show summary of changes to user
3. Ask: "Ready to push to GitHub?"
4. Wait for user confirmation
5. Only then run `git push origin main`

### Commit Messages

Follow conventional commit format with a co-author tag:

```
<Short summary (50 chars)>

<Detailed description with bullet points if needed>
- Point 1
- Point 2

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

Use `git commit -m "$(cat <<'EOF' ... EOF)"` for multi-line messages.

### Versioning

Version format: **MAJOR.MINOR.PATCH** (Semantic Versioning)

- **MAJOR**: Breaking API changes or major feature milestones
- **MINOR**: New features, API additions (backwards compatible)
- **PATCH**: Bug fixes, documentation updates

Version is stored in `src/version.h`:
```cpp
#define TINKLINK_VERSION_MAJOR 1
#define TINKLINK_VERSION_MINOR 3
#define TINKLINK_VERSION_PATCH 0
#define TINKLINK_VERSION_STRING "1.3.0"
```

### Config Backup Format Versioning

The config backup JSON includes a `"version"` field using **MAJOR.MINOR** format (currently `"1.2"`). This is **separate from firmware versioning** — it tracks the backup file format, not the firmware release.

**Version rules:**
- **Major bump** = breaking change (removed/renamed properties, type changes). Restore will **reject** backups with a higher major version.
- **Minor bump** = non-breaking change (new properties added). Restore will **accept** any minor version within the same major.

**Compatibility policy:**
- Aim to **never make breaking changes**. Add new fields, don't remove or rename existing ones.
- If a breaking change is unavoidable, bump the major version and add migration logic to the restore handler (or fail gracefully with a clear error).
- Legacy backups without a `"version"` field are always accepted (treated as pre-1.0).

**When to bump:**
- Adding a new config section or property → bump minor (e.g., `1.0` → `1.1`)
- Removing, renaming, or changing the type of a property → bump major (e.g., `1.0` → `2.0`)
- Bug fixes to backup/restore logic → no version bump needed

The version constant is in the `handleApiConfigBackup()` method in `WebServer.cpp`.

### Git Tags

After significant releases, create annotated tags. Tag messages should include a one-line summary followed by a bullet point for each major change area — matching the level of detail used in the changelog (`docs/changelog.md`):

```bash
git tag -a v1.9.0 -m "$(cat <<'EOF'
Release v1.9.0: Pluggable switcher architecture, ESP32-C3 support, and power management

- Pluggable switcher architecture — Abstract Switcher base class with SwitcherFactory; renamed ExtronSwVga to ExtronSwVgaSwitcher; all Extron-specific naming generalized
- Serial transport refactoring — New UartSerial class; each device class owns its serial transport internally
- ConfigManager refactoring — Replaced typed config structs with raw JsonDocument storage; new accessors and "tink" config section
- RetroTINK power management — Configurable serialMode (usb/uart) and powerManagementMode (off/simple/full)
- ESP32-C3 support — New env:esp32c3 environment; NO_USB_HOST build flag; separate data_c3/ directory
- LED configuration — New ledColorOrder config field; runtime color order selection; GPIO8 support for C3
- Documentation — New ALTERNATIVE_BOARDS.md guide
EOF
)"
git push origin v1.9.0
```

## Code Style & Conventions

### File Naming

**Source files must match their class names** (PascalCase):

✅ Good:
- `ExtronSwVga.h` / `ExtronSwVga.cpp` → `class ExtronSwVga`
- `WifiManager.h` / `WifiManager.cpp` → `class WifiManager`
- `WebServer.h` / `WebServer.cpp` → `class WebServer`

❌ Bad:
- `extron_sw_vga.h` (uses snake_case)
- `wifi_mgr.h` (abbreviation doesn't match class name)

### Header Documentation

**All public APIs must be documented** in header files using Doxygen-style comments:

```cpp
/**
 * Brief description of what the class/function does.
 *
 * Longer description with usage notes, examples, or important details.
 *
 * @param paramName Description of parameter
 * @return Description of return value
 */
class MyClass {
public:
    /**
     * Initialize the component.
     * @return true on success
     */
    bool begin();

    /**
     * Get the current state.
     * @return Current state value
     */
    int getState() const { return _state; }
};
```

Document:
- Classes (purpose, usage pattern)
- Public methods (parameters, return values, side effects)
- Structs and enums
- Important constants/defines

Private methods don't require documentation unless complex.

### Include Guards & Formatting

Use `#ifndef` guards matching the filename:

```cpp
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

// content

#endif // WEB_SERVER_H
```

Include order:
1. Corresponding header (for .cpp files)
2. System headers (`<Arduino.h>`)
3. Library headers (`<ESPAsyncWebServer.h>`)
4. Project headers (`"WifiManager.h"`)

## API Design Conventions

### REST API Structure

APIs are organized by resource:

```
/api/status              - System status
/api/wifi/*              - WiFi operations
/api/tink/*              - RetroTINK operations
/api/v1/*                - Retro-Bridge compatible API (RetroBridge class; for the RT4K Profiler / Remote apps)
/api/switcher/*          - Video switcher operations
/api/avr/*               - Denon/Marantz AVR operations
/api/config/*            - Configuration (triggers, tink, AVR, backup/restore)
/api/debug/*             - Debug utilities
/api/logs                - System logs
/api/ota/*               - OTA updates
/api/system/reboot       - Device reboot
```

### Response Format

All successful API responses include `"status": "ok"`:

```json
{
  "status": "ok",
  "data": "..."
}
```

Error responses use HTTP status codes with error field:

```json
{
  "error": "Description of what went wrong"
}
```

### Configuration Files

Configuration is split into two JSON files in LittleFS:

**`/config.json`** - Hardware and application settings:
```json
{
  "switcher": {
    "type": "Extron SW VGA",
    "txPin": 43,
    "rxPin": 44
  },
  "hostname": "tinklink",
  "triggers": [...]
}
```

**`/wifi.json`** - WiFi credentials (separate for easy clearing):
```json
{
  "ssid": "NetworkName",
  "password": "password",
  "hostname": "tinklink"
}
```

**Important:** `data/wifi.json` is gitignored and must exist locally with your WiFi credentials. It gets baked into the LittleFS image on every filesystem build. Without it, the device will boot into AP mode.

### Modular Design

The project is designed to support different video switcher types:

- Use `switcher` instead of hardcoded `extron` in APIs/configs
- Switcher classes provide `getTypeName()` for identification
- Config stores switcher type for future extensibility

## Development Workflow

### Building & Uploading

```bash
# Build firmware
pio run -e esp32s3

# Build filesystem
pio run -t buildfs -e esp32s3

# Upload via USB (requires bootloader mode since CDC is disabled)
pio run -t upload -e esp32s3
pio run -t uploadfs -e esp32s3

# Upload via OTA (preferred - device runs in USB OTG mode)
pio run -t ota -e esp32s3
pio run -t buildfs -t otafs -e esp32s3   # buildfs is required: otafs uploads the existing image
```

### Remote Debugging

When USB CDC is unavailable (USB OTG mode), use:

```bash
# Tail logs over WiFi
python3 scripts/logs.py

# Or access via web interface
curl http://tinklink.local/api/logs
```

### Testing Changes

Before committing significant changes:
1. Build to verify compilation
2. Upload to device
3. Test functionality via web interface
4. Check logs for errors
5. Verify API responses with curl

## Common Tasks

### Adding a New API Endpoint

1. Add route in `WebServer::setupRoutes()`
2. Implement handler method
3. Update header documentation
4. Update `data/api.html` with endpoint docs, then copy changed HTML to `data_c3/` (the web pages are identical; only `config.json` differs)
5. Test with curl

### Adding Configuration Options

1. Update config struct in `ConfigManager.h`
2. Update `loadConfig()` and `saveConfig()` in `ConfigManager.cpp`
3. Update default `data/config.json`
4. Update web UI if needed
5. Update default `data_c3/config.json` if the option applies to ESP32-C3
6. Update `docs/configuration.md` with the new field (type, defaults, description)
7. **Ensure the API handler applies the change live** — all user-facing config must take effect without a reboot. If the config controls an object lifecycle (create/destroy), use the pointer-to-pointer pattern (see `DenonAvr**` in WebServer).

### Changing Pin Assignments

Default pins are in `ConfigManager` constructor. Can be overridden in `config.json`:

```json
{
  "switcher": {
    "txPin": 43,
    "rxPin": 44
  }
}
```

## Testing & Quality

### Pre-Commit Checklist

- [ ] Code compiles without warnings
- [ ] Public APIs documented in headers
- [ ] File names match class names
- [ ] Configuration changes reflected in default config.json
- [ ] API changes documented in api.html
- [ ] Changes tested on hardware
- [ ] Commit message follows convention
- [ ] User approved push to GitHub

### Build Validation

Always check build output for:
- Compilation warnings
- Flash usage (should be < 90%)
- RAM usage (should be < 70%)

```
RAM:   [=         ]  14.4% (used 47308 bytes from 327680 bytes)
Flash: [=======   ]  68.7% (used 900405 bytes from 1310720 bytes)
```

## Security Considerations

### Known Limitations

These are acknowledged but not currently addressed:
- WiFi passwords stored in plaintext in LittleFS
- OTA updates not authenticated/encrypted
- Web interface has no authentication

Don't proactively "fix" these unless user requests it.

### WiFi Hostname (ESP32 Arduino Quirks)

The DHCP hostname requires careful handling on ESP32 Arduino:
- `WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE)` must be called
  before `WiFi.setHostname()` — without it, the DHCP client ignores the hostname.
- `WiFi.mode()` resets the hostname to the default (`esp32s3-XXXX`), so
  `WiFi.config()` + `WiFi.setHostname()` must be called after every mode change.
- `WiFi.setAutoReconnect(true)` causes the ESP32 WiFi stack to silently reconnect
  with the default hostname, bypassing application code. Auto-reconnect is disabled;
  all reconnection is handled by WifiManager's state machine.

### mDNS and IPv6

- Apple's resolver queries A and AAAA for `.local` names together and waits for both. The IDF mDNS responder only answers AAAA when the interface has an IPv6 address (it can't send a negative NSEC answer), so without one **every lookup of `tinklink.local` takes exactly 5 s**. `WifiManager::setupMDNS()` therefore calls `WiFi.enableIpV6()` / `WiFi.softAPenableIpV6()` (link-local only) on every connect and mode change. Don't remove it. Check with `curl -w '%{time_namelookup}' http://tinklink.local/api/v1/info` (expect a few ms).
- Clients prefer the IPv6 address once it is advertised, so every TCP server must accept IPv6. **AsyncTCP is vendored in `lib/AsyncTCP/`** with a dual-stack listener patch (see `TINKLINK_PATCH.md` there). `AsyncClient::remoteIP()` returns 0.0.0.0 for IPv6 peers. Any new listener (raw lwIP, `WiFiServer`, ...) needs the same care, or name-based clients will stall on it.
- The responder renames itself after a name conflict (`tinklink-2`) and this IDF version has no getter; `WifiManager::getMdnsName()` finds the name by probing `mdns_hostname_exists()`.

### RetroTINK Serial Link Sharing

- `RetroTink::rawOpen()` gives one caller (a Retro-Bridge transaction or lease) exclusive, binary-safe use of the link. While it is open, `sendCommand()` defers TinkLink's own commands until `rawClose()`. Anything new that talks to the RT4K must go through `sendCommand()` or the raw channel, never straight to the transport.
- `UsbHostSerial` handles USB events in its own high-priority tasks and keeps several bulk IN transfers queued. The FT232R's 256-byte FIFO overruns after ~1.3 ms unpolled at 2 Mbaud, so never move receive handling back into `loop()`. The RX ring buffer is single-producer (USB client task) / single-consumer (loop task).
- RTS/CTS is enabled on the FTDI because the RT4K needs it to receive long writes at 2 Mbaud. The RT4K does not throttle its own output, so the receive path must keep up unaided.
- Power state (FULL mode) comes from `[COM]` replies, not status text: RT4K firmware 1.75+ no longer sends `[MCU]` lines over serial (`quiet 0` re-enables them but resets on every sleep). `pwr on` answers `Power On Requested` when asleep and `Bad Command: pwr on` when on; a sleeping RT4K ignores everything else; SVS commands never get a reply. Every input change therefore goes `pwr on` -> (poll `ver` if it was asleep, ~5 s) -> profile command. Test with `POST /api/tink/trigger`.
- CORS / Private Network Access headers are sent on `/api/v1/*` only. Don't add them globally: that would expose WiFi config and OTA to any web page.
- ESPAsyncWebServer is **vendored in `lib/ESPAsyncWebServer/`** with an opt-in HTTP keep-alive patch (`request->setKeepAlive(true)`, see `TINKLINK_PATCH.md` there); PlatformIO restores registry packages on every build, so it can't be patched in `.pio/libdeps`. Only `RetroBridge` opts in. Keep-alive matters: the Profiler aborts lease reads after 2 s, and without it every request costs an extra round trip.
- A `no_ack` lease must never report completion early (the app stops reading and the transfer dies). `RetroBridge` reports it when the RT4K's end-of-transfer line is the last thing received, or after 3 s of silence with the app caught up.
- Retro-Bridge handlers block the web server task while waiting on serial data (bounded, feeding the task watchdog). Keep lease reads short; transactions may take as long as the RT4K takes to answer.

### Safe Practices

- Never commit credentials or secrets
- Use `.gitignore` to exclude sensitive files
- Warn user before committing `.env` or credentials

## Project Structure

```
tink-link-usb/
├── data/              # Web interface files (served from LittleFS)
│   ├── index.html     # Status page
│   ├── config.html    # WiFi configuration
│   ├── debug.html     # System console
│   ├── api.html       # API documentation
│   ├── style.css      # Global styles
│   ├── config.json    # Default configuration
│   └── wifi.json      # WiFi credentials (gitignored, must exist locally)
├── src/               # Application source code
│   ├── main.cpp       # Entry point
│   ├── WebServer.*    # HTTP server & API handlers
│   ├── WifiManager.*  # WiFi connection management
│   ├── ConfigManager.* # Configuration persistence
│   ├── UsbHostSerial.* # USB Host FTDI serial driver
│   ├── SerialInterface.h # Abstract serial interface
│   ├── TelnetSerial.* # TCP telnet serial transport
│   ├── ExtronSwVga.*  # Video switcher protocol handler
│   ├── RetroTink.*    # RetroTINK 4K controller (USB Host)
│   ├── RetroBridge.*  # Retro-Bridge compatible API (/api/v1/*) over RetroTink's raw channel
│   ├── DenonAvr.*     # Denon/Marantz AVR controller (telnet + SSDP)
│   ├── Logger.*       # Centralized logging
│   └── version.h      # Version definitions
├── scripts/           # Helper scripts
│   ├── ota_upload.py  # OTA firmware uploader
│   └── logs.py        # Remote log viewer
├── lib/
│   ├── AsyncTCP/          # Vendored TCP library with TinkLink's dual-stack (IPv4 + IPv6) patch
│   └── ESPAsyncWebServer/ # Vendored web server library with TinkLink's keep-alive patch
├── platformio.ini     # PlatformIO configuration
├── README.md          # Short user-facing overview (see Documentation below)
├── docs/              # Detailed docs: hardware, building, configuration, troubleshooting, changelog, history
└── CLAUDE.md          # This file (AI assistant guide)
```

### Documentation Layout

- **`README.md` is for people deciding whether and how to use TinkLink.** Keep it short: what it does, what you need, quick start, links. No release history, no internals (class names, state names, config keys), no deep hardware detail.
- Detail goes in `docs/`: `hardware.md`, `building.md`, `configuration.md`, `alternative-boards.md`, `troubleshooting.md`, `retrotink-serial.md`, `changelog.md` (release notes, newest first), `history.md` (project background).
- On a release: add the entry to `docs/changelog.md` and update the version line in `README.md`.

## Additional Notes

### LED Status Indicators

- **Green**: WiFi connected
- **Yellow**: WiFi connecting
- **Red**: WiFi failed
- **Blue (blinking)**: Access Point mode
- **Black**: Disconnected

### UART Configuration

- **Switcher UART**: GPIO43 (TX), GPIO44 (RX) @ 9600 baud, 8N1
- **RetroTINK USB**: USB Host via EspUsbHost library, FTDI FT232R @ 2,000,000 baud (RT4K firmware 1.75+ default; was 115200)

### Important GPIO Pins

- GPIO21: WS2812 RGB LED
- GPIO43: Switcher TX (UART0)
- GPIO44: Switcher RX (UART0)
- GPIO19/20: USB Host D-/D+ (USB OTG mode)

## References

- **User Documentation**: README.md (overview) and `docs/`
- **Configuration Reference**: docs/configuration.md
- **Changelog**: docs/changelog.md
- **API Reference**: http://tinklink.local/api.html (when device is running)
- **Repository**: https://github.com/smudgeface/tink-link-usb
- **HEOS CLI Protocol Specification**: assets/docs/HEOS_CLI_Protocol_Specification.pdf
- **Denon AVR Network Ports**: https://manuals.denon.com/EUsecurity/EU/EN/index.php

### Live Configuration

All user-facing configuration changes must apply immediately without requiring a reboot:

- **WiFi** — `connect()` applies immediately
- **Triggers** — Clears and reloads into RetroTink on save
- **AVR enable/disable** — Creates or destroys instance at runtime via `DenonAvr**` pointer-to-pointer
- **AVR settings** (IP, input) — Reconfigures live instance
- **RetroTINK baud rate** — Applied to the running serial transport (USB change is deferred to the loop task)
- **Retro-Bridge API enable** (`tink.retroBridgeApi`) — Toggled live; disabling releases any active lease

Hardware-level settings (switcher type, RetroTink serial mode, pin assignments) are boot-only since they represent physical hardware that doesn't change at runtime.

### Config Backup Format Versioning

The config backup JSON includes a `"version"` field using **MAJOR.MINOR** format (currently `"1.2"`). This is **separate from firmware versioning** — it tracks the backup file format, not the firmware release.

**Version rules:**
- **Major bump** = breaking change (removed/renamed properties, type changes). Restore will **reject** backups with a higher major version.
- **Minor bump** = non-breaking change (new properties added). Restore will **accept** any minor version within the same major.

**Compatibility policy:**
- Aim to **never make breaking changes**. Add new fields, don't remove or rename existing ones.
- If a breaking change is unavoidable, bump the major version and add migration logic to the restore handler (or fail gracefully with a clear error).
- Legacy backups without a `"version"` field are always accepted (treated as pre-1.0).

**When to bump:**
- Adding a new config section or property → bump minor (e.g., `1.0` → `1.1`)
- Removing, renaming, or changing the type of a property → bump major (e.g., `1.0` → `2.0`)
- Bug fixes to backup/restore logic → no version bump needed

The version constant is in the `handleApiConfigBackup()` method in `WebServer.cpp`.

---

**Last Updated**: 2026-09-19 (v1.13.1)
