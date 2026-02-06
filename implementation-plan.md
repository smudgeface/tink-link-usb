# TinkLink-USB Implementation Plan

## Project Overview

**Goal**: ESP32-S3 USB Host bridge between Extron video switchers and RetroTINK 4K

**Key Innovation**: Direct USB communication with RetroTINK 4K instead of VGA serial pins
- Eliminates need for custom VGA adapter and RS-232 level shifter
- Uses native USB serial interface (FTDI FT232R) at 115200 baud
- Single USB OTG cable provides power and communication

**Hardware**: Waveshare ESP32-S3-Zero
- ESP32-S3FH4R2 dual-core @ 240MHz
- 4MB Flash, 2MB PSRAM
- USB OTG on GPIO19/20 (internal to Type-C port)
- Extron UART0 on GPIO43 (TX) / GPIO44 (RX)
- WS2812B RGB LED on GPIO21

**Reference Documentation**: See [README.md](README.md) for:
- Board specifications and pinout
- RetroTINK 4K serial protocol details
- USB Host configuration requirements
- Links to datasheets and reference projects

---

## Current Status

### ✅ Phase 3: USB Host FTDI Communication (COMPLETED)

**Status**: USB Host communication with RetroTINK 4K working and verified on hardware

**Completed Work**:
- [x] Created `UsbHostSerial` class (subclasses `EspUsbHostSerial_FTDI` from EspUsbHost library)
- [x] Ring buffer (512 bytes) for USB receive data with `readLine()` support
- [x] USB Host detects FTDI FT232R device (VID:0x0403, PID:0x6001) at 115200 baud
- [x] RetroTINK command framing: `\r<COMMAND>\r` (leading CR clears partial input)
- [x] RT4K power state tracking: UNKNOWN, WAKING, BOOTING, ON, SLEEPING
- [x] Boot complete detection via `[MCU] Boot Sequence Complete` serial message
- [x] Power-up detection via `Powering Up` serial message (transitions WAKING → BOOTING)
- [x] Power-off detection via `Power Off` or `Entering Sleep` serial messages
- [x] Auto-wake from SLEEPING: sends `pwr on`, sets BOOTING, queues command, waits for boot complete (15s timeout fallback)
- [x] Auto-wake from UNKNOWN: sends `pwr on`, sets WAKING, waits up to 3s for RT4K response. If `Powering Up` received → BOOTING (wait for boot). If no response → assume already ON, send command immediately.
- [x] SVS mode support with keep-alive (`SVS CURRENT INPUT=N` sent 1 second after `SVS NEW INPUT=N`)
- [x] Remote mode support (`remote profN`, `remote menu`, etc.)
- [x] Serial data sanitization: non-printable bytes replaced with `?` to prevent JSON parse failures in web console
- [x] Switched to USB OTG mode (`ARDUINO_USB_MODE=0`, `ARDUINO_USB_CDC_ON_BOOT=0`)
- [x] USB CDC serial debugging disabled; all debugging via web console and `scripts/logs.py`
- [x] RT4K connection status and power state exposed in `/api/status` response
- [x] Web debug console sends commands to RetroTINK via dropdown target selector
- [x] Debug console simplified to logs-only display with setTimeout-based polling (no duplicate entries)
- [x] Deployed and verified via OTA: commands confirmed working on actual RT4K display
- [x] Signal detection auto-switch: parses Extron `Sig` messages, 2-second debounce, highest active input wins
- [x] Signal-lost-and-restored handling: re-triggers input callback when signal returns on current input (enables RT4K auto-wake after power cycle)

**Key Files Added/Modified**:
- `src/UsbHostSerial.h`, `src/UsbHostSerial.cpp` - USB Host FTDI serial driver (new)
- `src/RetroTink.h`, `src/RetroTink.cpp` - Complete rewrite with USB Host, power state tracking, auto-wake
- `src/main.cpp` - Wired up UsbHostSerial, disabled CDC, 6-step init
- `src/WebServer.cpp` - Added `tink.connected` and `tink.powerState` to status API
- `src/version.h` - Bumped to 1.5.0
- `platformio.ini` - Switched to OTG mode, added EspUsbHost library dependency
- `data/debug.html` - Enabled tink commands, simplified to logs-only, fixed polling races
- `src/ExtronSwVga.h`, `src/ExtronSwVga.cpp` - Signal detection parsing, debounce, auto-switch logic
- `data/index.html` - Added RT4K connection/power state display, switcher type as status field
- `data/api.html` - Updated status response example with tink.connected/powerState, removed stub references

