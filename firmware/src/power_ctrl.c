#include "power_ctrl.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>

// GP13: drives NPN transistor base → shorts motherboard PWR_BTN header
#define PIN_PWR_BTN         13

// GP14: reads the divided-down VBUS from the PC's USB port
//   Voltage divider: R2=10kΩ top, R3=10kΩ bottom → 2.5 V at 5 V in.
//   RP2040 GPIO threshold is ~1.65 V so this reads cleanly as HIGH/LOW.
#define PIN_PC_VBUS_SENSE   14

void power_ctrl_init(void)
{
    gpio_init(PIN_PWR_BTN);
    gpio_set_dir(PIN_PWR_BTN, GPIO_OUT);
    gpio_put(PIN_PWR_BTN, 0);   // ensure button is released on startup

    gpio_init(PIN_PC_VBUS_SENSE);
    gpio_set_dir(PIN_PC_VBUS_SENSE, GPIO_IN);
    gpio_disable_pulls(PIN_PC_VBUS_SENSE);  // external divider biases the pin
}

void power_ctrl_pulse_button(uint32_t ms)
{
    printf("[pwr] pressing power button for %lu ms\n", (unsigned long)ms);
    gpio_put(PIN_PWR_BTN, 1);
    sleep_ms(ms);
    gpio_put(PIN_PWR_BTN, 0);
    printf("[pwr] power button released\n");
}

bool power_ctrl_is_pc_on(void)
{
    return gpio_get(PIN_PC_VBUS_SENSE);
}
