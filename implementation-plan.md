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

### ✅ Phase 2: WiFi, LED, Console, and OTA (COMPLETED)

**Status**: All Phase 2 functionality working and tested

**Completed Work**:
- [x] WS2812 RGB LED status indication (FastLED library)
- [x] LED colors: Blue blinking (AP), Yellow (connecting), Green (connected), Red (failed)
- [x] WiFi AP mode with automatic fallback
- [x] WiFi STA mode with credential persistence
- [x] mDNS working (http://tinklink.local)
- [x] All web pages functional (status, config, debug)
- [x] Centralized logging system (Logger class)
- [x] Web-based System Console with target selector (Extron/RetroTINK)
- [x] UART TX/RX to Extron via GPIO43/44
- [x] OTA firmware updates via web interface
- [x] OTA filesystem updates via web interface
- [x] PlatformIO OTA automation (`pio run -t ota`, `pio run -t otafs`)
- [x] Standalone OTA upload script (`scripts/ota_upload.py`)

**Key Files Added/Modified**:
- `src/logger.h`, `src/logger.cpp` - Centralized logging with circular buffer
- `src/web_server.cpp` - OTA upload handlers, logs API endpoint
- `data/debug.html` - System Console UI, OTA upload UI
- `scripts/ota_upload.py` - OTA automation for PlatformIO
- `platformio.ini` - Added `extra_scripts` for OTA targets

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
- `src/retrotink.{h,cpp}` - Stub implementation (to be completed in Phase 2)
- `src/config_manager.{h,cpp}` - Configuration management
- `src/wifi_manager.{h,cpp}` - WiFi connectivity
- `src/web_server.{h,cpp}` - Web interface
- `src/extron_sw_vga.{h,cpp}` - Extron RS-232 handler
- `data/` - Web interface files (HTML/CSS)

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

## Phase 3: USB Host FTDI Implementation

**Goal**: Implement USB Host communication with RetroTINK 4K via FTDI serial

### 3.1: USB Host Test Fixture

**Objective**: Verify USB Host FTDI communication in isolation before integration

**Tasks**:
1. Create `examples/usb_host_test/` directory
2. Write minimal test sketch:
   ```cpp
   // Test sketch structure:
   - Initialize USB Host
   - Detect FTDI device connection
   - Send test string
   - Read response (if any)
   - Report status via Serial (CDC)
   ```
3. Switch to USB OTG mode for testing:
   ```ini
   ARDUINO_USB_MODE=0           # OTG mode
   ARDUINO_USB_CDC_ON_BOOT=0    # Disable CDC (use UART0 for debugging)
   ```
4. Test with actual RetroTINK 4K connected via USB OTG cable

**Expected Behavior**:
- USB Host detects FTDI FT232R device (VID:0403, PID:6001)
- Opens serial connection at 115200 baud, 8N1
- Successfully sends/receives data

**Reference Code**:
- [ClownCar project](https://github.com/svirant/ClownCar) - Arduino Nano ESP32 USB Host example
- [EspUsbHost library](https://github.com/wakwak-koba/EspUsbHost) - USB Host for ESP32-S3

**Debugging Strategy**:
- Use UART0 (GPIO43/44) with USB-to-TTL adapter for serial debugging
- Web interface still available for status monitoring (if WiFi connected)

### 3.2: USB Host FTDI Class Implementation

**Objective**: Create reusable USB Host FTDI serial class

**Files to Create**:
- `src/usb_host_ftdi.h` - USB Host FTDI interface
- `src/usb_host_ftdi.cpp` - Implementation based on EspUsbHost

**Class Interface**:
```cpp
class UsbHostFtdi {
public:
    UsbHostFtdi();
    ~UsbHostFtdi();

    bool begin();  // Initialize USB Host
    void update(); // Process USB events (call in loop)

    // Serial-like interface
    void write(const char* str);
    void writeLine(const char* str);  // Adds \r\n
    int available();
    int read();

    // Status
    bool isConnected() const;
    String getDeviceInfo() const;

    // Callbacks
    void onConnect(std::function<void()> callback);
    void onDisconnect(std::function<void()> callback);

private:
    // EspUsbHost implementation details
    // ...
};
```

**Key Requirements**:
- Based on EspUsbHost library patterns
- Support 115200 baud, 8N1 (RetroTINK default)
- Handle device connect/disconnect events
- Buffer management for reads/writes
- Thread-safe if needed

**Testing**:
- Unit test with RetroTINK 4K
- Verify command transmission
- Check for proper initialization and cleanup

### 3.3: RetroTINK Integration

**Objective**: Replace stub implementation with USB Host serial

**Modifications to `src/retrotink.cpp`**:
```cpp
// Replace:
Serial.printf("RetroTINK TX (stub): [%s]\n", command.c_str());

// With:
if (_usbSerial && _usbSerial->isConnected()) {
    _usbSerial->writeLine(command.c_str());
    Serial.printf("RetroTINK TX (USB): [%s]\n", command.c_str());
} else {
    Serial.printf("RetroTINK TX (not connected): [%s]\n", command.c_str());
}
```

**Constructor Update**:
```cpp
RetroTink::RetroTink(UsbHostFtdi* usbSerial)
    : _usbSerial(usbSerial)
    , _lastCommand("")
{
}
```

**Connection Status**:
- Add `isConnected()` method
- Update web API to report USB connection status
- LED indication for USB connected/disconnected

---

## Phase 4: Testing and Validation

### 4.1: UART Loopback Test (Extron) ✅ COMPLETED

**Goal**: Verify Extron UART hardware before RS-232 integration

**Setup**:
- Jumper GPIO43 to GPIO44 on ESP32-S3-Zero (UART0)

**Test Procedure**:
1. Install jumper between GPIO43 (TX) and GPIO44 (RX)
2. Use web console UART test endpoints to send/receive
3. Verify 9600 baud timing is correct

**Success Criteria**:
- ✅ Characters sent on TX (GPIO43) received on RX (GPIO44)
- ✅ Loopback verified working

### 4.2: USB Host Integration Test

**Goal**: Verify complete system with RetroTINK 4K

**Test Setup**:
- ESP32-S3-Zero connected to RetroTINK 4K via USB OTG cable
- Extron switcher connected via RS-232 (with level shifter)
- WiFi configured for web interface access

**Test Cases**:

1. **USB Connection Test**:
   - Power on system
   - Verify USB Host detects RetroTINK
   - Check web interface shows "USB: Connected"
   - Verify LED indicates connection status

2. **Manual Command Test**:
   - Use debug web interface
   - Send `remote prof1` command
   - Verify RetroTINK loads Profile 1
   - Send `SVS NEW INPUT=1` command
   - Verify behavior

3. **Trigger Test**:
   - Configure triggers in config.json
   - Simulate Extron input change via web interface
   - Verify RetroTINK switches profiles automatically
   - Check web status shows correct state

4. **End-to-End Test**:
   - Connect actual Extron switcher
   - Change physical input on switcher
   - Verify Extron message received
   - Verify trigger mapping executed
   - Verify RetroTINK profile changes
   - Confirm all status updates in web interface

5. **Error Handling Test**:
   - Disconnect RetroTINK USB during operation
   - Verify graceful handling
   - Reconnect RetroTINK
   - Verify auto-reconnection

**Success Criteria**:
- ✅ USB Host stable and reliable
- ✅ Commands transmitted correctly at 115200 baud
- ✅ Trigger mappings execute as configured
- ✅ Web interface accurately reflects system state
- ✅ No crashes or memory leaks during extended operation

### 4.3: Performance and Stability

**Long-term Test**:
- Run system continuously for 24+ hours
- Monitor memory usage
- Check for WiFi stability
- Verify USB Host doesn't disconnect

**Stress Test**:
- Rapid input changes on Extron
- Verify all trigger commands sent
- Check for buffer overflows or command drops

---

## Phase 5: USB OTG Mode Transition

**Goal**: Switch from CDC debugging to USB OTG mode for production

### Configuration Changes

**platformio.ini**:
```ini
# Production configuration
ARDUINO_USB_MODE=0           # OTG mode (USB Host)
ARDUINO_USB_CDC_ON_BOOT=0    # Disable CDC
```

**Debugging Strategy**:
- Use web interface for status monitoring
- Use UART0 (GPIO43/44) + USB-to-TTL adapter if needed
- Add comprehensive logging to web debug page

**Validation**:
- Rebuild with OTG configuration
- Upload firmware (may require BOOT button)
- Verify USB Host functionality intact
- Confirm web interface still accessible

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

**Immediate Actions** (Phase 3: USB Host):
1. Create USB Host test fixture in `examples/usb_host_test/`
2. Switch to USB OTG mode configuration
3. Test basic USB Host functionality with RetroTINK 4K
4. Implement `UsbHostFtdi` class
5. Integrate with `RetroTink` class
6. Test end-to-end: Extron input change → RetroTINK profile switch

**Agent Handoff Notes**:
- Phase 1 ✅ Complete - Base framework
- Phase 2 ✅ Complete - WiFi, LED, Console, OTA
- All code compiles successfully
- OTA updates working (web UI and command line)
- User has Waveshare ESP32-S3-Zero hardware available
- UART communication with Extron verified on GPIO43/44
- Ready to begin Phase 3: USB Host FTDI implementation
- Key challenge: USB OTG mode will disable USB CDC debugging
  - Solution: Use web console and OTA for debugging/updates
