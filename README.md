# TinkLink-USB

> 🚧 **Status**: Phase 2 Complete - WiFi, LED, Web Console, OTA Updates working. USB Host implementation next.

A USB-based ESP32-S3 bridge between video switchers and the RetroTINK 4K.

📋 **[Implementation Plan](implementation-plan.md)** - Detailed development roadmap and current status

TinkLink-USB automatically triggers RetroTINK 4K profile changes when your video switcher changes inputs. This version uses **USB Host** to communicate directly with the RetroTINK 4K over its USB serial interface, eliminating the need for VGA cable serial connections and level shifters.

## Why USB Instead of UART?

The original [TinkLink-Lite](https://github.com/smudgeface/tink-link-lite) project used the RetroTINK's HD-15 (VGA) serial pins, which required:
- Custom VGA cable adapter to access serial pins
- RS-232 level shifter (MAX3232)
- Careful wiring to avoid interfering with VGA video signal

**TinkLink-USB simplifies this** by connecting directly to the RetroTINK's USB-C port:
- ✅ Single USB OTG cable provides both power and communication
- ✅ No custom adapters or level shifters needed
- ✅ RetroTINK's USB serial uses FTDI FT232R chip (standard USB-to-serial)
- ✅ Native 115200 baud communication (RetroTINK's native speed)

## Features

- **USB Host Communication** - Direct USB serial connection to RetroTINK 4K
- **Extron SW VGA Support** - Monitors Extron SW series VGA switchers via RS-232
- **Web Interface** - Monitor status and configure WiFi from any browser
- **System Console** - Web-based serial console for debugging and sending commands
- **OTA Updates** - Update firmware and filesystem over WiFi (no USB required)
- **Centralized Logging** - Debug logs accessible via web interface and serial
- **mDNS Support** - Access via `http://tinklink.local`
- **Persistent Configuration** - Settings stored in flash via LittleFS

## Hardware Requirements

### Required Components

| Component | Notes |
|-----------|-------|
| **ESP32-S3 Dev Board** | USB Host requires ESP32-S3 (S3-DevKitC, ATOMS3, or StampS3). ESP32-C3 does NOT support USB Host. |
| **USB OTG Cable** | Type-C to Type-A with power support (18W+ recommended) |
| **RetroTINK 4K** | USB serial interface enabled |
| **Extron SW VGA** | Or compatible video switcher with RS-232 output |
| **RS-232 Level Shifter** | MAX3232 or similar (for Extron connection only) |

**Important:** The ESP32-C3 does NOT support USB Host. You must use an ESP32-S3 variant.

### ESP32-S3 Boards

Supported boards (based on [EspUsbHost](https://github.com/wakwak-koba/EspUsbHost) library):

| Board | Notes |
|-------|-------|
| **Waveshare ESP32-S3-Zero** | ✅ **Recommended** - Compact (23.5×18mm), USB-C port, castellated holes. [Pinout](https://www.espboards.dev/esp32/esp32-s3-zero/) • [Wiki](https://www.waveshare.com/wiki/ESP32-S3-Zero) |
| **ESP32-S3-DevKitC-1** | Standard development board, readily available |
| **M5Stack ATOMS3** | Compact form factor with built-in display |
| **M5Stack StampS3** | Ultra-compact module |

#### Waveshare ESP32-S3-Zero Details

The **Waveshare ESP32-S3-Zero** is highly suitable for this project:

**Specifications:**
- **Chip**: ESP32-S3FH4R2 dual-core @ 240MHz
- **Memory**: 4MB Flash, 2MB PSRAM, 512KB SRAM
- **Size**: 23.5 × 18mm (ultra-compact)
- **GPIO**: 24 available pins (GPIO33-37 reserved for PSRAM)
- **USB**: Native USB on GPIO19 (D-) and GPIO20 (D+)
- **LED**: WS2812B RGB LED on GPIO21
- **UART0**: Dedicated TX on GPIO43, RX on GPIO44 (accessible on edge connectors)
- **Power**: 3.7V-6V, minimum 500mA @ 5V
- **WiFi**: 2.4GHz 802.11 b/g/n
- **Bluetooth**: BLE 5.0

**Pin Assignments for TinkLink-USB:**
- **GPIO21**: WS2812 RGB LED (status indicator)
- **GPIO43**: UART0 TX (Extron switcher communication)
- **GPIO44**: UART0 RX (Extron switcher communication)
- **GPIO19/20**: USB OTG (RetroTINK communication - Phase 3)

**USB Host Configuration:**

The ESP32-S3 has two USB controllers sharing GPIO19/GPIO20:
1. **USB Serial/JTAG** - Default mode for programming and debugging
2. **USB OTG** - For USB Host/Device functionality

To use USB Host mode, the platformio.ini must configure USB OTG mode:
```ini
build_flags =
    -DARDUINO_USB_MODE=0    ; 0 = OTG mode, 1 = CDC mode (default)
```

**Trade-offs:**
- ✅ USB OTG mode enables USB Host functionality for RetroTINK communication
- ⚠️ Disables USB CDC serial debugging (use UART0 on GPIO43/44 instead)
- ⚠️ Programming may require holding BOOT button during upload

**References:**
- [Waveshare ESP32-S3-Zero Wiki](https://www.waveshare.com/wiki/ESP32-S3-Zero)
- [ESPBoards.dev - ESP32-S3-Zero](https://www.espboards.dev/esp32/esp32-s3-zero/)
- [Schematic PDF](https://files.waveshare.com/wiki/ESP32-S3-Zero/ESP32-S3-Zero-Sch.pdf)
- [ESP32-S3 USB OTG Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-otg-console.html)

### Pin Assignments

#### Waveshare ESP32-S3-Zero

| Function | GPIO | Notes |
|----------|------|-------|
| USB Host D+ | 20 | Connected to Type-C port (USB OTG mode) |
| USB Host D- | 19 | Connected to Type-C port (USB OTG mode) |
| Debug UART TX | 43 | Hardware UART0 TX (for serial debugging) |
| Debug UART RX | 44 | Hardware UART0 RX |
| Extron TX | 17 | Hardware UART1 TX, 9600 baud |
| Extron RX | 18 | Hardware UART1 RX |
| Status LED | 21 | WS2812B RGB LED (onboard) |

**Important Notes:**
- GPIO19/20 are internally connected to the board's Type-C USB port
- USB OTG mode disables USB CDC debugging - use UART0 (GPIO43/44) with a USB-to-TTL adapter for debugging
- GPIO33-37 are reserved for onboard PSRAM and not available
- The Type-C port serves dual purpose: programming (Serial/JTAG mode) and USB Host (OTG mode)

### Wiring Connections

**RetroTINK 4K (via USB):**
- Connect ESP32-S3 USB OTG port to RetroTINK USB-C port
- Use USB OTG cable with power support
- RetroTINK appears as FTDI FT232R device (`/dev/ttyUSB0` on Linux)
- Serial settings: 115200 baud, 8N1

**Extron SW VGA (via RS-232):**
- Requires RS-232 level shifter (3.3V ↔ RS-232 voltage levels)
- ESP32 TX → Level Shifter → Extron RX
- Extron TX → Level Shifter → ESP32 RX
- Common RS-232 level shifter: MAX3232
- Serial settings: 9600 baud, 8N1

## RetroTINK 4K Serial Protocol

The RetroTINK 4K supports two command types over USB serial:

### Remote Commands

Emulate IR remote button presses:
```
remote prof1\r\n    # Load Profile 1
remote prof2\r\n    # Load Profile 2
remote menu\r\n     # Open menu
```

### SVS Commands

Scalable Video Switch protocol for automated profile loading:
```
SVS NEW INPUT=1\r\n    # Switch to input 1 and load S1_*.rt4 profile
SVS NEW INPUT=2\r\n    # Switch to input 2 and load S2_*.rt4 profile
```

**Serial Settings:**
- Baud Rate: 115200
- Data Format: 8N1 (8 data bits, no parity, 1 stop bit)

**Reference**: [RetroTINK-4K Wiki - Serial Communication](https://consolemods.org/wiki/AV:RetroTINK-4K#Serial_Over_USB_/_HD-15)

## Getting Started

### Prerequisites

1. **PlatformIO** - Install one of:
   - [VS Code](https://code.visualstudio.com/) with [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode) (recommended)
   - [PlatformIO Core CLI](https://platformio.org/install/cli)

2. **ESP32-S3 Development Board** - DevKitC-1, ATOMS3, or StampS3

3. **USB OTG Cable** - For connecting ESP32-S3 to RetroTINK 4K

4. **2.4GHz WiFi network** - For web interface access

### Installation

**Current Status - Phase 2**: WiFi and LED functionality working, USB Host implementation in progress.

#### Step 1: Clone the Repository

```bash
git clone https://github.com/smudgeface/tink-link-usb.git
cd tink-link-usb
```

#### Step 2: Connect Hardware

Connect your **Waveshare ESP32-S3-Zero** to your computer via USB-C cable.

#### Step 3: Build the Firmware

Compile the project to verify everything is set up correctly:

```bash
pio run -e esp32s3
```

This compiles the firmware but does not upload it to the device.

#### Step 4: Upload Firmware

Flash the compiled firmware to the ESP32-S3:

```bash
pio run -e esp32s3 -t upload
```

The device will automatically reset after upload.

#### Step 5: Upload Filesystem

Upload the web interface files (HTML, CSS, config files) to the device's LittleFS filesystem:

```bash
pio run -e esp32s3 -t uploadfs
```

**Important**: You must run this command to access the web interface. Without it, you'll see "Not Found" errors when browsing to the device.

#### Step 6: Monitor Serial Output (Optional)

To view debug output and verify the device is working:

```bash
pio device monitor -e esp32s3
```

Press `Ctrl+C` to exit the monitor.

#### Step 7: Configure WiFi

On first boot, the device starts in Access Point (AP) mode:

1. Look for the **blue blinking LED** on the ESP32-S3-Zero
2. Connect to the WiFi network named **`TinkLink-XXXXXX`** (no password)
3. Your device should auto-assign an IP via DHCP (192.168.1.100-200 range)
4. Open a web browser and navigate to **`http://192.168.1.1`**
5. Use the web interface to scan for networks and connect to your 2.4GHz WiFi

#### Step 8: Access Web Interface

After WiFi is configured:
- The LED will turn **solid green** when connected
- Access the device at **`http://tinklink.local`** (via mDNS)
- Or use the IP address assigned by your router

### Complete Build & Upload Commands

For convenience, here's the complete sequence to build and flash everything:

```bash
# Build, upload firmware, and upload filesystem in one go
pio run -e esp32s3 -t upload && pio run -e esp32s3 -t uploadfs
```

**Note**: USB Host functionality (Phase 3) not yet implemented. See [implementation-plan.md](implementation-plan.md) for roadmap.

### OTA (Over-The-Air) Updates

Once your device is connected to WiFi, you can update firmware and filesystem without USB:

**Via Web Interface:**
1. Navigate to `http://tinklink.local` → Debug page
2. Scroll to "Firmware Update (OTA)" section
3. Select firmware (`.bin`) or filesystem (`.bin`) file
4. Click Upload and wait for completion
5. Device will automatically reboot

**Via Command Line (PlatformIO):**
```bash
# Build and upload firmware via OTA
pio run -t ota -e esp32s3

# Build and upload filesystem via OTA
pio run -t otafs -e esp32s3

# Use a specific IP address instead of mDNS
TINKLINK_HOST=192.168.1.100 pio run -t ota -e esp32s3
```

**Via Python Script (standalone):**
```bash
python scripts/ota_upload.py firmware .pio/build/esp32s3/firmware.bin
python scripts/ota_upload.py fs .pio/build/esp32s3/littlefs.bin
python scripts/ota_upload.py firmware firmware.bin --host 192.168.1.100
```

**Environment Variables:**
- `TINKLINK_HOST` - Device hostname/IP (default: `tinklink.local`)

## Troubleshooting

### Device Won't Boot / Boot Loop

**Symptom**: Device continuously reboots with error: `E (xxx) spi_flash: Detected size(4096k) smaller than the size in the binary image header(8192k)`

**Solution**: This occurs when the firmware is built for 8MB flash but your device has 4MB. The `platformio.ini` file must include:

```ini
board_upload.flash_size = 4MB
board_build.flash_size = 4MB
```

These settings are already configured correctly in this project. If you still see this error:

1. Clean the build cache: `pio run -e esp32s3 -t clean`
2. Erase flash completely: `python3 ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 erase_flash`
3. Rebuild and upload: `pio run -e esp32s3 -t upload`

### Web Interface Shows "Not Found"

**Symptom**: Web server responds but all pages show "Not Found"

**Solution**: The filesystem hasn't been uploaded. Run:

```bash
pio run -e esp32s3 -t uploadfs
```

This uploads the HTML, CSS, and config files to the device's LittleFS filesystem.

### LED Color Reference

- **Blue blinking** (500ms) = Access Point mode active
- **Yellow solid** = Connecting to WiFi
- **Green solid** = Connected to WiFi network
- **Red solid** = Connection failed
- **Off** = Disconnected or initializing

### mDNS Not Working

**Symptom**: Cannot access `http://tinklink.local`

**Solution**:
- **macOS/Linux**: mDNS should work automatically
- **Windows**: Install [Bonjour Print Services](https://support.apple.com/kb/DL999) or use the device's IP address instead
- **Alternative**: Check your router's DHCP client list for the device's IP address

### Serial Monitor Not Working

If `pio device monitor` fails with a termios error, the serial port may be in use. Try:
- Unplug and replug the USB cable
- Use a different serial terminal like `screen` or `minicom`
- Check that no other application is using the serial port

## Project Structure

```
tink-link-usb/
├── .gitignore
├── LICENSE
├── README.md
├── implementation-plan.md
├── platformio.ini
├── scripts/
│   └── ota_upload.py          # OTA upload automation script
├── src/
│   ├── main.cpp
│   ├── logger.h               # Centralized logging system
│   ├── logger.cpp
│   ├── usb_host_ftdi.h
│   ├── usb_host_ftdi.cpp
│   ├── extron_sw_vga.h
│   ├── extron_sw_vga.cpp
│   ├── retrotink.h
│   ├── retrotink.cpp
│   ├── wifi_manager.h
│   ├── wifi_manager.cpp
│   ├── web_server.h
│   ├── web_server.cpp
│   ├── config_manager.h
│   └── config_manager.cpp
└── data/
    ├── index.html             # Status page
    ├── config.html            # WiFi configuration
    ├── debug.html             # System console & OTA updates
    ├── style.css
    └── config.json
```

## Reference Projects

- **[ClownCar](https://github.com/svirant/ClownCar)** - Arduino Nano ESP32 project that uses USB Host to communicate with RetroTINK 4K
- **[EspUsbHost](https://github.com/wakwak-koba/EspUsbHost)** - ESP32-S3 USB Host library supporting FTDI serial devices

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

Based on the [TinkLink-Lite](https://github.com/smudgeface/tink-link-lite) project, which was itself based on the original [tink-link](https://github.com/smudgeface/tink-link) MicroPython project.
