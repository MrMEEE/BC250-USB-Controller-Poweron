#include <Arduino.h>

#include <cstdarg>
#include <cstring>

#include "usb/usb_helpers.h"
#include "usb/usb_host.h"

#ifndef USB_SWITCH_SEL_PIN
#define USB_SWITCH_SEL_PIN 10
#endif

#ifndef USB_SWITCH_PC_SEL_LEVEL
#define USB_SWITCH_PC_SEL_LEVEL 1
#endif

#ifndef PWR_BTN_PIN
#define PWR_BTN_PIN 13
#endif

#ifndef PC_VBUS_SENSE_PIN
#define PC_VBUS_SENSE_PIN 9
#endif

#ifndef DEV_VBUS_SENSE_PIN
#define DEV_VBUS_SENSE_PIN 12
#endif

#ifndef WAKE_DETECT_FROM_HID
#define WAKE_DETECT_FROM_HID 1
#endif

#ifndef ENABLE_LOGGING
#define ENABLE_LOGGING 1
#endif

#ifndef PSU_ON_PIN
#define PSU_ON_PIN 8
#endif

#ifndef PSU_ON_ACTIVE_LOW
#define PSU_ON_ACTIVE_LOW 0
#endif

#ifndef STATUS_LED_PIN
#ifdef LED_BUILTIN
#define STATUS_LED_PIN LED_BUILTIN
#else
#define STATUS_LED_PIN -1
#endif
#endif

#ifndef STATUS_LED_ACTIVE_LOW
#define STATUS_LED_ACTIVE_LOW 0
#endif

#ifndef ENABLE_POST_SHUTDOWN_COOLDOWN
#define ENABLE_POST_SHUTDOWN_COOLDOWN 1
#endif

#ifndef POST_SHUTDOWN_COOLDOWN_MS
#define POST_SHUTDOWN_COOLDOWN_MS 10000U
#endif

static constexpr uint32_t PSU_SETTLE_DELAY_MS = 1000U;
static constexpr uint32_t PWR_BTN_PULSE_MS = 150U;
static constexpr uint32_t HOST_BOOT_TIMEOUT_MS = 90000U;
static constexpr uint32_t HOST_OFF_CONFIRM_MS = 3000U;
static constexpr uint32_t USB_HOST_POLL_MS = 10U;
static constexpr uint16_t STEAM_DONGLE_VID = 0x28de;
static constexpr uint16_t STEAM_DONGLE_PID = 0x1142;
static constexpr uint8_t STEAM_DONGLE_IFACE_PREFERRED = 1;
static constexpr uint32_t HID_MIN_SILENCE_MS = 800U;
static constexpr uint32_t HID_BURST_MS = 200U;
static constexpr uint8_t HID_BURST_COUNT = 3U;
static constexpr uint32_t HID_REARM_SILENCE_MS = 1500U;

static constexpr uint8_t HID_PREFIX_A[] = {0x01, 0x00, 0x01, 0x3c};
static constexpr uint8_t HID_PREFIX_B[] = {0x01, 0x00, 0x04, 0x0b};
static constexpr uint8_t HID_PREFIX_C[] = {0x01, 0x00, 0x03, 0x01};

enum system_state_t {
  STATE_MONITORING,
  STATE_WAITING_FOR_PSU,
  STATE_POWERING_ON,
  STATE_PASSTHROUGH,
  STATE_DORMANT,
};

static system_state_t s_state = STATE_MONITORING;
static uint32_t s_psu_on_started_ms = 0;
static uint32_t s_power_on_start_ms = 0;
static uint32_t s_host_lost_at_ms = 0;
static bool s_status_led_enabled = false;
static bool s_status_led_blink_state = false;
static uint32_t s_status_led_last_toggle_ms = 0;
static bool s_hid_target_present = false;
static bool s_hid_detector_armed = true;
static bool s_hid_wake_pending = false;
static uint32_t s_post_shutdown_cooldown_until_ms = 0;
static uint32_t s_hid_last_match_ms = 0;
static uint32_t s_hid_burst_start_ms = 0;
static uint8_t s_hid_burst_count = 0;
static bool s_hid_host_ready = false;
static bool s_usb_event_new_device = false;
static bool s_usb_event_device_gone = false;
static uint8_t s_usb_event_dev_addr = 0;
static usb_device_handle_t s_usb_event_dev_hdl = nullptr;
static usb_host_client_handle_t s_usb_client = nullptr;
static usb_device_handle_t s_usb_dev_hdl = nullptr;
static usb_transfer_t* s_usb_transfer = nullptr;
static uint8_t s_usb_target_iface = 0;
static uint8_t s_usb_target_endpoint = 0;
static uint8_t s_usb_target_mps = 0;
static uint8_t s_usb_target_dev_addr = 0;
static bool s_usb_cleanup_requested = false;
static uint32_t s_usb_last_scan_ms = 0;
static uint32_t s_usb_last_scan_empty_log_ms = 0;

