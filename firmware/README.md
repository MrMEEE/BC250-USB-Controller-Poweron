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

```bash
git clone https://github.com/raspberrypi/pico-sdk --recurse-submodules ~/pico-sdk
export PICO_SDK_PATH=~/pico-sdk
```

Add the export to `~/.bashrc` or `~/.profile` to make it permanent.

---

## Build

```bash
cd firmware
mkdir build && cd build
cmake ..
make -j$(nproc)
```

This produces `build/usb_wake_switch.uf2`.

---

## Flash

1. Hold the **BOOTSEL** button on the Pico while plugging it in to USB (or press BOOTSEL
   while pressing the onboard reset button if you added SW1).
2. A mass-storage device named **RPI-RP2** will appear.
3. Copy the `.uf2` file to it:

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
