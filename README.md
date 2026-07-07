# USB Wake Switch — BC250 / Steam Controller

A small ESP32-S2-based board that watches for Steam Controller dongle wake traffic, turns on the
ATX PSU feeding a BC250 system, pulses the BC250 power button, and then hands the controller's
USB connection over to the BC250 once the host is actually online. When the BC250 shuts down,
the board turns the PSU back off and returns the USB path to monitoring mode.

## Why not just leave the device plugged in to the BC250?

The BC250 does not wake itself just because the controller is powered. The controller may be on
while the host and even the PSU are still off, so the BC250 never sees the device in time. This
board solves that sequencing problem in hardware by sitting between the controller, the BC250,
and the ATX supply.

---

## How it works

```
         ATX 24-pin
     5VSB ───────────────► ESP32-S2 power
     PS_ON# ◄───────────── transistor ◄── GPIO8
                               
 Steam Controller                                  BC250
 USB Type-A ──► TS3USB221 USB switch ───────────► USB port
            ▲              ▲
            │              └── SEL from GPIO10, OE tied to GND on tested breakout
            │
            └── HID wake-pattern detection (monitor path)

 BC250 USB VBUS sense ───────────────────────────► GPIO9
 BC250 PWR_BTN header ◄── transistor ◄────────── GPIO13
```

### State machine

```
┌─────────────────────────────────────────────────────────────┐
│  MONITORING                                                  │
│  USB switch → ESP32-S2 monitor path. Watch HID wake pattern.│
└─────────────────────┬───────────────────────────────────────┘
                      │ wake signature detected
                      ▼
┌─────────────────────────────────────────────────────────────┐
│  WAITING_FOR_PSU                                              │
│  Assert PS_ON#. Wait 1 s for ATX rails to settle.             │
└─────────────────────┬───────────────────────────────────────┘
                      │ 1 second elapsed
                      ▼
┌─────────────────────────────────────────────────────────────┐
│  POWERING_ON                                                 │
│  Pulse BC250 PWR_BTN. Wait up to 90 s for BC250 USB VBUS.   │
└──────────┬──────────────────────────────┬───────────────────┘
           │ BC250 VBUS high              │ timeout / abort
           ▼                             ▼
┌──────────────────────┐      ┌──────────────────────────────┐
│  PASSTHROUGH         │      │  MONITORING (back to top)    │
│  USB switch → BC250. │      └──────────────────────────────┘
│  PSU held on. Monitor│
│  BC250 VBUS.         │
└──────────┬───────────┘
           │ BC250 VBUS gone > 3 s
           ▼
┌─────────────────────────────────────────────────────────────┐
│  DORMANT                                                     │
│  PSU off. USB switch → ESP32-S2. Wait for quiet dongle      │
│  traffic before allowing the next wake trigger.             │
└─────────────────────┬───────────────────────────────────────┘
                      │ detector re-armed after silence
                      ▼
                  MONITORING
```

The DORMANT state prevents the board from immediately re-waking the BC250 from one continuous
dongle report burst.

---

## Repository layout

```
.
├── README.md
├── build.sh             ← root firmware build wrapper (PlatformIO + ESP32-S2)
├── firmware-esp32s2/
│   ├── README.md          ← ESP32-S2 build/wiring notes
│   ├── build.sh           ← ESP32-S2 local build/upload helper
│   ├── platformio.ini
│   └── src/
│       └── main.cpp       ← ESP32-S2 state machine
├── hardware/
│   ├── README.md          ← schematic description & design notes
│   └── BOM.md             ← bill of materials
```

### Firmware target

- ESP32-S2 firmware (LOLIN S2 Mini): [firmware-esp32s2/README.md](firmware-esp32s2/README.md)

---

## Quick start

1. Wire the hardware per `hardware/README.md`.
    Important: on the TS3USB221 breakout used here, `OE` must be tied to `GND` or the ESP32-S2
    will never see the dongle on its host bus.
2. Build and flash the ESP32-S2 firmware:
    - From repo root: `./build.sh`
    - Or follow [firmware-esp32s2/README.md](firmware-esp32s2/README.md)
3. Connect the ESP32-S2 to ATX `5VSB` so the board stays powered while the BC250 is off.
4. Wire the ESP32-S2 PSU driver output to ATX `PS_ON#` through a transistor stage.
5. Plug the Steam Controller into the board's Type-A input jack.

## Dongle Pattern Discovery

If your controller dongle is always powered, wake detection should be based on HID report
patterns instead of VBUS. Use:

- [tools/capture_hid_reports.py](tools/capture_hid_reports.py)
- [tools/analyze_hid_pattern.py](tools/analyze_hid_pattern.py)

to capture idle vs wake traffic on a local PC and extract a wake signature to port into ESP32
firmware logic.
