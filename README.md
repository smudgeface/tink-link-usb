# TinkLink-USB

> 🚧 **Status**: In development

A USB-based ESP32-S3 bridge between video switchers and the RetroTINK 4K.

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
| **ESP32-S3-DevKitC-1** | Standard development board, readily available |
| **M5Stack ATOMS3** | Compact form factor with built-in display |
| **M5Stack StampS3** | Ultra-compact module |

### Pin Assignments

| Function | GPIO | Notes |
|----------|------|-------|
| USB Host D+ | 20 | Built-in USB OTG (ESP32-S3) |
| USB Host D- | 19 | Built-in USB OTG (ESP32-S3) |
| Extron TX | TBD | Hardware UART, 9600 baud |
| Extron RX | TBD | Hardware UART |

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

_Installation instructions coming soon_

## Project Structure

```
tink-link-usb/
├── .gitignore
├── LICENSE
├── README.md
├── platformio.ini
├── src/
│   ├── main.cpp
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
    ├── index.html
    ├── config.html
    ├── debug.html
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
