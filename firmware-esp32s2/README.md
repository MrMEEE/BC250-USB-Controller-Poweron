# ESP32-S2 Firmware

This firmware targets ESP32-S2 boards such as LOLIN S2 Mini.

It keeps the same high-level behavior as the RP2040 firmware:
- Detect controller insertion
- Enable the ATX PSU through the 24-pin connector
- Wait 1 second for PSU rails to settle
- Pulse the BC250 motherboard power button
- Switch USB mux to BC250 after boot
- Turn PSU off and return USB to monitor mode after BC250 shuts down

## Important difference from RP2040 version

This implementation detects insertion using a GPIO VBUS-sense signal (`DEV_VBUS_SENSE_PIN`) rather than USB host enumeration.

You must wire a 5V-to-3.3V divider from the controller-side VBUS line into `DEV_VBUS_SENSE_PIN`.

## Default pin mapping

Configured in [platformio.ini](platformio.ini):
- `USB_SWITCH_SEL_PIN = GPIO15`
- `PWR_BTN_PIN = GPIO13`
- `PC_VBUS_SENSE_PIN = GPIO14`
- `DEV_VBUS_SENSE_PIN = GPIO12`
- `PSU_ON_PIN = GPIO16`

All VBUS sense pins must use resistor dividers to stay within 3.3V GPIO limits.

## Power sequence

The ESP32-S2 firmware now uses this sequence:
1. Steam Controller powers up
2. ESP32-S2 detects controller-side VBUS
3. Assert PSU enable
4. Wait 1 second for PSU rails to settle
5. Pulse BC250 power button
6. Wait for BC250 USB VBUS to appear
7. Switch USB mux to BC250

When BC250 shuts down:
1. Detect BC250 VBUS loss
2. Turn PSU off
3. Switch USB mux back to ESP32-S2 monitoring path
4. Stay dormant until the controller is unplugged

## ATX PSU control

The PSU enable output is intended to drive the ATX `PS_ON#` signal on the 24-pin connector.
Do not connect `PSU_ON_PIN` directly to the ATX wire. Use a transistor or MOSFET stage so the
board only sinks or gates the control signal safely.

Typical ATX signals involved:
- `5VSB` (purple wire): powers the ESP32-S2 even while BC250 is off
- `PS_ON#` (green wire): pulled active to turn the PSU on
- `GND` (black wire): common reference

## Status LED

The firmware now supports host power status indication on the board LED:
- Host system online: solid ON
- Host system shutdown/offline: 1 Hz blink

By default it uses `LED_BUILTIN` when available. If the LED pin conflicts with one of
the control/sense pins, LED status is disabled automatically and reported on serial.

Optional overrides can be set in [platformio.ini](platformio.ini):
- `-D STATUS_LED_PIN=...`
- `-D STATUS_LED_ACTIVE_LOW=1`

## Build and flash

Install PlatformIO, then:

```bash
cd firmware-esp32s2
./build.sh
./build.sh --upload --wait-for-port
./build.sh --upload --port /dev/ttyACM1 --monitor
```

Or from repository root:

```bash
./build-esp32s2.sh
./build-esp32s2.sh --upload --wait-for-port
```

## Notes

- Keep the RP2040 firmware in [firmware](../firmware) if you prefer USB-host-based device detection.
- If you want true USB host enumeration on ESP32-S2, this can be added later with an ESP-IDF USB host client implementation.