#if ENABLE_LOGGING
static void dbg_log_print(const char* text) {
  if (text == nullptr) {
    return;
  }
  Serial.print(text);
}

static void dbg_log_println() {
  Serial.println();
}

static void dbg_log_println(const char* text) {
  Serial.println(text);
}

static void dbg_log_printf(const char* fmt, ...) {
  if (fmt == nullptr) {
    return;
  }

  char buffer[256];
  va_list args;
  va_start(args, fmt);
  const int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  if (written <= 0) {
    return;
  }

  Serial.print(buffer);
}

#define LOG(...) dbg_log_print(__VA_ARGS__)
#define LOGLN(...) dbg_log_println(__VA_ARGS__)
#define LOGF(...) dbg_log_printf(__VA_ARGS__)
#else
#define LOG(...) ((void) 0)
#define LOGLN(...) ((void) 0)
#define LOGF(...) ((void) 0)
#endif

static const char* state_name(system_state_t s) {
  switch (s) {
    case STATE_MONITORING: return "MONITORING";
    case STATE_WAITING_FOR_PSU: return "WAITING_FOR_PSU";
    case STATE_POWERING_ON: return "POWERING_ON";
    case STATE_PASSTHROUGH: return "PASSTHROUGH";
    case STATE_DORMANT: return "DORMANT";
    default: return "UNKNOWN";
  }
}

static void transition(system_state_t next) {
  LOGF("[fsm] %s -> %s\n", state_name(s_state), state_name(next));
  s_state = next;
}

static inline bool host_is_online() {
  return digitalRead(PC_VBUS_SENSE_PIN) == HIGH;
}

static inline bool fallback_device_vbus_present() {
  return digitalRead(DEV_VBUS_SENSE_PIN) == HIGH;
}

static inline bool wake_event_detected() {
  if (ENABLE_POST_SHUTDOWN_COOLDOWN &&
      s_post_shutdown_cooldown_until_ms != 0 &&
      millis() < s_post_shutdown_cooldown_until_ms) {
    return false;
  }

  if (!s_hid_host_ready) {
    return fallback_device_vbus_present();
  }

  if (!s_hid_wake_pending) {
    return false;
  }

  s_hid_wake_pending = false;
  return true;
}

static inline bool device_present() {
  if (s_hid_host_ready) {
    return s_hid_target_present;
  }
  return fallback_device_vbus_present();
}

static bool report_has_wake_prefix(uint8_t const* report, uint16_t len) {
  if (len < sizeof(HID_PREFIX_A)) {
    return false;
  }

  return memcmp(report, HID_PREFIX_A, sizeof(HID_PREFIX_A)) == 0 ||
         memcmp(report, HID_PREFIX_B, sizeof(HID_PREFIX_B)) == 0 ||
         memcmp(report, HID_PREFIX_C, sizeof(HID_PREFIX_C)) == 0;
}

static void hid_reset_detector_state() {
  s_hid_detector_armed = true;
  s_hid_wake_pending = false;
  s_hid_last_match_ms = 0;
  s_hid_burst_start_ms = 0;
  s_hid_burst_count = 0;
}

static void start_post_shutdown_cooldown(uint32_t now) {
  if (!ENABLE_POST_SHUTDOWN_COOLDOWN) {
    s_post_shutdown_cooldown_until_ms = 0;
    return;
  }

  s_post_shutdown_cooldown_until_ms = now + POST_SHUTDOWN_COOLDOWN_MS;
  LOGF("[fsm] post-shutdown cooldown active until %lu ms\n",
       static_cast<unsigned long>(s_post_shutdown_cooldown_until_ms));
}

