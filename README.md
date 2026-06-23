# OsmAnd Moto Keypad (XIAO nRF52840)

A handlebar-mounted **Bluetooth LE HID keyboard** for controlling
[OsmAnd](https://osmand.net/) navigation on Android while riding. Built around
a Seeed Studio **XIAO nRF52840**, it exposes a small set of physical buttons as
HID keystrokes (zoom, pan, etc.), with key mappings that are **remappable over
BLE from a companion Android app** and firmware that updates **over-the-air
(OTA DFU)** — no cable, no removing the device from the bike.

> Status: active hobby project. Firmware and app evolve together.

\---

## Features

* **BLE HID keyboard** — pairs with Android as a standard keyboard; OsmAnd reacts
to the configured keys.
* **3 physical buttons**, each with **short / double / long** press, all sending
configurable HID keys.
* **Remote key remapping** — assign keys from the Android app over a BLE
characteristic; mappings persist in on-chip flash.
* **App-configurable settings** — e.g. `restartOnDisconnect` (auto re-advertise
after disconnect) is toggled from the app and persisted to flash.
* **OTA firmware updates** via the Adafruit nRF52 bootloader (Nordic DFU).
* **Soft-reset gesture** — hold both outer buttons for 3 s to cleanly reboot the
MCU, with an LED countdown.
* **Status LED** on an external pin with distinct states (advertising / connected
/ reset-arming / boot).
* **Compile-time TEST / PROD modes** — strip serial logging and debug LEDs from
production builds with a single flag.
* **Optional 5-way analog joystick** (voltage-divider on an ADC pin), behind a
compile flag so an unconnected, floating pin can't generate phantom presses.

\---

## Hardware

|Part|Notes|
|-|-|
|MCU|Seeed Studio XIAO nRF52840|
|Buttons|3 momentary, active-LOW, internal pull-ups (pins D0/D1/D2)|
|Status LED|External LED on D3 and/or D4 (active-HIGH in firmware)|
|Joystick (optional)|5-way, via voltage divider into an analog pin|
|Power|USB-C or 12V from the bike|

> The on-board RGB LED is not visible inside the enclosure on the target build,
> so the firmware drives an external LED. A `TEST\_MODE` build also mirrors status
> onto the built-in LED for bench testing.

\---

## Firmware

### Toolchain

* **Arduino IDE** (or arduino-cli) with **Seeed XIAO nRF52840** board support
(the Seeed nRF52 Boards package, based on the Adafruit nRF52 / Bluefruit core).
* The two custom libraries in `firmware/libraries/` (`MyBfButton`,
`MyBfButtonManager`) — modified from
[ButtonFever](https://github.com/mickey9801/ButtonFever) by Mickey Chan.

### Build / flash

1. Select board **Seeed XIAO nRF52840**.
2. Set the compile flags at the top of `osmand\_keypad.ino` (see below).
3. First flash via USB. Subsequent updates can go **OTA** (see *OTA updates*).

### Compile-time flags

|Flag|Default|Effect|
|-|-|-|
|`TEST\_MODE`|`1`|`1` = dev: serial console on, status mirrored to the built-in LED, BLE name `"Test Osmand Keyboard"`. `0` = production: no serial, no built-in LED, BLE name `"Osmand Keyboard"`.|
|`USE\_ANALOG`|`0`|`1` = read the 5-way joystick. `0` = analog code is compiled out entirely (safe when the joystick is not wired and the pin floats).|

Derived from `TEST\_MODE`: `ENABLE\_SERIAL` and `MIRROR\_BUILTIN\_LED`. All serial
output routes through `DBG\_\*` macros that expand to nothing in a production
build.

### Status LED states

|State|Pattern|
|-|-|
|Boot|three quick flashes (also confirms a reset completed)|
|Advertising / disconnected|slow \~500 ms blink|
|Connected|solid on|
|Reset arming|blink that accelerates over 3 s, then a rapid strobe before reboot|

### Soft-reset gesture

Hold **both outer buttons** (Dbtn1 + Dbtn3) for **3 s** → clean `NVIC\_SystemReset()`.
While the combo is held, normal key sending is suppressed so the gesture doesn't
emit stray keystrokes.

\---

## BLE protocol

Custom configuration service (alongside the standard HID, Device Information,
and DFU services):

* **Service UUID:** `12345678-1234-5678-1234-56789abcdef0`

### Key-mapping characteristic — `...def1` (Write, fixed 9 bytes)

Each button gets 3 bytes: `{ single, double, long }`. Values are **USB HID usage
IDs** (the `HID\_KEY\_\*` constants); `0x00` = no action.

> \*\*Important ordering gotcha.\*\* The firmware indexes the array as
> `customKeys\[getID() \* 3]`, and on this library `getID()` returns the \*\*pin
> number\*\*, not the construction order. The real byte → button mapping is:

|Bytes|Button|Pin|
|-|-|-|
|0–2|Dbtn3|0|
|3–5|Dbtn2|1|
|6–8|Dbtn1|2|

The Android app must send bytes in this order (`data\[0]` → Dbtn3).

### Settings characteristic — `...def2` (Write, fixed `SETTINGS\_LEN` bytes)

|Index|Setting|Values|
|-|-|-|
|0|`restartOnDisconnect`|`0x00` = false (default), `0x01` = true|

Writes are persisted to flash and applied live (no reboot needed). The length is
fixed, so when settings grow, bump `SETTINGS\_LEN` and update both sides together.

\---

## OTA updates

Firmware updates over BLE use the Adafruit nRF52 bootloader (Nordic legacy DFU).
The recommended Android path is the official
[Nordic Android DFU Library](https://github.com/NordicSemiconductor/Android-DFU-Library).

**Known issue — bonded GATT cache.** Because the device is an HID keyboard it is
bonded, and Android caches its services. The Adafruit bootloader keeps the same
MAC address in DFU mode, so the first OTA attempt can fail with
`GATT INVALID\_HANDLE` and succeed on retry. Practical mitigations (all app-side):

* `setNumberOfRetries(2..3)` on the `DfuServiceInitiator` (automates the retry
that already works).
* Keep the same address for legacy DFU (do **not** force-scan for `address+1`).
* Force a GATT cache refresh before connecting if needed.

See `docs/ble-protocol.md` and the project notes for details.

\---

## Android app

The companion app (in `android/`) handles:

* Pairing and connecting as the HID host.
* Reading/writing the key-mapping and settings characteristics.
* Triggering and running OTA DFU.

\---

## Credits \& license

* Firmware uses **Adafruit Bluefruit nRF52** (Adafruit / Nordic).
* `MyBfButton` / `MyBfButtonManager` are derived from **ButtonFever** by
Mickey Chan, MIT-licensed.

This project is released under the [MIT License](LICENSE) unless noted otherwise
in a subfolder. Third-party components keep their own licenses.

\---

*Built by BuuCzech Development.*