**Configuration**:
```ini
# platformio.ini - Phase 3 (current)
ARDUINO_USB_MODE=0           # OTG mode (USB Host for RT4K)
ARDUINO_USB_CDC_ON_BOOT=0    # CDC disabled
lib_deps = https://github.com/wakwak-koba/EspUsbHost.git
```

**Build Stats** (v1.6.0):
```
RAM:   [=         ]  14.6%
Flash: [=======   ]  72.4%
```

---

### ✅ Phase 2: WiFi, LED, Console, and OTA (COMPLETED)

**Status**: All Phase 2 functionality working and tested

**Completed Work**:
- [x] WS2812 RGB LED status indication (FastLED library)
- [x] LED colors: Blue blinking (AP), Yellow (connecting), Green (connected), Red (failed)
- [x] WiFi AP mode with automatic fallback
- [x] WiFi STA mode with credential persistence
- [x] mDNS working (http://tinklink.local)
- [x] All web pages functional (status, config, debug, API docs)
- [x] Web-based trigger configuration (add/edit/delete input mappings)
- [x] Centralized logging system with timestamps (Logger class)
- [x] Web-based System Console with live updates and target selector (Switcher/RetroTINK)
- [x] UART TX/RX to Extron via GPIO43/44 at 9600 baud 8N1
- [x] Remote log monitoring (`scripts/logs.py`)
- [x] OTA firmware updates via web interface
- [x] OTA filesystem updates via web interface
- [x] PlatformIO OTA automation (`pio run -t ota`, `pio run -t otafs`)
- [x] Standalone OTA upload script (`scripts/ota_upload.py`)
- [x] Configurable LED pin for multi-board support
- [x] Modular switcher design (renamed Extron to Switcher in APIs)

**Key Files Added/Modified**:
- `src/Logger.h`, `src/Logger.cpp` - Centralized logging with circular buffer
- `src/WebServer.cpp` - OTA upload handlers, logs API endpoint, trigger configuration API
- `src/RetroTink.cpp` - Added clearTriggers() method
- `data/index.html` - Status page with switcher and trigger display
- `data/config.html` - WiFi and trigger configuration interface
- `data/debug.html` - System Console UI with live updates, OTA upload UI
- `data/api.html` - REST API documentation
- `scripts/ota_upload.py` - OTA automation for PlatformIO
- `scripts/logs.py` - Remote log monitoring
- `platformio.ini` - Added `extra_scripts` for OTA targets
- `CLAUDE.md` - Development guide for AI assistants

**Configuration**:
```ini
# platformio.ini - Phase 2
ARDUINO_USB_MODE=1           # CDC mode (Serial debugging)
ARDUINO_USB_CDC_ON_BOOT=1    # Enable USB CDC

# OTA targets
extra_scripts = scripts/ota_upload.py
```

**OTA Usage**:
```bash
# Via PlatformIO
pio run -t ota -e esp32s3        # Upload firmware
pio run -t otafs -e esp32s3      # Upload filesystem

# Via Python script
python scripts/ota_upload.py firmware .pio/build/esp32s3/firmware.bin
python scripts/ota_upload.py fs .pio/build/esp32s3/littlefs.bin
```

---

### ✅ Phase 1: Base Framework (COMPLETED)

**Status**: Project compiles and is ready for ESP32-S3 hardware testing

**Completed Work**:
- [x] Project structure created
- [x] Ported all components from tink-link-lite:
  - ConfigManager (LittleFS-based configuration)
  - WifiManager (Station + AP fallback)
  - WebServer (status, config, debug pages)
  - ExtronSwVga (RS-232 UART handler)
- [x] RetroTINK module created in **stub mode**:
  - Parses triggers from config
  - Generates command strings (Remote/SVS modes)
  - Logs commands to serial (no actual transmission yet)
- [x] Pin assignments updated for ESP32-S3-Zero
- [x] CDC mode enabled for USB serial debugging
- [x] Build verified successful
- [x] Git repository initialized with commits

**Configuration**:
```ini
# platformio.ini - Phase 1
ARDUINO_USB_MODE=1           # CDC mode (Serial debugging)
ARDUINO_USB_CDC_ON_BOOT=1    # Enable USB CDC
```

**Files**:
- `src/main.cpp` - Application entry point
- `src/RetroTink.{h,cpp}` - Stub implementation (to be completed in Phase 2)
- `src/ConfigManager.{h,cpp}` - Configuration management
- `src/WifiManager.{h,cpp}` - WiFi connectivity
- `src/WebServer.{h,cpp}` - Web interface
- `src/ExtronSwVga.{h,cpp}` - Extron RS-232 handler
- `data/` - Web interface files (HTML/CSS)
- `assets/hardware/` - Hardware documentation and product images

---

## ✅ Phase 2: WiFi and LED Functionality Testing (COMPLETED)

**Goal**: Verify WiFi connectivity, AP fallback, and WS2812 LED status indication on ESP32-S3 hardware

**Status**: All tests passed, plus additional features implemented (logging, console, OTA)

### 2.1: Hardware Setup

**Connect ESP32-S3-Zero**:
- USB-C cable to computer for programming and serial debugging
- No other connections needed for WiFi testing

**LED Hardware**:
- WS2812B RGB LED on GPIO21 (onboard)
- Reference: [ESP32-S3-Zero Board Details](https://www.espboards.dev/esp32/esp32-s3-zero/)

### 2.2: WS2812 LED Control Implementation

**Objective**: Replace simple digitalWrite() with proper WS2812 RGB LED control

**Library Selection**:
- Use **FastLED** or **Adafruit_NeoPixel** library
- FastLED recommended for ESP32 compatibility

**Add to platformio.ini**:
```ini
lib_deps =
    bblanchon/ArduinoJson@^7.0.0
    me-no-dev/ESPAsyncWebServer@^1.2.3
    me-no-dev/AsyncTCP@^1.1.1
    fastled/FastLED@^3.6.0
```

**Update main.cpp**:

Current (placeholder) code:
```cpp
#define RGB_LED_PIN 21
// Simple digitalWrite() control
digitalWrite(RGB_LED_PIN, ledOn ? HIGH : LOW);
```

Replace with WS2812 control:
```cpp
#include <FastLED.h>

#define RGB_LED_PIN 21
#define NUM_LEDS 1
CRGB leds[NUM_LEDS];

void setup() {
    // Initialize WS2812 LED
    FastLED.addLeds<WS2812, RGB_LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(50);  // 0-255, adjust as needed
    leds[0] = CRGB::Black;      // Off initially
    FastLED.show();
    // ... rest of setup
}

void loop() {
    // ... other code

    // LED status indication
    if (wifiManager.getState() == WifiManager::State::AP_ACTIVE) {
        // Blink blue in AP mode (500ms interval)
        if (now - lastBlink >= 500) {
            lastBlink = now;
            ledOn = !ledOn;
            leds[0] = ledOn ? CRGB::Blue : CRGB::Black;
            FastLED.show();
        }
    } else if (wifiManager.getState() == WifiManager::State::CONNECTED) {
        // Solid green when connected
        leds[0] = CRGB::Green;
        FastLED.show();
    } else if (wifiManager.getState() == WifiManager::State::CONNECTING) {
        // Yellow when connecting
        leds[0] = CRGB::Yellow;
        FastLED.show();
    } else {
        // Off for other states
        leds[0] = CRGB::Black;
        FastLED.show();
    }
}
```

**LED Color Scheme**:
- **Blue blinking** (500ms) = Access Point mode active
- **Yellow solid** = Connecting to WiFi
- **Green solid** = Connected to WiFi network
- **Red** = Connection failed (optional)
- **Off** = Disconnected or initializing

### 2.3: WiFi Functionality Testing

**Test 1: First Boot (No WiFi Configuration)**

Expected behavior:
1. Device boots with no `wifi.json` file
2. Automatically starts AP mode
3. LED blinks blue (500ms interval)
4. Serial output shows:
   ```
   No WiFi credentials saved - starting Access Point mode
   ========================================
     Access Point Active
   ========================================
     SSID:     TinkLink-XXXXXX
     IP:       192.168.1.1
   ```

Steps:
- Upload firmware to ESP32-S3-Zero
- Monitor serial output
- Verify LED blinks blue
- Scan for WiFi networks on phone/laptop
- Confirm `TinkLink-XXXXXX` network appears

**Test 2: AP Mode WiFi Configuration**

Expected behavior:
1. Connect to TinkLink AP
2. Get automatic IP via DHCP (192.168.1.100-200 range)
3. Access web interface at http://192.168.1.1
4. Scan for networks via web UI
5. Select network, enter password, save
6. Device disconnects AP, connects to network

Steps:
- Connect phone/laptop to TinkLink AP
- Open browser to http://192.168.1.1
- Verify index.html loads
- Navigate to config.html
- Click "Scan" button
- Verify networks appear
- Select your 2.4GHz network
- Enter password
- Click "Save & Connect"
- Monitor serial output for connection attempt
- Verify LED changes from blinking blue to yellow to green
- Verify web interface accessible at http://tinklink.local

**Test 3: WiFi Credentials Persistence**

Expected behavior:
1. Credentials saved to `/wifi.json` in LittleFS
2. Device reboots and auto-connects
3. No AP mode started
4. LED goes yellow → green

Steps:
- With WiFi connected, press reset button on ESP32-S3-Zero
- Monitor serial output
- Verify shows "Attempting to connect to saved network: [SSID]"
- Verify LED shows yellow then green
- Verify web interface accessible at http://tinklink.local
- No AP network should be broadcasting

**Test 4: AP Fallback (Wrong Password)**

Expected behavior:
1. Modify `wifi.json` with incorrect password
2. Upload filesystem
3. Device attempts connection
4. Fails after retries (~60 seconds)
5. Falls back to AP mode
6. LED blinks blue again

Steps:
- Edit `data/wifi.json` with wrong password
- Upload filesystem: `pio run -t uploadfs`
- Reset device
- Monitor serial output
- Verify connection attempts and failures
- Verify fallback to AP mode after timeout
- Verify LED returns to blinking blue
- Verify TinkLink AP network broadcasting

**Test 5: AP Fallback (Network Unavailable)**

Expected behavior:
1. Configure valid credentials for network that's powered off
2. Device attempts connection
3. Fails after retries
4. Falls back to AP mode

Steps:
- Power off your WiFi router temporarily
- Reset ESP32-S3-Zero
- Monitor serial output for connection attempts
- Verify fallback to AP mode
- Power router back on
- Manually reconnect via AP mode web interface

**Test 6: mDNS Resolution**

Expected behavior:
1. Device connected to WiFi
2. Accessible via http://tinklink.local
3. No need for IP address

Steps:
- With device connected to WiFi
- Open browser to http://tinklink.local
- Verify web interface loads
- Test on multiple devices (computer, phone, tablet)
- Note: Windows may require Bonjour service

**Test 7: Web Interface Pages**

Verify all pages load correctly:
- `/` - Status page (index.html)
  - Shows WiFi status
  - Shows current Extron input (0 if not connected)
  - Shows last RetroTINK command
  - Lists configured triggers
- `/config.html` - WiFi configuration page
  - Scan button works
  - Network list populates
  - Can save credentials
  - Can disconnect
- `/debug.html` - Debug page
  - Can send raw RetroTINK commands (logged only in Phase 1)
  - Can simulate Extron input changes
  - Can send continuous test signals

**Test 8: API Endpoints**

Test all API endpoints via curl or browser:
```bash
# Status
curl http://tinklink.local/api/status

# Scan networks
curl http://tinklink.local/api/scan

# Connect (POST)
curl -X POST http://tinklink.local/api/connect \
  -d "ssid=YourNetwork&password=YourPassword"

# Disconnect
curl -X POST http://tinklink.local/api/disconnect

# Save credentials
curl -X POST http://tinklink.local/api/save \
  -d "ssid=YourNetwork&password=YourPassword"

# Debug: Send command
curl -X POST http://tinklink.local/api/debug/send \
  -d "command=remote prof1"

# Debug: Simulate input
curl -X POST http://tinklink.local/api/debug/simulate \
  -d "input=1"

# Debug: Continuous test
curl -X POST http://tinklink.local/api/debug/continuous \
  -d "count=5"
```

### 2.4: Success Criteria

Phase 2 is complete when:
- ✅ WS2812 LED displays correct colors for each WiFi state
- ✅ AP mode starts automatically when no credentials exist
- ✅ Can configure WiFi via web interface in AP mode
- ✅ Credentials persist across reboots
- ✅ Auto-connects to saved network on boot
- ✅ Falls back to AP mode after connection failures (~60s)
- ✅ mDNS resolution works (http://tinklink.local)
- ✅ All web pages load correctly
- ✅ All API endpoints respond as expected
- ✅ Serial output provides clear status messages
- ✅ No crashes or hangs during WiFi state transitions
- ✅ **BONUS**: Centralized logging system with web console
- ✅ **BONUS**: OTA firmware and filesystem updates
- ✅ **BONUS**: PlatformIO OTA automation scripts

**Resolved Issues**:
- LED color order corrected (RGB not GRB for ESP32-S3-Zero)
- Flash size fixed (4MB not 8MB)
- UART pins configured correctly (GPIO43/44)

---

## ✅ Phase 3: USB Host FTDI Implementation (COMPLETED)

**Goal**: Implement USB Host communication with RetroTINK 4K via FTDI serial

**Approach**: Skipped the separate test fixture (3.1) and went directly to implementation. USB OTG mode transition (originally Phase 5) was done simultaneously.

### 3.1: UsbHostSerial Class (COMPLETED)

Created `src/UsbHostSerial.h` and `src/UsbHostSerial.cpp`:
- Subclasses `EspUsbHostSerial_FTDI` from EspUsbHost library
- 512-byte ring buffer for received data
- `begin()` initializes USB Host at 115200 baud
- `update()` calls `task()` to process USB events
- `sendData()` sends data via `submit()` (max 64 bytes per packet)
- `readLine()` scans buffer for CR/LF-terminated lines
- `onNew()` / `onGone()` overrides for device connect/disconnect
- `onReceive()` writes incoming bytes to ring buffer
- Connection/disconnection callbacks for external notification

### 3.2: RetroTink Rewrite (COMPLETED)

Completely rewrote `src/RetroTink.h` and `src/RetroTink.cpp`:
- Takes `UsbHostSerial*` pointer, sends commands via USB Host
- Command framing: `\r<COMMAND>\r` (leading CR clears RT4K input buffer)
- Power state tracking: UNKNOWN, WAKING, BOOTING, ON, SLEEPING
- Parses incoming serial: `Powering Up` → BOOTING, `[MCU] Boot Sequence Complete` → ON, `Power Off` → SLEEPING
- Auto-wake logic:
  - SLEEPING: sends `pwr on`, sets BOOTING, queues command, waits for boot complete (15s timeout)
  - UNKNOWN: sends `pwr on`, sets WAKING, waits 3s. If `Powering Up` received → BOOTING (wait for boot). If no response → ON, sends command.
  - ON: sends command directly
- SVS keep-alive: sends `SVS CURRENT INPUT=N` one second after `SVS NEW INPUT=N`
- Serial data sanitization: non-printable bytes replaced with `?` to prevent JSON parse failures

### 3.3: System Integration (COMPLETED)

- `main.cpp`: Creates UsbHostSerial instance, passes to RetroTink, disables Serial/CDC, calls `tink->update()` in loop
- `WebServer.cpp`: Added `tink.connected` and `tink.powerState` to `/api/status`
- `platformio.ini`: Switched to `ARDUINO_USB_MODE=0`, `ARDUINO_USB_CDC_ON_BOOT=0`, added EspUsbHost lib
- `debug.html`: Enabled tink command dropdown, simplified to logs-only display, fixed polling race conditions
- `version.h`: Bumped to 1.5.0

---

## Phase 4: Testing and Validation

### 4.1: UART Loopback Test (Extron) ✅ COMPLETED

- ✅ Characters sent on TX (GPIO43) received on RX (GPIO44)
- ✅ Loopback verified working

### 4.2: USB Host Integration Test ✅ COMPLETED

- ✅ USB Host detects RetroTINK FTDI device on power-up
- ✅ Manual commands work via web debug console (`remote menu`, `remote back`, `remote profN`)
- ✅ Commands verified on actual RetroTINK 4K display
- ✅ Power state tracking detects boot complete, powering up, and power-off
- ✅ Auto-wake from SLEEPING: sends `pwr on`, waits for boot complete, then sends queued command
- ✅ Auto-wake from UNKNOWN: sends `pwr on`, 3s response window — `Powering Up` → wait for boot, no response → assume ON
- ✅ SVS keep-alive sends follow-up command after 1 second
- ✅ Signal detection auto-switch: parses `Sig` messages, 2s debounce, highest input wins, re-triggers on signal restore
- ✅ Status page shows RT4K connection status, power state, switcher type
- ✅ API documentation (`api.html`) updated with current response format

---

## ✅ Phase 5: USB OTG Mode Transition (COMPLETED)

**Completed as part of Phase 3** - OTG mode switch was done simultaneously with USB Host implementation.

- ✅ `ARDUINO_USB_MODE=0` (OTG mode)
- ✅ `ARDUINO_USB_CDC_ON_BOOT=0` (CDC disabled)
- ✅ `Logger::instance().setSerialEnabled(false)` in main.cpp
- ✅ All debugging via web console (`http://tinklink.local/debug.html`) and `scripts/logs.py`
- ✅ OTA updates working for both firmware and filesystem

---

## Phase 6: Documentation and Cleanup

### 6.1: Update README.md

**Additions**:
- Installation instructions for ESP32-S3
- USB OTG cable requirements
- Troubleshooting section for USB Host issues
- Wiring diagrams for complete setup

### 6.2: Code Comments

**Review and document**:
- USB Host initialization sequence
- FTDI communication patterns
- Error handling logic
- Configuration options

### 6.3: Examples

**Create**:
- `examples/usb_host_test/` - Standalone USB Host test
- `examples/minimal/` - Minimal working configuration
- `examples/full_setup/` - Complete example with all features

### 6.4: GitHub Repository

**Create and push**:
- Create GitHub repository: `smudgeface/tink-link-usb`
- Push all commits
- Write comprehensive README
- Add license (MIT)
- Tag release: v1.0.0

---

## Reference Information

### RetroTINK 4K Serial Protocol

**Connection Settings**:
- Baud Rate: 115200
- Data Format: 8N1 (8 data bits, no parity, 1 stop bit)
- USB Device: FTDI FT232R (VID:0403, PID:6001)

**Command Format**:

1. **Remote Commands** (IR emulation):
   ```
   remote prof1\r\n    # Load Profile 1
   remote prof2\r\n    # Load Profile 2
   remote menu\r\n     # Open menu
   ```

2. **SVS Commands** (Scalable Video Switch):
   ```
   SVS NEW INPUT=1\r\n    # Load profile S1_*.rt4
   SVS NEW INPUT=2\r\n    # Load profile S2_*.rt4
   ```

**References**:
- [RetroTINK-4K Wiki](https://consolemods.org/wiki/AV:RetroTINK-4K#Serial_Over_USB_/_HD-15)

### ESP32-S3 USB Modes

**USB Serial/JTAG Mode** (Default):
- GPIO19/20 used for programming and debugging
- USB CDC for Serial.print() output
- Cannot use USB Host

**USB OTG Mode** (Required for this project):
- GPIO19/20 available for USB Host
- No USB CDC (use UART0 GPIO43/44 for debugging)
- Requires `ARDUINO_USB_MODE=0`

**Switching**:
- Software: platformio.ini build flags
- May require holding BOOT button during upload

**References**:
- [ESP32-S3 USB OTG Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-otg-console.html)
- [Waveshare ESP32-S3-Zero](https://www.espboards.dev/esp32/esp32-s3-zero/)

### EspUsbHost Library

**Repository**: [wakwak-koba/EspUsbHost](https://github.com/wakwak-koba/EspUsbHost)

**Supported Devices**:
- USB Keyboard
- USB Mouse
- USB Serial (FTDI, CP210x)

**Installation**:
```ini
lib_deps = https://github.com/wakwak-koba/EspUsbHost.git
```

**Usage Pattern**:
```cpp
#include <EspUsbHostSerial_FTDI.h>

class MySerialHost : public EspUsbHostSerial_FTDI {
public:
    void onReceive(usb_transfer_t *transfer) override {
        // Handle received data
    }
};

MySerialHost usbHost;

void setup() {
    usbHost.begin();
}

void loop() {
    usbHost.task();  // Process USB events
}
```

**Reference Project**: [ClownCar](https://github.com/svirant/ClownCar)

---

## Known Issues and Considerations

### 1. USB Host Stability
- **Issue**: Some USB devices may have initialization timing issues
- **Mitigation**: Add retry logic, device detection callbacks
- **Testing**: Verify connect/disconnect/reconnect scenarios

### 2. Debugging Without CDC
- **Challenge**: USB OTG mode disables USB CDC serial
- **Solution**: Use UART0 (GPIO43/44) with USB-to-TTL adapter
- **Alternative**: Rely on web interface for debugging

### 3. Power Requirements
- **Requirement**: USB OTG cable must provide sufficient power
- **Recommendation**: 18W+ USB power adapter
- **Consideration**: ESP32-S3-Zero + RetroTINK power draw

### 4. Baud Rate Mismatch
- **Note**: Original tink-link-lite used 9600 baud for RMT UART
- **Correction**: RetroTINK expects 115200 baud (now correctly configured)
- **Impact**: Previous RMT UART implementation would not have worked

### 5. WiFi Interference
- **Consideration**: 2.4GHz WiFi may interfere with USB signals
- **Mitigation**: Use good quality USB cables, test for reliability
- **Fallback**: Can disable WiFi if issues occur (not recommended)

---

## Success Metrics

**Project is complete when**:
- ✅ USB Host reliably communicates with RetroTINK 4K
- ✅ Extron input changes trigger RetroTINK profile switches
- ✅ Web interface provides full monitoring and configuration
- ✅ System runs stably for 24+ hours
- ✅ Documentation is complete and accurate
- ✅ Code is published to GitHub

**Stretch Goals**:
- Support for additional USB serial devices (CP210x, CH340)
- Support for multiple RetroTINK units
- Profile management via web interface
- ✅ Remote firmware updates (OTA) - **COMPLETED**
- Integration with home automation systems

---

## Next Steps

**Remaining Work**:
1. Extended stability testing (USB Host, WiFi, log buffer over 24+ hours)
2. USB disconnect/reconnect robustness validation
3. Documentation cleanup (Phase 6)
4. Consider LED color for RT4K USB connected state (e.g., purple)

**Agent Handoff Notes**:
- Phase 1 ✅ Complete - Base framework
- Phase 2 ✅ Complete - WiFi, LED, Console, OTA
- Phase 3 ✅ Complete - USB Host FTDI communication with RT4K
- Phase 4 ✅ Complete - Testing and validation (all tests passed)
- Phase 5 ✅ Complete - USB OTG mode transition (done with Phase 3)
- Firmware version: 1.6.0
- Device runs in USB OTG mode — no USB CDC serial available
- All debugging via web console (`http://tinklink.local/debug.html`) or `scripts/logs.py`
- OTA updates: `pio run -t ota -e esp32s3` / `pio run -t otafs -e esp32s3`
- USB Host detects FTDI FT232R, commands verified on actual RT4K display
- Power state tracking: UNKNOWN, WAKING, BOOTING, ON, SLEEPING
- Auto-wake from UNKNOWN: sends `pwr on`, 3s response window — `Powering Up` → BOOTING (wait for boot), no response → assume ON
- Auto-wake from SLEEPING: sends `pwr on`, sets BOOTING, waits for boot complete (15s timeout)
- Serial data sanitized (non-printable bytes → '?') to prevent JSON parse failures
- Web debug console uses setTimeout chaining (not setInterval) to prevent duplicate log entries
- Status page shows: switcher type, RT4K USB connection, power state, last command
- API docs (`api.html`) updated with current `tink.connected` and `tink.powerState` fields
- Signal detection auto-switch enabled: parses `Sig` messages, 2s debounce, highest active input wins
- Signal-lost-and-restored: re-triggers callback when signal returns on current input (enables RT4K auto-wake after full power cycle)
