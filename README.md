# OsmAnd Moto Keypad (XIAO nRF52840)

A handlebar-mounted **Bluetooth LE HID keyboard** for controlling
[OsmAnd](https://osmand.net/) navigation on Android while riding. Built around
a Seeed Studio **XIAO nRF52840**, it exposes a small set of physical buttons as
HID keystrokes (zoom, pan, etc.), with key mappings that are **remappable over
BLE from a companion Android app** and firmware that updates **over-the-air
(OTA DFU)** — no cable, no removing the device from the bike.

> Status: active hobby project. Firmware and app evolve together.

---

## Features

- **BLE HID keyboard** — pairs with Android as a standard keyboard; OsmAnd reacts
  to the configured keys.
- **3 physical buttons**, each with **short / double / long** press, all sending
  configurable HID keys.
- **Remote key remapping** — assign keys from the Android app over a BLE
  characteristic; mappings persist in on-chip flash.
- **App-configurable settings** — e.g. `restartOnDisconnect` (auto re-advertise
  after disconnect) is toggled from the app and persisted to flash.
- **OTA firmware updates** via the Adafruit nRF52 bootloader (Nordic DFU).
- **Soft-reset gesture** — hold both outer buttons for 3 s to cleanly reboot the
  MCU, with an LED countdown.
- **Status LED** on an external pin with distinct states (advertising / connected
  / reset-arming / boot).
- **Compile-time TEST / PROD modes** — strip serial logging and debug LEDs from
  production builds with a single flag.
- **Optional 5-way analog joystick** (voltage-divider on an ADC pin), behind a
  compile flag so an unconnected, floating pin can't generate phantom presses.

---

## Hardware

| Part | Notes |
|------|-------|
| MCU | Seeed Studio XIAO nRF52840 |
| Buttons | 3 momentary, active-LOW, internal pull-ups (pins D0/D1/D2) |
| Status LED | External LED on D3 and/or D4 (active-HIGH in firmware) |
| Joystick (optional) | 5-way, via voltage divider into an analog pin |
| Power | (project-specific — battery / bike supply) |

> The on-board RGB LED is not visible inside the enclosure on the target build,
> so the firmware drives an external LED. A `TEST_MODE` build also mirrors status
> onto the built-in LED for bench testing.

---

## Repository structure

This is a **monorepo** holding firmware, the Android app, and the physical
design. The guiding rule: **keep editable source separate from generated
outputs**, and keep large binaries out of normal git history (use Git LFS +
GitHub Releases).

```
osmand-keypad/
├── README.md
├── LICENSE
├── .gitignore
├── .gitattributes                 # Git LFS rules for binary assets
│
├── firmware/
│   ├── osmand_keypad/
│   │   └── osmand_keypad.ino       # main sketch
│   ├── libraries/                  # vendored custom libs (or use submodules)
│   │   ├── MyBfButton/
│   │   └── MyBfButtonManager/
│   └── README.md                   # build & flash instructions
│
├── android/                        # companion app (Android Studio project)
│   └── ...
│
├── hardware/
│   ├── pcb/
│   │   ├── osmand-keypad.kicad_pro # EDITABLE source (KiCad / EasyEDA)
│   │   ├── osmand-keypad.kicad_sch
│   │   ├── osmand-keypad.kicad_pcb
│   │   ├── libraries/              # project symbols/footprints
│   │   └── production/             # GENERATED — regenerate from source
│   │       ├── gerbers/
│   │       ├── drill/
│   │       ├── bom/
│   │       └── assembly/           # pick-and-place / CPL
│   │
│   └── enclosure/
│       ├── cad/                    # EDITABLE source: .step / .f3d / .FCStd / .scad / .3mf
│       ├── stl/                    # print-ready exports (GENERATED)
│       └── print-profiles/         # slicer profiles / Bambu Studio .3mf projects
│
├── docs/
│   ├── images/                     # photos, renders, wiring diagrams
│   ├── ble-protocol.md
│   └── wiring.md
│
└── tools/                          # helper scripts (DFU packaging, etc.)
```

### Why this layout

- **Source vs. generated.** PCB design files (KiCad source) and CAD source
  (STEP/native) are the truth; gerbers and STLs are exports. Committing both is
  fine, but put exports in clearly named `production/` and `stl/` folders so a
  reviewer knows what is hand-edited and what is regenerated.
- **Git LFS for binaries.** STL, STEP, `.f3d`, `.3mf`, gerber zips, and images
  bloat history fast. Track them with LFS (see `.gitattributes` below).
- **GitHub Releases for deliverables.** Don't commit built firmware `.uf2` /
  `.zip` or fabrication `.zip`s into the tree — attach them to a tagged Release.
  That keeps the repo lean and gives users versioned downloads.
- **Native CAD over STL when you can.** STL is a one-way mesh; keep the
  parametric source (STEP / Fusion `.f3d` / FreeCAD / OpenSCAD) so the part
  stays editable, and export STLs alongside it.

### Suggested `.gitattributes` (Git LFS)

```gitattributes
*.stl   filter=lfs diff=lfs merge=lfs -text
*.step  filter=lfs diff=lfs merge=lfs -text
*.stp   filter=lfs diff=lfs merge=lfs -text
*.f3d   filter=lfs diff=lfs merge=lfs -text
*.3mf   filter=lfs diff=lfs merge=lfs -text
*.FCStd filter=lfs diff=lfs merge=lfs -text
*.png   filter=lfs diff=lfs merge=lfs -text
*.jpg   filter=lfs diff=lfs merge=lfs -text
*.zip   filter=lfs diff=lfs merge=lfs -text
```

---

## Firmware

### Toolchain

- **Arduino IDE** (or arduino-cli) with **Seeed XIAO nRF52840** board support
  (the Seeed nRF52 Boards package, based on the Adafruit nRF52 / Bluefruit core).
- The two custom libraries in `firmware/libraries/` (`MyBfButton`,
  `MyBfButtonManager`) — modified from
  [ButtonFever](https://github.com/mickey9801/ButtonFever) by Mickey Chan.

### Build / flash

1. Select board **Seeed XIAO nRF52840**.
2. Set the compile flags at the top of `osmand_keypad.ino` (see below).
3. First flash via USB. Subsequent updates can go **OTA** (see *OTA updates*).

### Compile-time flags

| Flag | Default | Effect |
|------|---------|--------|
| `TEST_MODE` | `1` | `1` = dev: serial console on, status mirrored to the built-in LED, BLE name `"Test Osmand Keyboard"`. `0` = production: no serial, no built-in LED, BLE name `"Osmand Keyboard"`. |
| `USE_ANALOG` | `0` | `1` = read the 5-way joystick. `0` = analog code is compiled out entirely (safe when the joystick is not wired and the pin floats). |

Derived from `TEST_MODE`: `ENABLE_SERIAL` and `MIRROR_BUILTIN_LED`. All serial
output routes through `DBG_*` macros that expand to nothing in a production
build.

### Status LED states

| State | Pattern |
|-------|---------|
| Boot | three quick flashes (also confirms a reset completed) |
| Advertising / disconnected | slow ~500 ms blink |
| Connected | solid on |
| Reset arming | blink that accelerates over 3 s, then a rapid strobe before reboot |

### Soft-reset gesture

Hold **both outer buttons** (Dbtn1 + Dbtn3) for **3 s** → clean `NVIC_SystemReset()`.
While the combo is held, normal key sending is suppressed so the gesture doesn't
emit stray keystrokes.

---

## BLE protocol

Custom configuration service (alongside the standard HID, Device Information,
and DFU services):

- **Service UUID:** `12345678-1234-5678-1234-56789abcdef0`

### Key-mapping characteristic — `...def1` (Write, fixed 9 bytes)

Each button gets 3 bytes: `{ single, double, long }`. Values are **USB HID usage
IDs** (the `HID_KEY_*` constants); `0x00` = no action.

> **Important ordering gotcha.** The firmware indexes the array as
> `customKeys[getID() * 3]`, and on this library `getID()` returns the **pin
> number**, not the construction order. The real byte → button mapping is:

| Bytes | Button | Pin |
|-------|--------|-----|
| 0–2 | Dbtn3 | 0 |
| 3–5 | Dbtn2 | 1 |
| 6–8 | Dbtn1 | 2 |

The Android app must send bytes in this order (`data[0]` → Dbtn3).

### Settings characteristic — `...def2` (Write, fixed `SETTINGS_LEN` bytes)

| Index | Setting | Values |
|-------|---------|--------|
| 0 | `restartOnDisconnect` | `0x00` = false (default), `0x01` = true |

Writes are persisted to flash and applied live (no reboot needed). The length is
fixed, so when settings grow, bump `SETTINGS_LEN` and update both sides together.

---

## OTA updates

Firmware updates over BLE use the Adafruit nRF52 bootloader (Nordic legacy DFU).
The recommended Android path is the official
[Nordic Android DFU Library](https://github.com/NordicSemiconductor/Android-DFU-Library).

**Known issue — bonded GATT cache.** Because the device is an HID keyboard it is
bonded, and Android caches its services. The Adafruit bootloader keeps the same
MAC address in DFU mode, so the first OTA attempt can fail with
`GATT INVALID_HANDLE` and succeed on retry. Practical mitigations (all app-side):

- `setNumberOfRetries(2..3)` on the `DfuServiceInitiator` (automates the retry
  that already works).
- Keep the same address for legacy DFU (do **not** force-scan for `address+1`).
- Force a GATT cache refresh before connecting if needed.

See `docs/ble-protocol.md` and the project notes for details.

---

## Android app

The companion app (in `android/`) handles:
- Pairing and connecting as the HID host.
- Reading/writing the key-mapping and settings characteristics.
- Triggering and running OTA DFU.

---

## Credits & license

- Firmware uses **Adafruit Bluefruit nRF52** (Adafruit / Nordic).
- `MyBfButton` / `MyBfButtonManager` are derived from **ButtonFever** by
  Mickey Chan, MIT-licensed.

This project is released under the [MIT License](LICENSE) unless noted otherwise
in a subfolder. Third-party components keep their own licenses.

---

*Built by BuuCzech Development.*