static void hid_note_matching_report(uint32_t now) {
  if (!s_hid_detector_armed) {
    if (s_hid_last_match_ms != 0 && (now - s_hid_last_match_ms) >= HID_REARM_SILENCE_MS) {
      s_hid_detector_armed = true;
      s_hid_burst_start_ms = 0;
      s_hid_burst_count = 0;
      LOGF("[hid] detector re-armed at %lu ms\n", static_cast<unsigned long>(now));
    } else {
      s_hid_last_match_ms = now;
      return;
    }
  }

  if (s_hid_last_match_ms == 0 || (now - s_hid_last_match_ms) > HID_MIN_SILENCE_MS) {
    s_hid_burst_start_ms = now;
    s_hid_burst_count = 0;
  }

  if (s_hid_burst_start_ms == 0) {
    s_hid_burst_start_ms = now;
  }

  if ((now - s_hid_burst_start_ms) <= HID_BURST_MS) {
    s_hid_burst_count++;
  } else {
    s_hid_burst_start_ms = now;
    s_hid_burst_count = 1;
  }

  s_hid_last_match_ms = now;

  if (s_hid_burst_count >= HID_BURST_COUNT) {
    s_hid_wake_pending = true;
    s_hid_detector_armed = false;
    LOGF("[hid] wake burst detected at %lu ms\n", static_cast<unsigned long>(now));
  }
}

static void usb_host_request_cleanup() {
  s_usb_cleanup_requested = true;
}

static void usb_host_close_target() {
  if (s_usb_transfer != nullptr) {
    usb_host_transfer_free(s_usb_transfer);
    s_usb_transfer = nullptr;
  }

  if (s_usb_client != nullptr && s_usb_dev_hdl != nullptr) {
    usb_host_interface_release(s_usb_client, s_usb_dev_hdl, s_usb_target_iface);
    usb_host_device_close(s_usb_client, s_usb_dev_hdl);
  }

  s_usb_dev_hdl = nullptr;
  s_usb_target_dev_addr = 0;
  s_usb_target_iface = 0;
  s_usb_target_endpoint = 0;
  s_usb_target_mps = 0;
  s_hid_target_present = false;
  hid_reset_detector_state();
}

static void usb_transfer_complete_cb(usb_transfer_t* transfer) {
  if (transfer == nullptr) {
    return;
  }

  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes >= 4) {
    const uint8_t* report = transfer->data_buffer;
    const uint16_t len = static_cast<uint16_t>(transfer->actual_num_bytes);
    const uint32_t now = millis();
    if (report_has_wake_prefix(report, len)) {
      hid_note_matching_report(now);
    }
  }

  if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE || transfer->status == USB_TRANSFER_STATUS_CANCELED ||
      transfer->status == USB_TRANSFER_STATUS_ERROR || transfer->status == USB_TRANSFER_STATUS_STALL) {
    usb_host_request_cleanup();
    return;
  }

  if (s_usb_cleanup_requested || !s_hid_target_present || s_usb_dev_hdl == nullptr) {
    return;
  }

  transfer->device_handle = s_usb_dev_hdl;
  transfer->bEndpointAddress = s_usb_target_endpoint;
  transfer->num_bytes = static_cast<int>(usb_round_up_to_mps(64, s_usb_target_mps));
  if (usb_host_transfer_submit(transfer) != ESP_OK) {
    LOGLN("[usb] failed to resubmit interrupt transfer");
    usb_host_request_cleanup();
  }
}

