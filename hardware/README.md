# Hardware Design

This document originally described the RP2040/Pico prototype. The current ESP32-S2 design keeps
the same USB mux and power-button ideas, but also adds direct ATX PSU control through the 24-pin
connector so the board can power the BC250 system up and down.

## Block diagram

```
 5VSB (from ATX 24-pin connector)
   │
   ├──────────────────────────────► ESP32-S2 5V / regulator input (powers the MCU)
   │
   └──────────────────────────────► J1 VBUS    (powers the USB device always)

 ATX 24-pin
   PS_ON# (green) ── driver transistor/MOSFET ──► ESP32-S2 GPIO16
   GND    (black) ──────────────────────────────► ESP32-S2 GND

 J1 (USB Type-A Female — device input)
   │ D+/D─
   ▼
 U2  TS3USB221  2:1 USB switch
   ├─ Port A (D+/D─) ──────────────► ESP32-S2 / monitor-side path
   └─ Port B (D+/D─) ──────────────► J2 D+/D─        (PC passthrough path)
   SEL ◄──────────────────────────── ESP32-S2 GPIO15

 J2 (USB Type-A Male or header pins — to BC250 USB port)
   VBUS ─── R2 ─── node ─── R3 ─── GND
                    │
                    └──────────────► ESP32-S2 GPIO14  (BC250 VBUS sense)

 J1 VBUS ─── R4 ─── node ─── R5 ─── GND
                    │
                    └──────────────► ESP32-S2 GPIO12  (controller VBUS sense)

 ESP32-S2 GPIO13 ─── R1(1 kΩ) ─── Q1 Base
                                 Q1 Collector ──► J3 pin 1  (BC250 PWR_BTN)
                                 Q1 Emitter   ──► GND
 J3 pin 2 ──► GND                              (BC250 PWR_BTN return)
```

---

## Component choices

### U1 — ESP32-S2 / LOLIN S2 Mini

The current prototype uses an ESP32-S2 board because:
- It is already available in this build.
- It has enough GPIOs for USB mux control, BC250 power-button drive, controller VBUS sense,
  BC250 VBUS sense, and ATX PSU enable.
- USB host enumeration is not required for the current design because controller power-on is
  detected through VBUS presence.
- It includes a usable onboard LED for host online/offline indication.

### U2 — TI TS3USB221

A 2:1 USB 2.0 bidirectional mux/switch.

If using the common TS3USB221 breakout module sold on AliExpress and similar sites,
the `S` select control may be exposed as a small pad on the back side of the breakout
rather than on the main header edge. Check both sides of the board before wiring GP15.

| Pin  | Connection                                     |
|------|------------------------------------------------|
| VCC  | 3.3 V                                          |
| GND  | GND                                            |
| D+   | Device D+  (J1 pin 3) — common port            |
| D─   | Device D─  (J1 pin 2) — common port            |
| 1D+  | Pico USB D+ (monitoring path)                  |
| 1D─  | Pico USB D─ (monitoring path)                  |
| 2D+  | PC D+       (passthrough path)                 |
| 2D─  | PC D─       (passthrough path)                 |
| S    | Pico GP15  (0 = port 1 / Pico, 1 = port 2 / PC) |
| OE   | VCC — active-HIGH enable; tie HIGH to always enable |

Alternatives: **FSUSB42MX** (ON Semi), **PI3USB9281** (Diodes Inc.).  
Package: VSON (DRC) 10-pin 3×3 mm, or UQFN (RSE) 10-pin 2×1.5 mm.

### Q1 — NPN transistor (BC547 / 2N2222)

Drives the BC250 motherboard PWR_BTN header.  The transistor collector is wired to one header
pin; emitter to GND; the other header pin is GND on the motherboard itself.  When the
ESP32-S2 drives GPIO13 HIGH the transistor conducts and momentarily shorts the header — exactly
as pressing the front-panel power button does.

### Q2 — ATX `PS_ON#` driver

