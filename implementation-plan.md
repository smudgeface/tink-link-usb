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
- Extron UART on GPIO17 (TX) / GPIO18 (RX)
- WS2812B RGB LED on GPIO21

**Reference Documentation**: See [README.md](README.md) for:
- Board specifications and pinout
- RetroTINK 4K serial protocol details
- USB Host configuration requirements
- Links to datasheets and reference projects

---

## Current Status

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

## Phase 2: USB Host FTDI Implementation

**Goal**: Implement USB Host communication with RetroTINK 4K via FTDI serial

### 2.1: USB Host Test Fixture

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

### 2.2: USB Host FTDI Class Implementation

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

### 2.3: RetroTINK Integration

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

## Phase 3: Testing and Validation

### 3.1: UART Loopback Test (Extron)

**Goal**: Verify Extron UART hardware before RS-232 integration

**Setup**:
- Jumper GPIO17 to GPIO18 on ESP32-S3-Zero
- Same approach as tink-link-lite testing

**Test Procedure**:
1. Add loopback echo code to `extron_sw_vga.cpp::update()`
2. Build and upload firmware
3. Monitor serial output for echoed characters
4. Verify 9600 baud timing is correct
5. Remove loopback code after verification

**Success Criteria**:
- Characters sent on TX (GPIO17) received on RX (GPIO18)
- Clean UART waveform (if oscilloscope available)

### 3.2: USB Host Integration Test

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

### 3.3: Performance and Stability

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

## Phase 4: USB OTG Mode Transition

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

## Phase 5: Documentation and Cleanup

### 5.1: Update README.md

**Additions**:
- Installation instructions for ESP32-S3
- USB OTG cable requirements
- Troubleshooting section for USB Host issues
- Wiring diagrams for complete setup

### 5.2: Code Comments

**Review and document**:
- USB Host initialization sequence
- FTDI communication patterns
- Error handling logic
- Configuration options

### 5.3: Examples

**Create**:
- `examples/usb_host_test/` - Standalone USB Host test
- `examples/minimal/` - Minimal working configuration
- `examples/full_setup/` - Complete example with all features

### 5.4: GitHub Repository

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
- Remote firmware updates (OTA)
- Integration with home automation systems

---

## Next Steps

**Immediate Actions**:
1. Connect Waveshare ESP32-S3-Zero to computer
2. Upload Phase 1 firmware (CDC mode)
3. Verify basic functionality (WiFi, web interface, Extron UART loopback)
4. Begin Phase 2: USB Host FTDI implementation

**Agent Handoff Notes**:
- All code compiles successfully
- Git history is clean with descriptive commits
- README.md contains all reference links
- Ready to begin USB Host implementation
- User has Waveshare ESP32-S3-Zero hardware available
