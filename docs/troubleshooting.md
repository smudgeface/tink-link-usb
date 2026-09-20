# Troubleshooting

## Status LED

| LED | Meaning |
|-----|---------|
| Blue, blinking | Access point mode: join the `TinkLink-XXXXXX` network to set up WiFi |
| Yellow | Connecting to WiFi |
| Green | Connected |
| Red | Connection failed (it retries, then falls back to access point mode) |
| Off | Starting up or disconnected |

## Can't reach `http://tinklink.local`

- **macOS, iOS, Linux**: `.local` names normally just work.
- **Windows**: install [Bonjour Print Services](https://support.apple.com/kb/DL999), or use the IP address.
- **Find the IP address**: look for `tinklink` in your router's list of connected devices.
- **Another device already uses the name**: TinkLink then answers to `tinklink-2.local` (and so on). The Status page shows the name in use.
- **Weak WiFi**: the RT4K Profiler and Remote apps give up on requests after a few seconds. If they keep disconnecting, give TinkLink a better signal and connect by IP address.

## Every page says "Not Found"

The web pages haven't been uploaded yet. Upload the filesystem:

```bash
pio run -e esp32s3 -t uploadfs                 # over USB
pio run -t buildfs -t otafs -e esp32s3         # over WiFi
```

## USB upload doesn't start

TinkLink uses the board's USB port to talk to the RetroTINK, so the board can't be flashed over USB while the firmware runs. Put it in bootloader mode first:

1. Hold **BOOT**
2. Press and release **RESET** (or unplug and replug USB)
3. Release **BOOT**
4. Run the upload within a few seconds

Once WiFi is set up, [update over WiFi](building.md#updating-over-wifi-ota) instead.

## Boot loop after flashing

Log shows `spi_flash: Detected size(4096k) smaller than the size in the binary image header(8192k)`: the firmware was built for an 8 MB board but yours has 4 MB. This project's `platformio.ini` already sets 4 MB. If you still see the error:

1. Clean the build: `pio run -e esp32s3 -t clean`
2. Erase the flash: `python3 ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port <your port> erase_flash`
3. Upload firmware and filesystem again

## No serial monitor output

Expected: the USB port is busy talking to the RetroTINK, so there is no USB debug output. Use the System page's console or [`scripts/logs.py`](building.md#reading-logs) instead.
