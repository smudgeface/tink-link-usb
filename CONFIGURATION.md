# Configuration Reference

TinkLink-USB uses two JSON configuration files stored in the device's LittleFS filesystem:

- **`wifi.json`** — WiFi credentials (SSID, password, hostname)
- **`config.json`** — All other settings (switcher, RetroTINK, hardware, AVR, triggers)

These files live in the `data/` directory for ESP32-S3 builds and `data_c3/` for ESP32-C3 builds. The appropriate directory is uploaded to the device via `pio run -t uploadfs`.

## wifi.json

Contains WiFi network credentials and device hostname.

| Field | Type | Description |
|-------|------|-------------|
| `ssid` | string | WiFi network name to connect to |
| `password` | string | WiFi network password |
| `hostname` | string | (Optional) mDNS hostname for this device |

**Notes:**
- Credentials are stored in plaintext in LittleFS (acknowledged limitation)
- If `hostname` is present in `wifi.json`, it takes precedence over `config.json`
- The hostname field can be omitted; defaults to value in `config.json`

**Example:**
```json
{
  "ssid": "MyNetwork",
  "password": "mypassword"
}
```

## config.json

Contains all hardware and application settings. Organized into sections:

### hostname

| Field | Type | Description |
|-------|------|-------------|
| `hostname` | string | mDNS hostname for the device (e.g., `tinklink.local`) |

**Notes:**
- Overridden by `hostname` field in `wifi.json` if present
- Default: `tinklink` (S3) or `tinklink-c3` (C3)

---

### switcher

Configures the video switcher connection and behavior.

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Switcher model name (e.g., `"Extron SW VGA"`) |
| `uartId` | integer | UART peripheral number (0, 1, or 2) |
| `txPin` | integer | GPIO pin for transmit (TX) |
| `rxPin` | integer | GPIO pin for receive (RX) |
| `autoSwitch` | boolean | Enable automatic RetroTINK profile switching on input change |

**Supported Types:**
- `"Extron SW VGA"` — Extron SW VGA switcher family

**Notes:**
- Baud rate is fixed at **9600 baud, 8N1**
- The switcher must actively report input changes via UART
- Signal detection behavior depends on switcher model (e.g., Extron SW VGA reports `"Out1 In2 RGB"` when input 2 goes active)

---

### tink

Configures the RetroTINK 4K connection and power management.

| Field | Type | Description |
|-------|------|-------------|
| `serialMode` | string | Communication mode: `"usb"` or `"uart"` |
| `powerManagementMode` | string | Power management strategy: `"off"`, `"simple"`, or `"full"` |
| `uartId` | integer | UART peripheral number (0, 1, or 2) — only used when `serialMode` is `"uart"` |
| `txPin` | integer | GPIO pin for transmit (TX) — only used when `serialMode` is `"uart"` |
| `rxPin` | integer | GPIO pin for receive (RX) — only used when `serialMode` is `"uart"` |

**Serial Modes:**
- `"usb"` — Use USB Host (ESP32-S3 only, requires EspUsbHost library and FTDI FT232R)
- `"uart"` — Use hardware UART (available on both S3 and C3)

**Power Management Modes:**

| Mode | Description |
|------|-------------|
| `"off"` | No power management — RetroTINK always considered powered on |
| `"simple"` | Send menu commands only when RetroTINK is detected (checks for `TINK` prompt) |
| `"full"` | Auto-detects power state, sends wake command if needed, waits for ready state before commands |

**Notes:**
- Baud rate is fixed at **115200 baud, 8N1** for UART mode
- USB mode uses the FTDI FT232R driver via USB OTG (S3 only)
- Full power management provides the most reliable operation with automatic wake/sleep handling

---

### hardware

Configures LED status indicator and other hardware settings.

| Field | Type | Description |
|-------|------|-------------|
| `ledPin` | integer | GPIO pin for WS2812 RGB LED |
| `ledColorOrder` | string | Color channel order: `"RGB"` or `"GRB"` |

**Supported LED Pins:**
- **ESP32-S3:** GPIO21 (default)
- **ESP32-C3:** GPIO8 (default)

**Color Order:**
- Use `"RGB"` for most WS2812 LEDs
- Use `"GRB"` if colors appear incorrect (common on some C3 boards)

**Notes:**
- The LED displays WiFi status (green = connected, yellow = connecting, red = failed, blue blinking = AP mode)
- Color order varies by LED manufacturer; adjust if colors don't match expected behavior

---

### avr

Configures optional Denon/Marantz AVR control.

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | AVR model name (e.g., `"Denon X4300H"`) |
| `enabled` | boolean | Enable AVR integration |
| `ip` | string | AVR IP address (empty string if unknown; SSDP discovery will find it) |
| `input` | string | AVR input name to switch to (e.g., `"GAME"`, `"CBL/SAT"`, `"MPLAY"`) |

**Notes:**
- Uses telnet protocol on port 23 (Denon/Marantz network control)
- SSDP discovery automatically finds AVR if `ip` is empty or invalid
- Common input names: `"GAME"`, `"CBL/SAT"`, `"MPLAY"`, `"DVD"`, `"BD"`, `"SAT/CBL"`, `"NET"`, `"AUX1"`, `"AUX2"`
- Input names are model-specific; consult your AVR manual for exact values

---

### triggers

Defines mappings from switcher inputs to RetroTINK profiles.

Array of trigger objects. Each trigger has:

| Field | Type | Description |
|-------|------|-------------|
| `input` | integer | Switcher input number (1-based) |
| `profile` | integer | RetroTINK profile slot number (0-29) |
| `mode` | string | Profile type: `"Remote"` or `"SVS"` |
| `name` | string | Friendly name for this input (e.g., `"NES"`, `"SNES"`) |

