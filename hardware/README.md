# Hardware Design

This document describes the current ESP32-S2 design, which combines USB mux-based dongle
monitoring with direct ATX PSU control through the 24-pin connector.

## Block diagram

```
 ATX 24-pin connector
   5VSB (purple) ─────────────────────────────► ESP32-S2 5V / regulator input
           │
           └──────────────────────────────────► J1 VBUS  (controller powered while BC250 is off)

   PS_ON# (green) ────────────────► Q2 collector
   GND   (black) ─────────────────► ESP32-S2 GND / Q1 emitter / Q2 emitter

 ESP32-S2
   GPIO8  ── R4 (1 kΩ) ───────────► Q2 base         (ATX PSU enable)
   GPIO10 ─────────────────────────► U2 SEL         (0 = ESP32 monitor path, 1 = BC250 path)
   GPIO13 ── R1 (1 kΩ) ───────────► Q1 base         (BC250 power button pulse)
   GPIO9  ◄──────────────────────── divider R2/R3   (BC250 USB VBUS sense)
   USB monitor path ───────────────► HID wake-pattern detection

 J1 (USB Type-A Female, controller input)
   VBUS ───────────────────────────► always-on from 5VSB
   D+ / D- ───────────────────────► U2 common port

 U2 TS3USB221 USB 2:1 switch
   Port A D+ / D- ────────────────► ESP32-S2 monitor-side path
   Port B D+ / D- ────────────────► J2 D+ / D- (BC250 passthrough path)

 J2 (USB Type-A Male or header, to BC250 USB port)
   VBUS ── R2 ── node ── R3 ── GND
                  │
                  └────────────────────────────► ESP32-S2 GPIO9

 J3 (BC250 front-panel power button header)
   pin 1 ─────────────────────────► Q1 collector
   pin 2 ─────────────────────────► GND
```

---

## Component choices

### U1 — ESP32-S2 / LOLIN S2 Mini

The current prototype uses an ESP32-S2 board because:
- It is already available in this build.
- It has enough GPIOs for USB mux control, BC250 power-button drive, BC250 VBUS sense,
  and ATX PSU enable.
- It can run the wake detector logic derived from captured HID report patterns.
- It includes a usable onboard LED for host online/offline indication.

### U2 — TI TS3USB221

A 2:1 USB 2.0 bidirectional mux/switch.

If using the common TS3USB221 breakout module sold on AliExpress and similar sites,
the `S` select control may be exposed as a small pad on the back side of the breakout
rather than on the main header edge. Check both sides of the board before wiring GPIO10.

Important: on the breakout used for this project, the enable pin is effectively active-low.
`OE` must be tied to `GND` or the USB path remains disconnected and the ESP32-S2 host stack
will see `0 devices` regardless of the `S`/`SEL` state.

| Pin  | Connection                                     |
|------|------------------------------------------------|
| VCC  | 3.3 V                                          |
| GND  | GND                                            |
| D+   | Device D+  (J1 pin 3) — common port            |
| D─   | Device D─  (J1 pin 2) — common port            |
| 1D+  | ESP32 monitor-side D+ path                     |
| 1D─  | ESP32 monitor-side D─ path                     |
| 2D+  | BC250 passthrough D+ path                      |
| 2D─  | BC250 passthrough D─ path                      |
| S    | ESP32-S2 GPIO10 (0 = monitor path, 1 = BC250 passthrough path) |
| OE   | GND — required on this breakout to keep the switch enabled |

Confirmed bring-up note:
- `S`/`SEL` chooses which downstream path is connected.
- `OE` must be asserted separately; on the tested breakout this means pulling `OE` low.
- If `OE` is left floating or tied incorrectly, firmware logs show USB host startup but repeated
  `initial scan: 0 devices` / `scan: 0 devices` with no dongle enumeration.

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
- ESP32-S2 GPIO8 drives the transistor gate/base
- The transistor pulls `PS_ON#` into its asserted state
- ATX ground is shared with the ESP32-S2 ground

This lets the ESP32-S2 turn the PSU on before it pulses the BC250 power button.

Example with an NPN transistor such as 2N2222 or BC547:

```
ESP32-S2 GPIO8  ── R4 (1 kΩ) ──► Q2 base
ESP32-S2 GND  ─────────────────► Q2 emitter
Q2 collector  ─────────────────► ATX PS_ON# (green wire)
ATX GND       ─────────────────► ESP32-S2 GND (common ground)
```

Behavior:
- GPIO8 LOW: Q2 off, `PS_ON#` released, PSU off
- GPIO8 HIGH: Q2 on, `PS_ON#` pulled low, PSU on

That matches the current firmware default. If your driver stage inverts the logic, use the
`PSU_ON_ACTIVE_LOW` firmware option instead of changing the hardware notes.

### Voltage divider R2/R3 — VBUS sensing

USB VBUS is 5 V; ESP32-S2 GPIO is 3.3 V only.

```
R2 = 10 kΩ,  R3 = 10 kΩ
V_sense = 5 V × 10 / (10 + 10) = 2.5 V   ✓ safely below 3.3 V, clearly > 1.65 V threshold
```

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
| GPIO9  | IN      | BC250 USB VBUS sense                |
| GPIO10 | OUT     | USB switch SEL (0=ESP32 / 1=BC250) |
| GPIO8  | OUT     | ATX `PS_ON#` driver control         |

An onboard LED can be used for host status if its pin does not conflict with the signals above.

---

## Prototype wiring (breadboard)

Wiring color convention used in this build:
- BC250 power button (J3/Q1 path): red wire
- BC250 USB/power sense (J2 VBUS divider to GPIO9): blue wire

For a quick prototype without a custom PCB:

1. Use an ESP32-S2 board (for example LOLIN S2 Mini).
2. Wire the TS3USB221 on a breakout board or hand-solder it to a SOIC adapter.
3. For the device connector, break out a USB-A Female port module (common on eBay/LCSC).
4. For the BC250 connection, solder a USB-A Male cable end (or use a USB-A to pin-header
   breakout).
5. Tap ATX 24-pin standby power from 5VSB (purple wire) and common GND (black wire).
6. Route ATX `PS_ON#` (green wire) through the Q2 transistor stage.
7. Use a Berg/Dupont 2-pin connector on J3 to clip onto the motherboard PWR_BTN header.

---

## PCB considerations (future)

- Keep USB traces as a matched-length differential pair (100 Ω differential impedance).
- Avoid routing D+/D─ near switching signals.
- Place decoupling caps (100 nF) on VCC of U2 close to the IC.
- Silkscreen labels for J3 polarity (PWR_BTN header is not polarised but label anyway).
- Keep the ATX `PS_ON#` driver physically close to the 24-pin connector entry.
- Consider test pads for `5VSB`, `PS_ON#`, `GPIO9` sense, and `GPIO8` control.
