# ESP32-S2 Firmware

This firmware targets ESP32-S2 boards such as LOLIN S2 Mini.

It performs the full wake/power-switching flow:
- Detect controller wake activity
- Enable the ATX PSU through the 24-pin connector
- Wait 1 second for PSU rails to settle
- Pulse the BC250 motherboard power button
- Switch USB mux to BC250 after boot
- Turn PSU off and return USB to monitor mode after BC250 shuts down

## Wake trigger strategy

For a wireless controller + always-powered USB dongle, VBUS does not indicate controller wake.
The recommended approach is:
- Capture dongle HID reports on a local PC
- Identify a stable "controller turned on / input activity" signature
- Port that signature rule into ESP32 wake logic

The current firmware uses the raw ESP-IDF USB host library to watch the dongle on the monitor
path and applies the same silence-to-burst HID prefix rule validated on the PC.
`DEV_VBUS_SENSE_PIN` remains as a fallback if USB host initialization fails.

## Default pin mapping

Configured in [platformio.ini](platformio.ini):
- `USB_SWITCH_SEL_PIN = GPIO10`
- `PWR_BTN_PIN = GPIO13`
- `PC_VBUS_SENSE_PIN = GPIO9`
- `DEV_VBUS_SENSE_PIN = GPIO12`
- `PSU_ON_PIN = GPIO8`
- `WAKE_DETECT_FROM_HID = 1` (raw USB host detector enabled)

All VBUS sense pins must use resistor dividers to stay within 3.3V GPIO limits.

## Power sequence

The target ESP32-S2 firmware sequence is:
1. Steam Controller powers up
2. ESP32-S2 detects HID wake signature on the monitor path
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

Current implementation note: step 2 uses the raw USB host detector in `main.cpp`, with VBUS
fallback only if host initialization does not come up.

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

## Optional logging

Serial logging can be compiled out entirely with:

```ini
-D ENABLE_LOGGING=0
```

Leave it unset or set it to `1` for the current verbose bring-up logs.

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
./build.sh
./build.sh --upload --wait-for-port
```

## Notes

- The ESP32-S2 target now uses the ESP-IDF USB host library directly for HID wake detection.

## Local pattern capture workflow

Use the helper scripts in [tools](../tools):

1. Install dependency:

```bash
python3 -m pip install --user hidapi
```

2. List HID devices and find the dongle VID/PID:

```bash
python3 tools/capture_hid_reports.py --list
```

3. Capture idle traffic (controller untouched):

```bash
python3 tools/capture_hid_reports.py --vid 0xVID --pid 0xPID --output tools/captures/idle.csv --duration 30
```

4. Capture wake traffic (press controller power / wake action):

```bash
python3 tools/capture_hid_reports.py --vid 0xVID --pid 0xPID --output tools/captures/wake.csv --duration 30
```

5. Analyze for candidate wake signatures:

```bash
python3 tools/analyze_hid_pattern.py --idle tools/captures/idle.csv --wake tools/captures/wake.csv
```

The analyzer reports wake-only packets and most-active byte positions to help define a compact
match rule for ESP32 firmware.

6. Optional: run live wake detector on PC:

python3 tools/detect_wakeup_live.py --list
python3 tools/detect_wakeup_live.py --vid 0xVID --pid 0xPID --iface 1

The detector triggers when it sees a silence-to-burst transition in matching HID report prefixes,
which is useful for validating the wake rule before embedding it in ESP32 firmware.
