#include "usb_host.h"
#include "tusb.h"
#include <stdio.h>

// Volatile because it is written from TinyUSB callbacks and read from the main loop.
static volatile bool s_device_connected = false;

bool usb_host_device_is_connected(void)
{
    return s_device_connected;
}

void usb_host_clear_connected(void)
{
    s_device_connected = false;
}

// ── TinyUSB required callbacks ───────────────────────────────────────────────

void tuh_mount_cb(uint8_t dev_addr)
{
    // A device has been enumerated and is ready.
    printf("[usb] device mounted (addr %u)\n", dev_addr);
    s_device_connected = true;
}

void tuh_umount_cb(uint8_t dev_addr)
{
    // Device disconnected (or mux switched away — TinyUSB treats both the same).
    printf("[usb] device unmounted (addr %u)\n", dev_addr);
    s_device_connected = false;
}

// ── Optional HID callbacks ───────────────────────────────────────────────────
// These are called by TinyUSB when HID_CLASS support is enabled (CFG_TUH_HID > 0).
// We do not need to process HID reports — we only care about mount/umount —
// but TinyUSB requires these symbols to be defined when the HID driver is active.

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *desc_report, uint16_t desc_len)
{
    (void)desc_report;
    (void)desc_len;
    printf("[usb] HID interface mounted (addr %u, inst %u)\n", dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    printf("[usb] HID interface unmounted (addr %u, inst %u)\n", dev_addr, instance);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                 uint8_t const *report, uint16_t len)
{
    // Not used — passthrough is handled in hardware by the USB switch IC.
    (void)dev_addr;
    (void)instance;
    (void)report;
    (void)len;
}
