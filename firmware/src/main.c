#include <stdio.h>
#include "pico/stdlib.h"
#include "bsp/board.h"
#include "tusb.h"
#include "usb_host.h"
#include "power_ctrl.h"

// ── GPIO ─────────────────────────────────────────────────────────────────────
// Controls the TS3USB221 SEL pin:  0 = route D+/D─ to Pico (monitoring)
//                                  1 = route D+/D─ to PC   (passthrough)
#define PIN_USB_SWITCH_SEL  15

// ── Timing constants (milliseconds) ─────────────────────────────────────────
#define PWR_BTN_PULSE_MS    150U    // duration of simulated power-button press
#define PC_BOOT_TIMEOUT_MS  90000U  // give up waiting for the PC after 90 s
#define PC_OFF_CONFIRM_MS   3000U   // VBUS must be absent this long → PC is off

// ── State machine ─────────────────────────────────────────────────────────────
typedef enum {
    STATE_MONITORING,   // USB switch → Pico; watching for device insertion
    STATE_POWERING_ON,  // device found; power button pulsed; waiting for PC VBUS
    STATE_PASSTHROUGH,  // USB switch → PC; device connected directly to PC
    STATE_DORMANT,      // PC just shut down; waiting for device unplug before resuming
} system_state_t;

static system_state_t s_state = STATE_MONITORING;

// ── Helpers ───────────────────────────────────────────────────────────────────

static const char *state_name(system_state_t s)
{
    switch (s) {
        case STATE_MONITORING:  return "MONITORING";
        case STATE_POWERING_ON: return "POWERING_ON";
        case STATE_PASSTHROUGH: return "PASSTHROUGH";
        case STATE_DORMANT:     return "DORMANT";
        default:                return "UNKNOWN";
    }
}

static void transition(system_state_t next)
{
    printf("[fsm] %s → %s\n", state_name(s_state), state_name(next));
    s_state = next;
}

static void switch_to_pico(void)
{
    gpio_put(PIN_USB_SWITCH_SEL, 0);
    printf("[sw]  USB → Pico (monitoring path)\n");
}

static void switch_to_pc(void)
{
    gpio_put(PIN_USB_SWITCH_SEL, 1);
    printf("[sw]  USB → PC (passthrough path)\n");
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(void)
{
    // Pico SDK board init (sets up clocks, SysTick, etc.)
    board_init();
    stdio_init_all();

    printf("\n=== USB Wake Switch ===\n");
    printf("Initial state: %s\n\n", state_name(s_state));

    // USB switch select pin
    gpio_init(PIN_USB_SWITCH_SEL);
    gpio_set_dir(PIN_USB_SWITCH_SEL, GPIO_OUT);
    switch_to_pico();   // start in monitoring position

    // Power button + VBUS sense
    power_ctrl_init();

    // TinyUSB host
    tuh_init(BOARD_TUH_RHPORT);

    uint32_t power_on_start_ms = 0;
    uint32_t vbus_lost_at_ms   = 0;

    while (true) {
        // Drive the TinyUSB host stack (enumeration, keep-alives, callbacks).
        tuh_task();

        switch (s_state) {

            // ─────────────────────────────────────────────────────────────────
            case STATE_MONITORING:
                if (usb_host_device_is_connected()) {
                    if (power_ctrl_is_pc_on()) {
                        // PC is already on (e.g. device was hot-plugged while PC runs)
                        printf("[fsm] PC already on — skipping power button\n");
                        switch_to_pc();
                        transition(STATE_PASSTHROUGH);
                    } else {
                        power_ctrl_pulse_button(PWR_BTN_PULSE_MS);
                        power_on_start_ms = board_millis();
                        transition(STATE_POWERING_ON);
                    }
                }
                break;

            // ─────────────────────────────────────────────────────────────────
            case STATE_POWERING_ON:
                if (power_ctrl_is_pc_on()) {
                    // PC USB port is live — hand the device over
                    switch_to_pc();
                    // TinyUSB will call tuh_umount_cb as the mux disconnects D+/D─
                    // from the Pico; that sets s_device_connected = false which is fine.
                    vbus_lost_at_ms = 0;
                    transition(STATE_PASSTHROUGH);
                } else if (board_millis() - power_on_start_ms > PC_BOOT_TIMEOUT_MS) {
                    printf("[fsm] timeout waiting for PC — returning to MONITORING\n");
                    transition(STATE_MONITORING);
                }
                break;

            // ─────────────────────────────────────────────────────────────────
            case STATE_PASSTHROUGH:
                if (!power_ctrl_is_pc_on()) {
                    if (vbus_lost_at_ms == 0) {
                        vbus_lost_at_ms = board_millis();
                    } else if (board_millis() - vbus_lost_at_ms > PC_OFF_CONFIRM_MS) {
                        // PC is genuinely off (not a transient re-enumeration)
                        switch_to_pico();
                        usb_host_clear_connected();
                        vbus_lost_at_ms = 0;
                        transition(STATE_DORMANT);
                    }
                } else {
                    // VBUS is back (PC woke from sleep, etc.) — reset the loss timer
                    vbus_lost_at_ms = 0;
                }
                break;

            // ─────────────────────────────────────────────────────────────────
            case STATE_DORMANT:
                // Wait for the device to be physically unplugged before resuming
                // monitoring.  This prevents the board from immediately re-waking a
                // PC that the user intentionally shut down.
                if (!usb_host_device_is_connected()) {
                    printf("[fsm] device removed — ready to monitor again\n");
                    transition(STATE_MONITORING);
                }
                break;
        }
    }

    // unreachable
    return 0;
}