static bool usb_host_bind_target(uint8_t dev_addr) {
  usb_device_handle_t dev_hdl = nullptr;
  LOGF("[usb] probing addr %u\n", dev_addr);
  esp_err_t err = usb_host_device_open(s_usb_client, dev_addr, &dev_hdl);
  if (err != ESP_OK) {
    LOGF("[usb] device open failed for addr %u: %d\n", dev_addr, static_cast<int>(err));
    return false;
  }

  const usb_device_desc_t* device_desc = nullptr;
  err = usb_host_get_device_descriptor(dev_hdl, &device_desc);
  if (err != ESP_OK || device_desc == nullptr) {
    LOGF("[usb] get device descriptor failed for addr %u: %d\n", dev_addr, static_cast<int>(err));
    usb_host_device_close(s_usb_client, dev_hdl);
    return false;
  }

  LOGF("[usb] addr %u VID:PID %04x:%04x class 0x%02x\n",
                dev_addr,
                device_desc->idVendor,
                device_desc->idProduct,
                device_desc->bDeviceClass);

  if (device_desc->idVendor != STEAM_DONGLE_VID || device_desc->idProduct != STEAM_DONGLE_PID) {
    usb_host_device_close(s_usb_client, dev_hdl);
    return false;
  }

  const usb_config_desc_t* config_desc = nullptr;
  err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
  if (err != ESP_OK || config_desc == nullptr) {
    LOGF("[usb] failed to read config descriptor: %d\n", static_cast<int>(err));
    usb_host_device_close(s_usb_client, dev_hdl);
    return false;
  }

  LOGF("[usb] config total length: %u bytes\n", static_cast<unsigned>(config_desc->wTotalLength));

  uint8_t selected_iface = 0xFF;
  const usb_ep_desc_t* selected_ep_desc = nullptr;
  int selected_intf_offset = 0;

  for (uint8_t iface = 0; iface < 8; ++iface) {
    int intf_offset = 0;
    const usb_intf_desc_t* intf_desc = usb_parse_interface_descriptor(config_desc, iface, 0, &intf_offset);
    if (intf_desc == nullptr) {
      continue;
    }

    LOGF("[usb] iface %u class 0x%02x subclass 0x%02x proto 0x%02x eps %u\n",
                  intf_desc->bInterfaceNumber,
                  intf_desc->bInterfaceClass,
                  intf_desc->bInterfaceSubClass,
                  intf_desc->bInterfaceProtocol,
                  intf_desc->bNumEndpoints);

    const usb_ep_desc_t* candidate_ep = nullptr;
    for (int index = 0; index < intf_desc->bNumEndpoints; ++index) {
      const usb_ep_desc_t* ep_desc =
          usb_parse_endpoint_descriptor_by_index(intf_desc, index, config_desc->wTotalLength, &intf_offset);
      if (ep_desc == nullptr) {
        continue;
      }

      const bool is_interrupt_in = USB_EP_DESC_GET_XFERTYPE(ep_desc) == USB_BM_ATTRIBUTES_XFER_INT &&
                                   USB_EP_DESC_GET_EP_DIR(ep_desc) == 1;
      LOGF("[usb]   ep 0x%02x attr 0x%02x mps %u interval %u%s\n",
                    ep_desc->bEndpointAddress,
                    ep_desc->bmAttributes,
                    USB_EP_DESC_GET_MPS(ep_desc),
                    ep_desc->bInterval,
                    is_interrupt_in ? " <- int IN" : "");

      if (is_interrupt_in) {
        candidate_ep = ep_desc;
      }
    }

    if (intf_desc->bInterfaceClass != USB_CLASS_HID || candidate_ep == nullptr) {
      continue;
    }

    // Prefer the known interface number, but accept any HID interrupt-IN interface.
    if (selected_ep_desc == nullptr || intf_desc->bInterfaceNumber == STEAM_DONGLE_IFACE_PREFERRED) {
      selected_iface = intf_desc->bInterfaceNumber;
      selected_ep_desc = candidate_ep;
      selected_intf_offset = intf_offset;

      if (selected_iface == STEAM_DONGLE_IFACE_PREFERRED) {
        break;
      }
    }
  }

  if (selected_ep_desc == nullptr) {
    LOGLN("[usb] no HID interrupt-IN interface found on dongle");
    usb_host_device_close(s_usb_client, dev_hdl);
    return false;
  }

  err = usb_host_interface_claim(s_usb_client, dev_hdl, selected_iface, 0);
  if (err != ESP_OK) {
    LOGF("[usb] interface claim failed: %d\n", static_cast<int>(err));
    usb_host_device_close(s_usb_client, dev_hdl);
    return false;
  }

  const int mps = USB_EP_DESC_GET_MPS(selected_ep_desc);
  const size_t transfer_size = static_cast<size_t>(usb_round_up_to_mps(64, mps));
  if (transfer_size == 0) {
    LOGLN("[usb] invalid interrupt endpoint MPS");
    usb_host_interface_release(s_usb_client, dev_hdl, selected_iface);
    usb_host_device_close(s_usb_client, dev_hdl);
    return false;
  }

  if (s_usb_transfer != nullptr) {
    usb_host_transfer_free(s_usb_transfer);
    s_usb_transfer = nullptr;
  }

  err = usb_host_transfer_alloc(transfer_size, 0, &s_usb_transfer);
  if (err != ESP_OK || s_usb_transfer == nullptr) {
    LOGF("[usb] transfer allocation failed: %d\n", static_cast<int>(err));
    usb_host_interface_release(s_usb_client, dev_hdl, selected_iface);
    usb_host_device_close(s_usb_client, dev_hdl);
    return false;
  }

  s_usb_dev_hdl = dev_hdl;
  s_usb_target_dev_addr = dev_addr;
  s_usb_target_iface = selected_iface;
  s_usb_target_endpoint = selected_ep_desc->bEndpointAddress;
  s_usb_target_mps = static_cast<uint8_t>(mps);
  s_hid_target_present = true;
  hid_reset_detector_state();

  s_usb_transfer->device_handle = s_usb_dev_hdl;
  s_usb_transfer->bEndpointAddress = s_usb_target_endpoint;
  s_usb_transfer->num_bytes = static_cast<int>(transfer_size);
  s_usb_transfer->callback = usb_transfer_complete_cb;
  s_usb_transfer->context = nullptr;

  err = usb_host_transfer_submit(s_usb_transfer);
  if (err != ESP_OK) {
    LOGF("[usb] initial interrupt transfer submit failed: %d\n", static_cast<int>(err));
    usb_host_transfer_free(s_usb_transfer);
    s_usb_transfer = nullptr;
    usb_host_interface_release(s_usb_client, dev_hdl, selected_iface);
    usb_host_device_close(s_usb_client, dev_hdl);
    s_usb_dev_hdl = nullptr;
    s_hid_target_present = false;
    return false;
  }

  (void) selected_intf_offset;
  LOGF("[usb] Steam Controller dongle bound at addr %u, iface %u, ep 0x%02x\n",
                dev_addr,
                selected_iface,
                selected_ep_desc->bEndpointAddress);
  return true;
}

