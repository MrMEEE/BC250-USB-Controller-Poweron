#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

// ── Controller ──────────────────────────────────────────────────────────────
// Roothub port 0 operates in host mode.
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_HOST

// ── OS / scheduler ──────────────────────────────────────────────────────────
// No RTOS; driven by tuh_task() in the main loop.
#define CFG_TUSB_OS             OPT_OS_NONE

// ── Debug ───────────────────────────────────────────────────────────────────
// Set to 1 or 2 for verbose TinyUSB log output on UART.
#define CFG_TUSB_DEBUG          0

// ── Memory ──────────────────────────────────────────────────────────────────
#define CFG_TUSB_MEM_SECTION    /* default */
#define CFG_TUSB_MEM_ALIGN      __attribute__((aligned(4)))

// ── Host: enumeration buffer ─────────────────────────────────────────────────
#define CFG_TUH_ENUMERATION_BUFSIZE  256

// ── Host: hub support (allows devices via a USB hub) ─────────────────────────
#define CFG_TUH_HUB             1

// ── Host: HID ────────────────────────────────────────────────────────────────
// Up to 4 HID interfaces (e.g. controller with multiple endpoints).
#define CFG_TUH_HID             4
#define CFG_TUH_HID_EPIN_BUFSIZE  64

// ── Host: generic/vendor class ───────────────────────────────────────────────
// NOTE: Disabled due to TinyUSB version compatibility issues with vendor_host.h.
// The generic mount/umount callbacks (tuh_mount_cb/tuh_umount_cb) work for all
// device classes, so vendor class support is not needed for basic enumeration.
#define CFG_TUH_VENDOR          0

// ── Board-specific: roothub port for host mode ──────────────────────────────
// On Pico (RP2040), there is one USB port: port 0
#define BOARD_TUH_RHPORT        0

#endif /* TUSB_CONFIG_H_ */
