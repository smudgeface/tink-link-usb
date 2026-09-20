# Building, flashing and updating

## Prerequisites

- [PlatformIO](https://platformio.org/install): the [VS Code extension](https://platformio.org/install/ide?install=vscode) or the [command line tools](https://platformio.org/install/cli)
- Python 3 with `requests` (`pip install requests`) for the WiFi update and log scripts

```bash
git clone https://github.com/smudgeface/tink-link-usb.git
cd tink-link-usb
```

## First flash over USB

Connect the board to your computer with a USB-C cable, then:

```bash
pio run -e esp32s3 -t upload      # firmware
pio run -e esp32s3 -t uploadfs    # web pages and default settings
```

Both steps are needed: without the second one every page shows "Not Found".

A board that already runs TinkLink must be put in bootloader mode first: hold **BOOT**, press and release **RESET**, release **BOOT**, then upload within a few seconds. A factory-fresh board doesn't need this.

To ship WiFi credentials with the filesystem instead of setting them up in the browser, create `data/wifi.json` before `uploadfs` (see [Configuration](configuration.md)).

For ESP32-C3 and other boards, use the environment described in [Alternative boards](alternative-boards.md).

## Updating over WiFi (OTA)

Once TinkLink is on your network you never need the USB cable again.

**From the command line:**

```bash
pio run -t ota -e esp32s3                  # firmware
pio run -t buildfs -t otafs -e esp32s3     # web pages (your settings are backed up and restored)
```

The scripts look for `tinklink.local`. To use another address:

```bash
TINKLINK_HOST=192.168.1.100 pio run -t ota -e esp32s3
```

**From the browser:** open the System page, choose a firmware or filesystem `.bin` file (from `.pio/build/esp32s3/`) and click Upload. The device reboots when done. Note that a filesystem upload from the browser resets your settings; download a backup on the same page first.

**Standalone script:**

```bash
python scripts/ota_upload.py firmware .pio/build/esp32s3/firmware.bin
python scripts/ota_upload.py fs .pio/build/esp32s3/littlefs.bin --host 192.168.1.100
```

## Reading logs

There is no USB debug output (see [Hardware](hardware.md#the-usb-port-does-double-duty)). Read the log on the System page, or from a terminal:

```bash
python scripts/logs.py              # follow the log (Ctrl+C to stop)
python scripts/logs.py -n 50        # last 50 entries
python scripts/logs.py --clear      # clear the log
python scripts/logs.py --host 192.168.1.100
```

## Project layout

```
tink-link-usb/
├── src/                 # Firmware
│   ├── main.cpp             # Start-up and main loop
│   ├── RetroTink.*          # RetroTINK 4K control and power tracking
│   ├── RetroBridge.*        # App interface for the RT4K Profiler / Remote (/api/v1)
│   ├── Switcher.h, SwitcherFactory.*, ExtronSwVgaSwitcher.*   # Video switchers
│   ├── DenonAvr.*           # Denon/Marantz receiver control and discovery
│   ├── UsbHostSerial.*, UartSerial.*, TelnetSerial.*          # Serial links (USB, UART, network)
│   ├── WifiManager.*        # WiFi, access point fallback, .local name
│   ├── WebServer.*          # Web pages and REST API
│   ├── ConfigManager.*      # Settings storage
│   └── Logger.*             # Log buffer
├── data/                # Web pages and default settings (ESP32-S3)
├── data_c3/             # Same, for ESP32-C3
├── lib/                 # Patched copies of AsyncTCP and ESPAsyncWebServer (see TINKLINK_PATCH.md in each)
├── scripts/             # WiFi update and log tools
├── docs/                # Documentation
└── assets/              # Photos, pinouts, reference PDFs
```

The REST API is documented on the device itself: `http://tinklink.local/api.html`.