static void usb_client_event_cb(const usb_host_client_event_msg_t* event_msg, void* arg) {
  (void) arg;

  switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
      s_usb_event_new_device = true;
      s_usb_event_dev_addr = event_msg->new_dev.address;
      break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
      s_usb_event_device_gone = true;
      s_usb_event_dev_hdl = event_msg->dev_gone.dev_hdl;
      break;
  }
}

static void usb_host_service_once() {
  if (!s_hid_host_ready) {
    return;
  }

  uint32_t host_flags = 0;
  usb_host_lib_handle_events(pdMS_TO_TICKS(USB_HOST_POLL_MS), &host_flags);
  usb_host_client_handle_events(s_usb_client, pdMS_TO_TICKS(USB_HOST_POLL_MS));

  if (s_usb_cleanup_requested) {
    usb_host_close_target();
    s_usb_cleanup_requested = false;
  }

  if (s_usb_event_device_gone) {
    if (s_usb_dev_hdl == s_usb_event_dev_hdl) {
      LOGLN("[usb] target device removed");
      usb_host_close_target();
    }
    s_usb_event_device_gone = false;
    s_usb_event_dev_hdl = nullptr;
  }

  if (s_usb_event_new_device) {
    const uint8_t dev_addr = s_usb_event_dev_addr;
    s_usb_event_new_device = false;
    s_usb_event_dev_addr = 0;

    if (!s_hid_target_present) {
      usb_host_bind_target(dev_addr);
    }
  }

  if (!s_hid_target_present) {
    const uint32_t now = millis();
    if ((now - s_usb_last_scan_ms) >= 1000U) {
      s_usb_last_scan_ms = now;

      uint8_t addr_list[8] = {};
      int num_dev = 0;
      const esp_err_t scan_err = usb_host_device_addr_list_fill(8, addr_list, &num_dev);
      if (scan_err == ESP_OK) {
        if (num_dev > 0) {
          LOGF("[usb] scan found %d device(s)\n", num_dev);
          s_usb_last_scan_empty_log_ms = now;
        } else if ((now - s_usb_last_scan_empty_log_ms) >= 5000U) {
          LOGF("[usb] scan: 0 devices\n");
          s_usb_last_scan_empty_log_ms = now;
        }

        for (int i = 0; i < num_dev && !s_hid_target_present; ++i) {
          usb_host_bind_target(addr_list[i]);
        }
      } else {
        LOGF("[usb] device list scan failed: %d\n", static_cast<int>(scan_err));
      }
    }
  }
}

