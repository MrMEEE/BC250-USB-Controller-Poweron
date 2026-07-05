#include <Arduino.h>

#ifndef USB_SWITCH_SEL_PIN
#define USB_SWITCH_SEL_PIN 15
#endif

#ifndef PWR_BTN_PIN
#define PWR_BTN_PIN 13
#endif

#ifndef PC_VBUS_SENSE_PIN
#define PC_VBUS_SENSE_PIN 14
#endif

#ifndef DEV_VBUS_SENSE_PIN
#define DEV_VBUS_SENSE_PIN 12
#endif

#ifndef PSU_ON_PIN
#define PSU_ON_PIN 16
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

static constexpr uint32_t PSU_SETTLE_DELAY_MS = 1000U;
static constexpr uint32_t PWR_BTN_PULSE_MS = 150U;
static constexpr uint32_t HOST_BOOT_TIMEOUT_MS = 90000U;
static constexpr uint32_t HOST_OFF_CONFIRM_MS = 3000U;

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
  Serial.printf("[fsm] %s -> %s\n", state_name(s_state), state_name(next));
  s_state = next;
}

static inline bool host_is_online() {
  return digitalRead(PC_VBUS_SENSE_PIN) == HIGH;
}

static inline bool device_is_connected() {
  return digitalRead(DEV_VBUS_SENSE_PIN) == HIGH;
}

static void switch_to_monitor_path() {
  digitalWrite(USB_SWITCH_SEL_PIN, LOW);
  Serial.println("[sw] USB -> ESP32-S2 monitor path");
}

static void switch_to_pc_path() {
  digitalWrite(USB_SWITCH_SEL_PIN, HIGH);
  Serial.println("[sw] USB -> PC passthrough path");
}

static void pulse_power_button(uint32_t ms) {
  Serial.printf("[pwr] pressing power button for %lu ms\n", static_cast<unsigned long>(ms));
  digitalWrite(PWR_BTN_PIN, HIGH);
  delay(ms);
  digitalWrite(PWR_BTN_PIN, LOW);
  Serial.println("[pwr] power button released");
}

static void set_psu_enabled(bool logical_on) {
  const bool physical_on = PSU_ON_ACTIVE_LOW ? !logical_on : logical_on;
  digitalWrite(PSU_ON_PIN, physical_on ? HIGH : LOW);
  Serial.printf("[psu] %s\n", logical_on ? "enabled" : "disabled");
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

  // Host offline/shutdown: 1 Hz blink.
  if ((now - s_status_led_last_toggle_ms) >= 500U) {
    s_status_led_last_toggle_ms = now;
    s_status_led_blink_state = !s_status_led_blink_state;
    set_status_led(s_status_led_blink_state);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(USB_SWITCH_SEL_PIN, OUTPUT);
  pinMode(PWR_BTN_PIN, OUTPUT);
  pinMode(PC_VBUS_SENSE_PIN, INPUT);
  pinMode(DEV_VBUS_SENSE_PIN, INPUT);
  pinMode(PSU_ON_PIN, OUTPUT);

  if (STATUS_LED_PIN < 0) {
    Serial.println("[led] status LED disabled (STATUS_LED_PIN < 0)");
  } else if (STATUS_LED_PIN == USB_SWITCH_SEL_PIN ||
             STATUS_LED_PIN == PWR_BTN_PIN ||
             STATUS_LED_PIN == PC_VBUS_SENSE_PIN ||
             STATUS_LED_PIN == DEV_VBUS_SENSE_PIN ||
             STATUS_LED_PIN == PSU_ON_PIN) {
    Serial.printf("[led] status LED disabled (pin conflict on GPIO%d)\n", STATUS_LED_PIN);
  } else {
    pinMode(STATUS_LED_PIN, OUTPUT);
    s_status_led_enabled = true;
    s_status_led_last_toggle_ms = millis();
    set_status_led(false);
    Serial.printf("[led] status LED enabled on GPIO%d (active_%s)\n",
                  STATUS_LED_PIN,
                  STATUS_LED_ACTIVE_LOW ? "low" : "high");
  }

  digitalWrite(PWR_BTN_PIN, LOW);
  set_psu_enabled(false);
  switch_to_monitor_path();

  Serial.println();
  Serial.println("=== USB Wake Switch (ESP32-S2) ===");
  Serial.printf("Initial state: %s\n", state_name(s_state));
  Serial.println("Device detect mode: VBUS sense on DEV_VBUS_SENSE_PIN");
  Serial.println("Power sequence: controller -> PSU on -> 1s settle -> BC250 power button -> USB handoff");
}

void loop() {
  const uint32_t now = millis();
  update_status_led(host_is_online(), now);

  switch (s_state) {
    case STATE_MONITORING:
      if (device_is_connected()) {
        if (host_is_online()) {
          Serial.println("[fsm] host already online - skipping PSU/button sequence");
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
      if (!device_is_connected()) {
        set_psu_enabled(false);
        transition(STATE_MONITORING);
      } else if ((now - s_psu_on_started_ms) >= PSU_SETTLE_DELAY_MS) {
        pulse_power_button(PWR_BTN_PULSE_MS);
        s_power_on_start_ms = now;
        transition(STATE_POWERING_ON);
      }
      break;

    case STATE_POWERING_ON:
      if (!device_is_connected()) {
        set_psu_enabled(false);
        transition(STATE_MONITORING);
      } else if (host_is_online()) {
        switch_to_pc_path();
        s_host_lost_at_ms = 0;
        transition(STATE_PASSTHROUGH);
      } else if ((now - s_power_on_start_ms) > HOST_BOOT_TIMEOUT_MS) {
        Serial.println("[fsm] timeout waiting for host - disabling PSU and returning to MONITORING");
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
          transition(STATE_DORMANT);
        }
      } else {
        s_host_lost_at_ms = 0;
      }
      break;

    case STATE_DORMANT:
      if (!device_is_connected()) {
        Serial.println("[fsm] device removed - ready to monitor again");
        transition(STATE_MONITORING);
      }
      break;
  }

  delay(5);
}