**Modes:**
- `"Remote"` — Standard RetroTINK profiles (accessed via remote control, slots 0-9)
- `"SVS"` — Source-Voltage-Sync profiles (auto-selected by RetroTINK based on signal properties, slots 0-29)

**Notes:**
- Trigger order doesn't matter; inputs are matched by `input` number
- Profiles are zero-indexed internally but often displayed as 1-based in RetroTINK UI
- Name field is for user reference only; not sent to RetroTINK

**Example:**
```json
"triggers": [
  {
    "input": 1,
    "mode": "Remote",
    "profile": 1,
    "name": "NES"
  },
  {
    "input": 2,
    "mode": "SVS",
    "profile": 15,
    "name": "PS2"
  }
]
```

---

## Platform Defaults

TinkLink-USB provides different default configurations for ESP32-S3 and ESP32-C3 due to hardware differences.

| Setting | ESP32-S3 Default | ESP32-C3 Default | Notes |
|---------|------------------|------------------|-------|
| **hostname** | `tinklink` | `tinklink-c3` | Different to avoid mDNS conflicts |
| **tink.serialMode** | `usb` | `uart` | C3 lacks USB Host hardware |
| **tink.uartId** | 2 | 1 | Different UART peripheral |
| **tink.txPin** | 17 | 0 | Different GPIO |
| **tink.rxPin** | 18 | 1 | Different GPIO |
| **switcher.uartId** | 1 | 0 | Different UART peripheral |
| **switcher.txPin** | 43 | 21 | Different GPIO |
| **switcher.rxPin** | 44 | 20 | Different GPIO |
| **hardware.ledPin** | 21 | 8 | Different GPIO |
| **hardware.ledColorOrder** | `RGB` | `GRB` | C3 boards often use GRB LEDs |

### ESP32-S3 Default (data/config.json)

```json
{
  "switcher": {
    "type": "Extron SW VGA",
    "uartId": 1,
    "txPin": 43,
    "rxPin": 44,
    "autoSwitch": true
  },
  "tink": {
    "serialMode": "usb",
    "powerManagementMode": "full",
    "uartId": 2,
    "txPin": 17,
    "rxPin": 18
  },
  "hardware": {
    "ledPin": 21,
    "ledColorOrder": "RGB"
  },
  "avr": {
    "type": "Denon X4300H",
    "enabled": false,
    "ip": "",
    "input": "GAME"
  },
  "hostname": "tinklink",
  "triggers": [
    {
      "input": 1,
      "mode": "Remote",
      "profile": 1,
      "name": "NES"
    },
    {
      "input": 2,
      "mode": "Remote",
      "profile": 2,
      "name": "SNES"
    }
  ]
}
```

### ESP32-C3 Default (data_c3/config.json)

```json
{
  "switcher": {
    "type": "Extron SW VGA",
    "uartId": 0,
    "txPin": 21,
    "rxPin": 20,
    "autoSwitch": true
  },
  "tink": {
    "serialMode": "uart",
    "powerManagementMode": "full",
    "uartId": 1,
    "txPin": 0,
    "rxPin": 1
  },
  "hardware": {
    "ledPin": 8,
    "ledColorOrder": "GRB"
  },
  "avr": {
    "type": "Denon X4300H",
    "enabled": false,
    "ip": "",
    "input": "GAME"
  },
  "hostname": "tinklink-c3",
  "triggers": [
    {
      "input": 1,
      "mode": "Remote",
      "profile": 1,
      "name": "NES"
    },
    {
      "input": 2,
      "mode": "Remote",
      "profile": 2,
      "name": "SNES"
    }
  ]
}
```

---

## Editing Configuration

There are three ways to modify configuration:

### 1. Web Interface (Recommended)

Visit `http://tinklink.local/config.html` to edit WiFi settings via the web UI. Other settings can be modified via API endpoints.

### 2. Upload Modified Files

1. Edit `data/config.json` or `data/wifi.json` in your project directory
2. Upload to device:
   ```bash
   pio run -e esp32s3 -t uploadfs
   # or for ESP32-C3:
   pio run -e esp32c3 -t uploadfs
   ```

**Note:** The `ota_upload.py` script automatically backs up and restores device configuration during filesystem uploads, so runtime settings (WiFi credentials, triggers, AVR config) are preserved.

### 3. API Endpoints

Use the REST API to modify configuration programmatically. All changes take effect immediately — no reboot required.

- `POST /api/wifi/connect` — Connect to WiFi network
- `POST /api/config/triggers` — Update trigger mappings
- `POST /api/config/avr` — Update AVR settings (enable/disable, IP, input)
- `GET /api/config/backup` — Download all config as JSON
- `POST /api/config/restore` — Restore config from backup JSON (reboot to apply)
- `POST /api/system/reboot` — Reboot the device

See `http://tinklink.local/api.html` for complete API documentation.

---

## Live Configuration

All user-facing settings apply immediately when changed via the web interface or REST API — no reboot is required.

| Setting | Live? | Notes |
|---------|-------|-------|
| WiFi credentials | ✅ | Connects to new network immediately |
| Triggers | ✅ | Reloads into RetroTink on save |
| AVR enable/disable | ✅ | Creates or destroys AVR instance at runtime |
| AVR settings (IP, input) | ✅ | Reconfigures live instance |
| Switcher type | ❌ | Hardware config, set at boot |
| RetroTink serial mode | ❌ | Hardware config, set at boot |
| Pin assignments | ❌ | Hardware config, set at boot |

Hardware-level settings (switcher type, serial mode, pins) only apply at boot since they represent physical hardware that doesn't change at runtime. These are typically set once during initial setup.

---

**Last Updated:** 2026-02-16 (v1.9.5)