static void start_usb_host_stack() {
  if (s_hid_host_ready) {
    return;
  }

  usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };
  if (usb_host_install(&host_config) != ESP_OK) {
    LOGLN("[usb] failed to install host library");
    return;
  }

  usb_host_client_config_t client_config = {};
  client_config.is_synchronous = false;
  client_config.max_num_event_msg = 8;
  client_config.async.client_event_callback = usb_client_event_cb;
  client_config.async.callback_arg = nullptr;
  if (usb_host_client_register(&client_config, &s_usb_client) != ESP_OK) {
    LOGLN("[usb] failed to register host client");
    usb_host_uninstall();
    return;
  }

  s_hid_host_ready = true;
  s_usb_last_scan_ms = 0;
  s_usb_last_scan_empty_log_ms = 0;
  hid_reset_detector_state();
  LOGLN("[usb] host library initialized");

  // A dongle may already be connected before client registration and won't always trigger NEW_DEV.
  uint8_t addr_list[8] = {};
  int num_dev = 0;
  const esp_err_t scan_err = usb_host_device_addr_list_fill(8, addr_list, &num_dev);
  if (scan_err == ESP_OK) {
    if (num_dev > 0) {
      LOGF("[usb] initial scan found %d device(s)\n", num_dev);
    } else {
      LOGLN("[usb] initial scan: 0 devices");
    }
    for (int i = 0; i < num_dev && !s_hid_target_present; ++i) {
      usb_host_bind_target(addr_list[i]);
    }
  } else {
    LOGF("[usb] initial scan failed: %d\n", static_cast<int>(scan_err));
  }
}

static void switch_to_monitor_path() {
  const int sel_level = USB_SWITCH_PC_SEL_LEVEL ? LOW : HIGH;
  digitalWrite(USB_SWITCH_SEL_PIN, sel_level);
  LOGF("[sw] USB -> ESP32-S2 monitor path (SEL=%d)\n", sel_level);
}

static void switch_to_pc_path() {
  const int sel_level = USB_SWITCH_PC_SEL_LEVEL ? HIGH : LOW;
  digitalWrite(USB_SWITCH_SEL_PIN, sel_level);
  LOGF("[sw] USB -> PC passthrough path (SEL=%d)\n", sel_level);
}

static void pulse_power_button(uint32_t ms) {
  LOGF("[pwr] pressing power button for %lu ms\n", static_cast<unsigned long>(ms));
  digitalWrite(PWR_BTN_PIN, HIGH);
  delay(ms);
  digitalWrite(PWR_BTN_PIN, LOW);
  LOGLN("[pwr] power button released");
}

static void set_psu_enabled(bool logical_on) {
  const bool physical_on = PSU_ON_ACTIVE_LOW ? !logical_on : logical_on;
  digitalWrite(PSU_ON_PIN, physical_on ? HIGH : LOW);
  LOGF("[psu] %s\n", logical_on ? "enabled" : "disabled");
}

static void set_status_led(bool logical_on) {
  if (!s_status_led_enabled) {
    return;
  }

  const bool physical_on = STATUS_LED_ACTIVE_LOW ? !logical_on : logical_on;
  digitalWrite(STATUS_LED_PIN, physical_on ? HIGH : LOW);
}

static void update_status_led(bool host_online, uint32_t now) {
  if (!s_status_led_enabled) {
    return;
  }

  if (host_online) {
    set_status_led(true);
    return;
  }

  if ((now - s_status_led_last_toggle_ms) >= 500U) {
    s_status_led_last_toggle_ms = now;
    s_status_led_blink_state = !s_status_led_blink_state;
    set_status_led(s_status_led_blink_state);
  }
}

