#ifndef POWER_CTRL_H_
#define POWER_CTRL_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * Initialise power-button and VBUS-sense GPIOs.
 * Must be called once before any other function in this module.
 */
void power_ctrl_init(void);

/**
 * Drive the power-button line HIGH for @p ms milliseconds, then release.
 * Blocks for the duration of the pulse.
 */
void power_ctrl_pulse_button(uint32_t ms);

/**
 * Return true if the PC's USB VBUS rail is detected as present (PC is on / resuming).
 */
bool power_ctrl_is_pc_on(void);

#endif /* POWER_CTRL_H_ */
