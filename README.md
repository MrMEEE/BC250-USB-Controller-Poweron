# USB Wake Switch — BC250 / Steam Controller

A small microcontroller-based board that monitors a USB port for device insertion, wakes a
desktop PC by momentarily shorting the motherboard power button header, then transparently
routes the USB device to the PC for the remainder of the session. When the PC shuts down the
board returns to monitoring mode.

## Why not just leave the device plugged in to the PC?

The BC250 (and similar mini-PCs) do not support USB wake-from-S5.  The controller is
therefore invisible to the PC until it boots — and by then Steam may have already opened
without it.  This board solves the chicken-and-egg problem in hardware.

---

## How it works

```
                  ┌─────────────────────────────────────┐
                  │            RP2040 (Pico)             │
 Steam Controller │                                      │
  USB Type-A ─────┤─► USB Switch (TS3USB221)             │
                  │       ├─ SEL=0 ──► Pico USB host     │
                  │       └─ SEL=1 ──► PC USB port ───►──┤─► PC
                  │                                      │
                  │  GP13: PWR_BTN ──► transistor ──► MOB│
                  │  GP14: PC_VBUS_SENSE ◄─── divider ◄──┤─◄ PC USB VBUS
                  │  GP15: USB_SWITCH_SEL ──► TS3USB221  │
                  └─────────────────────────────────────┘
```

### State machine

```
┌─────────────────────────────────────────────────────────────┐
│  MONITORING                                                  │
│  USB switch → Pico.  TinyUSB host enumerates the device.    │
└─────────────────────┬───────────────────────────────────────┘
                      │ device plugged in
                      ▼
┌─────────────────────────────────────────────────────────────┐
│  POWERING_ON                                                 │
│  Pulse PWR_BTN for 150 ms.  Wait up to 90 s for PC VBUS.    │
└──────────┬──────────────────────────────┬───────────────────┘
           │ PC VBUS high                 │ timeout
           ▼                             ▼
┌──────────────────────┐      ┌──────────────────────────────┐
│  PASSTHROUGH         │      │  MONITORING (back to top)    │
│  USB switch → PC.    │      └──────────────────────────────┘
│  PC enumerates natively.
│  Monitor PC VBUS.    │
└──────────┬───────────┘
           │ PC VBUS gone > 3 s
           ▼
┌─────────────────────────────────────────────────────────────┐
│  DORMANT                                                     │
│  USB switch → Pico.  Wait for device to be unplugged so     │
│  that a future plug-in event is intentional.                 │
└─────────────────────┬───────────────────────────────────────┘
                      │ device unplugged
                      ▼
                  MONITORING
```

The DORMANT state prevents the board from immediately re-waking the PC after a normal
shutdown just because the Steam Controller is still sitting in the USB port.

---

## Repository layout

```
.
├── README.md
├── build.sh             ← root firmware build wrapper (CMake + Pico SDK)
├── build-esp32s2.sh     ← root firmware build wrapper (PlatformIO + ESP32-S2)
├── firmware-esp32s2/
│   ├── README.md          ← ESP32-S2 build/wiring notes
│   ├── build.sh           ← ESP32-S2 local build/upload helper
│   ├── platformio.ini
│   └── src/
│       └── main.cpp       ← ESP32-S2 state machine (VBUS-sense device detect)
├── hardware/
│   ├── README.md          ← schematic description & design notes
│   └── BOM.md             ← bill of materials
└── firmware/
    ├── README.md          ← build & flash instructions
    ├── CMakeLists.txt
    ├── platformio.ini     ← optional/experimental
    └── src/
        ├── main.c         ← state machine
        ├── usb_host.c/h   ← TinyUSB host callbacks
        ├── power_ctrl.c/h ← power button & VBUS sensing
        └── tusb_config.h  ← TinyUSB compile-time config
```

### Firmware targets

- RP2040/Pico firmware: [firmware/README.md](firmware/README.md)
- ESP32-S2 firmware (LOLIN S2 Mini): [firmware-esp32s2/README.md](firmware-esp32s2/README.md)

---

## Quick start

1. Wire the hardware per `hardware/README.md`.
2. Choose firmware target:
    - RP2040/Pico: run `./build.sh` (or follow [firmware/README.md](firmware/README.md))
    - ESP32-S2: run `./build-esp32s2.sh` (or follow [firmware-esp32s2/README.md](firmware-esp32s2/README.md))
3. Connect the Pico's VSYS to the PC's ATX 5VSB rail so the board stays powered when
   the PC is off.
4. Plug the Steam Controller into the board's Type-A input jack.
