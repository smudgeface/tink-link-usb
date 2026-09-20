# RetroTINK 4K serial commands

How TinkLink talks to the RetroTINK 4K. You don't need any of this to use TinkLink; it is here for the curious and for anyone debugging with the System page's console.

Reference: [RetroTINK-4K wiki — Serial over USB / HD-15](https://consolemods.org/wiki/AV:RetroTINK-4K#Serial_Over_USB_/_HD-15)

## Serial settings

| Port | Baud rate | Format |
|------|-----------|--------|
| USB | 2,000,000 (RetroTINK firmware 1.75+; earlier firmware: 115,200) | 8N1 |
| HD-15 | 115,200 | 8N1 |

## Loading profiles

TinkLink can load a profile in two ways. You pick one per trigger on the Config page.

**Remote** — acts like a button press on the IR remote:

```
remote prof1    # load profile 1
remote prof2    # load profile 2
remote menu     # open the menu
```

**SVS** (Scalable Video Switch) — tells the RetroTINK which switcher input is active; it loads the matching `S<input>_*.rt4` profile from the SD card:

```
SVS NEW INPUT=1    # input 1 -> S1_*.rt4
SVS NEW INPUT=2    # input 2 -> S2_*.rt4
```

Firmware 1.75+ acknowledges remote commands (`[COM] Serial Remote: prof1`) and rejects unknown ones (`[COM] Bad Command: <text>`). SVS commands get no reply. Status text such as `[MCU] Powering Up` is no longer sent over serial by default.

## Power state

With power management set to *full* ([configuration](configuration.md)), TinkLink works out whether the RetroTINK is awake from how it answers:

| Sent | RetroTINK is | Reply |
|------|--------------|-------|
| `pwr on` | asleep | `[COM] Power On Requested`, then silence until it has booted (about 5 s) |
| `pwr on` | on | `[COM] Bad Command: pwr on` |
| anything else | asleep | nothing |
| `ver` | on | `[COM] ... FW Version: x.y.z` |

So `pwr on` both wakes the RetroTINK and reveals whether it was asleep. The first answered `ver` afterwards marks the moment it will accept a profile command. When the RetroTINK powers down, its serial output drops low, which the USB serial chip reports as a line break.

To try a trigger without touching the switcher or AVR: `curl -X POST -d input=1 http://tinklink.local/api/tink/trigger`.
