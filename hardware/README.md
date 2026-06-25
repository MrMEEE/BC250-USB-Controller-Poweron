# Hardware Design

## Block diagram

```
 5VSB (from ATX connector or always-on USB header)
   │
   ├──────────────────────────────► Pico VSYS  (powers the MCU)
   │
   └──────────────────────────────► J1 VBUS    (powers the USB device always)

 J1 (USB Type-A Female — device input)
   │ D+/D─
   ▼
 U2  TS3USB221  2:1 USB switch
   ├─ Port A (D+/D─) ──────────────► Pico USB D+/D─  (monitoring path)
   └─ Port B (D+/D─) ──────────────► J2 D+/D─        (PC passthrough path)
   SEL ◄──────────────────────────── Pico GP15

 J2 (USB Type-A Male or header pins — to PC USB port)
   VBUS ─── R2 ─── node ─── R3 ─── GND
                    │
                    └──────────────► Pico GP14  (PC VBUS sense)

 Pico GP13 ─── R1(1 kΩ) ─── Q1 Base
                              Q1 Collector ──► J3 pin 1  (MOB PWR_BTN)
                              Q1 Emitter   ──► GND
 J3 pin 2 ──► GND                              (MOB PWR_BTN return)
```

---

## Component choices

### U1 — RP2040 / Raspberry Pi Pico

The Raspberry Pi Pico is chosen because:
- TinyUSB host stack is mature and well-tested on RP2040.
- Dual-core; USB host runs on core 0 while the state machine runs on core 0 via the
  cooperative TinyUSB task loop (no RTOS needed).
- 3.3 V I/O, cheap, breadboard-friendly.
- USB D+/D─ pins are accessible on the micro-USB connector pads.

> **Pico W** can be used instead if Wi-Fi is desired for future features (e.g. remote
> wake, MQTT status).

### U2 — TI TS3USB221

A 2:1 USB 2.0 bidirectional mux/switch.

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

Drives the motherboard PWR_BTN header.  The transistor collector is wired to one header
pin; emitter to GND; the other header pin is GND on the motherboard itself.  When the
Pico drives GP13 HIGH the transistor conducts and momentarily shorts the header — exactly
as pressing the front-panel power button does.

### Voltage divider R2/R3 — VBUS sensing

PC USB VBUS is 5 V; Pico GPIO is 3.3 V tolerant (max 3.63 V).

```
R2 = 10 kΩ,  R3 = 10 kΩ
V_sense = 5 V × 10 / (10 + 10) = 2.5 V   ✓ safely below 3.3 V, clearly > 1.65 V threshold
```

### Power — 5VSB

The ATX standby rail (pin 9 on the 24-pin connector, purple wire) provides 5 V even
when the PC is off.  Route this to:
- Pico VSYS (which feeds the onboard 3.3 V LDO and runs the MCU).
- J1 VBUS (so the plugged-in USB device stays powered and its D+ pullup is visible).

A 500 mA polyfuse between 5VSB and J1 VBUS protects against a shorted cable.

> If tapping the ATX connector is not practical, the PC's always-on USB header (if the
> BIOS enables 5VSB on a rear USB port) can be used instead.

---

## GPIO pin assignments

| GPIO | Direction | Function                            |
|------|-----------|-------------------------------------|
| GP0  | TX        | UART debug output (optional)        |
| GP1  | RX        | UART debug input  (optional)        |
| GP13 | OUT       | Power button drive (to Q1 base)     |
| GP14 | IN        | PC VBUS sense (via R2/R3 divider)   |
| GP15 | OUT       | USB switch SEL (0=Pico / 1=PC)      |

USB D+/D─ are the Pico's dedicated USB PHY pins (not GPIOs).

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
- Consider a status LED on GP16 to show current state (solid=passthrough, blink=monitoring).
