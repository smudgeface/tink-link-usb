# History

## Where TinkLink-USB comes from

TinkLink-USB is based on [TinkLink-Lite](https://github.com/smudgeface/tink-link-lite), which was itself based on the original [tink-link](https://github.com/smudgeface/tink-link) MicroPython project.

## Why USB instead of the HD-15 port?

The original [TinkLink](https://github.com/Patrick-Working/tink-link) talked to the RetroTINK 4K through the serial pins of its HD-15 (VGA) connector. That needed:

- a custom VGA cable adapter to reach the serial pins,
- an RS-232 level shifter (MAX3232),
- careful wiring so the serial lines didn't disturb the VGA video signal.

TinkLink-USB plugs into the RetroTINK's USB-C port instead:

- one USB OTG cable carries both power and communication,
- no custom adapter or level shifter on the RetroTINK side,
- the RetroTINK shows up as a standard USB serial device (FTDI FT232R),
- the link runs at 2 Mbaud, the default since RetroTINK 4K firmware 1.75 (older firmware: 115,200, [configurable](configuration.md)).

Boards without USB Host can still use the HD-15 serial pins; see [Alternative boards](alternative-boards.md).

## Release history

Every release, with the reasoning behind the bigger changes, is in the [changelog](changelog.md).

## Projects that helped

- [ClownCar](https://github.com/svirant/ClownCar) — Arduino Nano ESP32 project that talks to the RetroTINK 4K over USB Host
- [EspUsbHost](https://github.com/wakwak-koba/EspUsbHost) — ESP32-S3 USB Host library with FTDI serial support
- [Retro-Bridge](https://github.com/solidpipe/retro-bridge) — the WiFi adapter whose app interface TinkLink is compatible with, so the [RT4K Profiler](https://rt4k-profiler.pipe.hr) and Remote apps work
- [HEOS CLI Protocol Specification](../assets/docs/HEOS_CLI_Protocol_Specification.pdf) and [Denon AVR network ports](https://manuals.denon.com/EUsecurity/EU/EN/index.php) — Denon/Marantz network control