void setup() {
#if ENABLE_LOGGING
  Serial.begin(115200);
#endif
  delay(300);

  pinMode(USB_SWITCH_SEL_PIN, OUTPUT);
  pinMode(PWR_BTN_PIN, OUTPUT);
  pinMode(PC_VBUS_SENSE_PIN, INPUT);
  pinMode(DEV_VBUS_SENSE_PIN, INPUT);
  pinMode(PSU_ON_PIN, OUTPUT);

  if (STATUS_LED_PIN < 0) {
    LOGLN("[led] status LED disabled (STATUS_LED_PIN < 0)");
  } else if (STATUS_LED_PIN == USB_SWITCH_SEL_PIN ||
             STATUS_LED_PIN == PWR_BTN_PIN ||
             STATUS_LED_PIN == PC_VBUS_SENSE_PIN ||
             STATUS_LED_PIN == DEV_VBUS_SENSE_PIN ||
             STATUS_LED_PIN == PSU_ON_PIN) {
    LOGF("[led] status LED disabled (pin conflict on GPIO%d)\n", STATUS_LED_PIN);
  } else {
    pinMode(STATUS_LED_PIN, OUTPUT);
    s_status_led_enabled = true;
    s_status_led_last_toggle_ms = millis();
    set_status_led(false);
    LOGF("[led] status LED enabled on GPIO%d (active_%s)\n",
                  STATUS_LED_PIN,
                  STATUS_LED_ACTIVE_LOW ? "low" : "high");
  }

  digitalWrite(PWR_BTN_PIN, LOW);
  set_psu_enabled(false);
  switch_to_monitor_path();

#if WAKE_DETECT_FROM_HID
  start_usb_host_stack();
#endif

  LOGLN();
  LOGLN("=== USB Wake Switch (ESP32-S2) ===");
  LOGF("Initial state: %s\n", state_name(s_state));
#if WAKE_DETECT_FROM_HID
  LOGLN(s_hid_host_ready ? "Wake detect mode: raw USB host HID detector" : "Wake detect mode: VBUS fallback");
#else
  LOGLN("Wake detect mode: fallback VBUS sense on DEV_VBUS_SENSE_PIN");
#endif
  LOGLN("Power sequence: controller -> PSU on -> 1s settle -> BC250 power button -> USB handoff");
}

void loop() {
  const uint32_t now = millis();

#if WAKE_DETECT_FROM_HID
  usb_host_service_once();
#endif

  update_status_led(host_is_online(), now);

  switch (s_state) {
    case STATE_MONITORING:
      if (wake_event_detected()) {
        if (host_is_online()) {
          LOGLN("[fsm] host already online - skipping PSU/button sequence");
          switch_to_pc_path();
          transition(STATE_PASSTHROUGH);
        } else {
          set_psu_enabled(true);
          s_psu_on_started_ms = now;
          transition(STATE_WAITING_FOR_PSU);
        }
      }
      break;

    case STATE_WAITING_FOR_PSU:
      if (!device_present()) {
        set_psu_enabled(false);
        transition(STATE_MONITORING);
      } else if ((now - s_psu_on_started_ms) >= PSU_SETTLE_DELAY_MS) {
        pulse_power_button(PWR_BTN_PULSE_MS);
        s_power_on_start_ms = now;
        transition(STATE_POWERING_ON);
      }
      break;

    case STATE_POWERING_ON:
      if (!device_present()) {
        set_psu_enabled(false);
        transition(STATE_MONITORING);
      } else if (host_is_online()) {
        switch_to_pc_path();
        s_host_lost_at_ms = 0;
        transition(STATE_PASSTHROUGH);
      } else if ((now - s_power_on_start_ms) > HOST_BOOT_TIMEOUT_MS) {
        LOGLN("[fsm] timeout waiting for host - disabling PSU and returning to MONITORING");
        set_psu_enabled(false);
        transition(STATE_MONITORING);
      }
      break;

    case STATE_PASSTHROUGH:
      if (!host_is_online()) {
        if (s_host_lost_at_ms == 0) {
          s_host_lost_at_ms = now;
        } else if ((now - s_host_lost_at_ms) > HOST_OFF_CONFIRM_MS) {
          set_psu_enabled(false);
          switch_to_monitor_path();
          s_host_lost_at_ms = 0;
          start_post_shutdown_cooldown(now);
          transition(STATE_MONITORING);
        }
      } else {
        s_host_lost_at_ms = 0;
      }
      break;

    case STATE_DORMANT:
      transition(STATE_MONITORING);
      break;
  }

  delay(5);
}
