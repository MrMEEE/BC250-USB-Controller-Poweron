# Firmware

Written in C using the **Raspberry Pi Pico SDK** and **TinyUSB** (bundled with the SDK).

## Build requirements

| Tool | Min version |
|------|-------------|
| CMake | 3.13 |
| arm-none-eabi-gcc | 10.x |
| Pico SDK | 1.5 / 2.x |
| Ninja or Make | any |

### Install on Debian/Ubuntu

```bash
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
```

### Get the Pico SDK

Option 1: Clone manually (recommended):

```bash
git clone https://github.com/raspberrypi/pico-sdk --recurse-submodules ~/pico-sdk
```

Option 2: Let the build script download it automatically:

```bash
cd firmware
./build.sh --fetch-sdk
```

This will create `firmware/pico-sdk/` and use it automatically. If Git is available, it will clone with submodules. Otherwise, it will download a ZIP and manually fetch TinyUSB.

The project defaults to `~/pico-sdk`, so no extra configuration is needed if you keep
the SDK there.

If your SDK is elsewhere, set `PICO_SDK_PATH` (or pass `-DPICO_SDK_PATH=...` to CMake):

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
```

Add the export to `~/.bashrc` or `~/.profile` to make it permanent.

---

## Build

Primary build flow is CMake + Pico SDK.

Build:

```bash
cd firmware
./build.sh
```

Build + upload (RP2040 in BOOTSEL mode):

```bash
cd firmware
./build.sh --upload
```

If needed, pass the mounted RP2040 path explicitly:

```bash
./build.sh --upload --uf2-target /media/$USER/RPI-RP2
```

Manual CMake build:

```bash
cd firmware
mkdir -p build && cd build
cmake ..
cmake --build . --parallel "$(nproc)"
```

This produces `build/usb_wake_switch.uf2`.

`platformio.ini` is kept for experiments, but PlatformIO currently does not support
this project's Pico SDK framework configuration for board `pico` on this machine.

---

## Flash

1. **Put the Pico in BOOTSEL mode:**
   - Hold the **BOOTSEL** button while plugging in USB, OR
   - Hold BOOTSEL while pressing the onboard reset button

2. A mass-storage device named **RPI-RP2** will appear. Verify with:
   ```bash
   mount | grep RPI
   ```

3. Flash using the build script:

   **Automatic (waits for mount if not yet present):**
   ```bash
   ./build.sh --upload --wait-for-pico
   ```

   **If mount is already present:**
   ```bash
   ./build.sh --upload
   ```

   **Manual copy (if using explicit mount path):**
   ```bash
   cp build/usb_wake_switch.uf2 /media/$USER/RPI-RP2/
   ```

The Pico will reboot automatically and start running the firmware.

---

## Debug output

The firmware prints state-machine transitions over UART0 (GP0 TX / GP1 RX) at 115200 baud.
Connect a USB-UART adapter to those pins and use `minicom`, `screen`, or `tio`:

```bash
tio /dev/ttyUSB0 -b 115200
```

---

## Tuning parameters (main.c)

| Constant              | Default | Description                                  |
|-----------------------|---------|----------------------------------------------|
| `PWR_BTN_PULSE_MS`    | 150 ms  | How long to hold the power button pin HIGH   |
| `PC_BOOT_TIMEOUT_MS`  | 90 000  | Max wait for PC VBUS after pressing button   |
| `PC_OFF_CONFIRM_MS`   | 3 000   | VBUS must be absent this long to count as off|
| `PIN_USB_SWITCH_SEL`  | GP15    | USB switch select GPIO                       |
| `PIN_PC_VBUS_SENSE`   | GP14    | PC VBUS sense GPIO                           |
| `PIN_PWR_BTN`         | GP13    | Power button drive GPIO                      |
