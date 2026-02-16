# Claude Development Guide for TinkLink-USB

This document provides guidelines and conventions for Claude (AI assistant) when working on the TinkLink-USB project.

## Project Overview

TinkLink-USB is an ESP32-S3 USB bridge between video switchers and the RetroTINK 4K. It automatically triggers RetroTINK profile changes when video switcher inputs change.

**Current Status:** Active development. USB Host, WiFi, LED, Web Console, OTA, Denon AVR control, and SSDP discovery all functional. Version 1.9.3.

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

### Git Tags

After significant releases, create annotated tags. Tag messages should include a one-line summary followed by a bullet point for each major change area — matching the level of detail used in the README changelog:

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
/api/switcher/*          - Video switcher operations
/api/avr/*               - Denon/Marantz AVR operations
/api/config/avr          - AVR configuration
/api/debug/*             - Debug utilities
/api/logs                - System logs
/api/ota/*               - OTA updates
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
pio run -t otafs -e esp32s3
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
4. Update `data/api.html` with endpoint docs
5. Test with curl

### Adding Configuration Options

1. Update config struct in `ConfigManager.h`
2. Update `loadConfig()` and `saveConfig()` in `ConfigManager.cpp`
3. Update default `data/config.json`
4. Update web UI if needed
5. Update default `data_c3/config.json` if the option applies to ESP32-C3
6. Update `CONFIGURATION.md` with the new field (type, defaults, description)

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
│   └── wifi.json      # WiFi credentials template
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
│   ├── DenonAvr.*     # Denon/Marantz AVR controller (telnet + SSDP)
│   ├── Logger.*       # Centralized logging
│   └── version.h      # Version definitions
├── scripts/           # Helper scripts
│   ├── ota_upload.py  # OTA firmware uploader
│   └── logs.py        # Remote log viewer
├── platformio.ini     # PlatformIO configuration
├── README.md          # User documentation
├── CLAUDE.md          # This file (AI assistant guide)
└── CONFIGURATION.md   # Configuration reference for all settings
```

## Additional Notes

### LED Status Indicators

- **Green**: WiFi connected
- **Yellow**: WiFi connecting
- **Red**: WiFi failed
- **Blue (blinking)**: Access Point mode
- **Black**: Disconnected

### UART Configuration

- **Switcher UART**: GPIO43 (TX), GPIO44 (RX) @ 9600 baud, 8N1
- **RetroTINK USB**: USB Host via EspUsbHost library, FTDI FT232R @ 115200 baud

### Important GPIO Pins

- GPIO21: WS2812 RGB LED
- GPIO43: Switcher TX (UART0)
- GPIO44: Switcher RX (UART0)
- GPIO19/20: USB Host D-/D+ (USB OTG mode)

## References

- **User Documentation**: README.md
- **Configuration Reference**: CONFIGURATION.md
- **API Reference**: http://tinklink.local/api.html (when device is running)
- **Repository**: https://github.com/smudgeface/tink-link-usb
- **HEOS CLI Protocol Specification**: assets/docs/HEOS_CLI_Protocol_Specification.pdf
- **Denon AVR Network Ports**: https://manuals.denon.com/EUsecurity/EU/EN/index.php

---

**Last Updated**: 2026-02-15 (v1.9.3)
