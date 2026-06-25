#ifndef USB_HOST_H_
#define USB_HOST_H_

#include <stdbool.h>

/**
 * Return true if a USB device is currently mounted on the host port.
 * Thread-safe: updated from TinyUSB callbacks, read from the main loop.
 */
bool usb_host_device_is_connected(void);

/**
 * Clear the connected flag.  Call this after switching the USB mux away from
 * the Pico so that the next physical insertion is detected as a fresh event.
 */
void usb_host_clear_connected(void);

#endif /* USB_HOST_H_ */
