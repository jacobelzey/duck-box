#include "leds.h"

#include <Arduino.h>

#include "config.h"
#include "pulse.h"

namespace leds {

namespace {
constexpr uint32_t kMaxDuty = (1u << cfg::LED_PWM_BITS) - 1u;
// LEDC channel n is used for LED n. Channels 0/1 share one hardware timer
// and 2/3 another; both timers get the same frequency and resolution.
}  // namespace

void begin() {
  if (cfg::POWER_SAVE) {
    // No PWM unit: the LEDs are flashed once per cycle by pulse::window().
    for (uint8_t i = 0; i < cfg::LED_COUNT; ++i) {
      pinMode(cfg::PIN_LED[i], OUTPUT);
      digitalWrite(cfg::PIN_LED[i], LOW);
    }
    return;
  }
  for (uint8_t i = 0; i < cfg::LED_COUNT; ++i) {
    ledcSetup(i, cfg::LED_PWM_HZ, cfg::LED_PWM_BITS);
    ledcAttachPin(cfg::PIN_LED[i], i);
    ledcWrite(i, 0);
  }
}

void set(uint8_t led, float duty) {
  if (led >= cfg::LED_COUNT) return;
  if (duty < 0.0f) duty = 0.0f;
  if (duty > 1.0f) duty = 1.0f;
  if (cfg::POWER_SAVE) {
    pulse::setLedOnUs(led, (uint32_t)(duty * (float)(cfg::CYCLE_MS * 1000u) + 0.5f));
    return;
  }
  ledcWrite(led, (uint32_t)(duty * (float)kMaxDuty + 0.5f));
}

void allOff() {
  if (cfg::POWER_SAVE) {
    pulse::ledsOff();
    return;
  }
  for (uint8_t i = 0; i < cfg::LED_COUNT; ++i) ledcWrite(i, 0);
}

}  // namespace leds