Add a second transistor or small MOSFET stage between the ESP32-S2 and the ATX `PS_ON#` wire.
The board must not drive the ATX control line directly. The safe pattern is:
- ESP32-S2 GPIO16 drives the transistor gate/base
- The transistor pulls `PS_ON#` into its asserted state
- ATX ground is shared with the ESP32-S2 ground

This lets the ESP32-S2 turn the PSU on before it pulses the BC250 power button.

Example with an NPN transistor such as 2N2222 or BC547:

```
ESP32-S2 GPIO16 ── R4 (1 kΩ) ──► Q2 base
ESP32-S2 GND  ─────────────────► Q2 emitter
Q2 collector  ─────────────────► ATX PS_ON# (green wire)
ATX GND       ─────────────────► ESP32-S2 GND (common ground)
```

Behavior:
- GPIO16 LOW: Q2 off, `PS_ON#` released, PSU off
- GPIO16 HIGH: Q2 on, `PS_ON#` pulled low, PSU on

That matches the current firmware default. If your driver stage inverts the logic, use the
`PSU_ON_ACTIVE_LOW` firmware option instead of changing the hardware notes.

### Voltage divider R2/R3 and R4/R5 — VBUS sensing

USB VBUS is 5 V; ESP32-S2 GPIO is 3.3 V only.

```
R2 = 10 kΩ,  R3 = 10 kΩ
V_sense = 5 V × 10 / (10 + 10) = 2.5 V   ✓ safely below 3.3 V, clearly > 1.65 V threshold
```

Use the same divider values for controller-side VBUS sensing on GPIO12.

### Power — 5VSB and ATX `PS_ON#`

The ATX standby rail (pin 9 on the 24-pin connector, purple wire) provides 5 V even
when the PC is off.  Route this to:
- The ESP32-S2 board power input / regulator path.
- J1 VBUS (so the plugged-in USB device stays powered and its D+ pullup is visible).

The ATX `PS_ON#` line (pin 16, green wire) is the PSU control signal. Drive it only through
the Q2 transistor stage described above.

A 500 mA polyfuse between 5VSB and J1 VBUS protects against a shorted cable.

> If tapping the ATX connector is not practical, the PC's always-on USB header (if the
> BIOS enables 5VSB on a rear USB port) can be used instead.

---

## GPIO pin assignments

| GPIO | Direction | Function                            |
|------|-----------|-------------------------------------|
| GPIO13 | OUT     | BC250 power button drive            |
| GPIO14 | IN      | BC250 USB VBUS sense                |
| GPIO15 | OUT     | USB switch SEL (0=ESP32 / 1=BC250) |
| GPIO12 | IN      | Controller-side VBUS sense          |
| GPIO16 | OUT     | ATX `PS_ON#` driver control         |

An onboard LED can be used for host status if its pin does not conflict with the signals above.

---

## Prototype wiring (breadboard)

For a quick prototype without a custom PCB:

1. Use a Pico or Pico W.
2. Wire the TS3USB221 on a breakout board or hand-solder it to a SOIC adapter.
3. For the device connector, break out a USB-A Female port module (common on eBay/LCSC).
4. For the PC connection, solder a USB-A Male cable end (or use a USB-A to pin-header
   breakout).
5. Tap 5VSB from any Molex 4-pin connector on the ATX PSU (yellow = 12 V, red = 5 V).
   **Use the red (5 V) wire — not yellow.**
6. Use a Berg/Dupont 2-pin connector on J3 to clip onto the motherboard PWR_BTN header.

---

## PCB considerations (future)

- Keep USB traces as a matched-length differential pair (100 Ω differential impedance).
- Avoid routing D+/D─ near switching signals.
- Place decoupling caps (100 nF) on VCC of U2 close to the IC.
- Silkscreen labels for J3 polarity (PWR_BTN header is not polarised but label anyway).
- Keep the ATX `PS_ON#` driver physically close to the 24-pin connector entry.
- Consider test pads for `5VSB`, `PS_ON#`, `GPIO14` sense, and `GPIO16` control.
