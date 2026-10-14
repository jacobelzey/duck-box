#include "power.h"

#include <Arduino.h>

#include "driver/gpio.h"
#include "esp_sleep.h"

#include "config.h"
#include "pulse.h"

namespace power {

namespace {

constexpr uint32_t kCycleUs = cfg::CYCLE_MS * 1000u;
// The chip refuses to sleep for less than about 2.3 ms (ESP-IDF 4.4:
// 128 cycles of the slow clock plus its own overhead), so do not ask.
constexpr uint32_t kMinSleepUs = 2500;
// Each sleep costs roughly this long with the chip not asleep: about 1 ms
// of waiting for the crystal after the wake-up, plus the code around it.
// The report counts it as awake time. (From reading the ESP-IDF source,
// not from a measurement.)
constexpr uint32_t kSleepOverheadUs = 1500;

uint32_t s_cycleStartUs = 0;
uint32_t s_passes = 0;

// Since the last report.
uint32_t s_periodStartMs = 0;
bool s_periodStarted = false;
uint32_t s_totalUs = 0;
uint32_t s_asleepUs = 0;
uint32_t s_cycles = 0;
uint32_t s_screenCycles = 0;
uint32_t s_sleeps = 0;
uint32_t s_refused = 0;
uint32_t s_longestUs = 0;
bool s_screenOn = false;

uint8_t pct(uint64_t part, uint64_t whole) {
  if (whole == 0) return 0;
  const uint64_t p = (part * 100u + whole / 2u) / whole;
  return (uint8_t)(p > 100u ? 100u : p);
}

}  // namespace

void begin() {
  // The chip can be told to change a pin's set-up automatically when it
  // enters light sleep. Nothing in this firmware asks for that, but the
  // LEDs, the emitters and above all Vext (screen and receivers) must not
  // twitch, so switch the feature off for every pin in use.
  const uint8_t pins[] = {
      cfg::PIN_LED[0], cfg::PIN_LED[1], cfg::PIN_LED[2], cfg::PIN_LED[3],
      cfg::PIN_EMIT_DROP, cfg::PIN_EMIT_SHELF, cfg::PIN_BEAM_DROP, cfg::PIN_BEAM_SHELF,
      cfg::PIN_VEXT_CTRL, cfg::PIN_BUTTON, cfg::PIN_BOARD_LED,
      cfg::PIN_OLED_SDA, cfg::PIN_OLED_SCL, cfg::PIN_OLED_RST,
      cfg::PIN_LORA_SS, cfg::PIN_LORA_SCK, cfg::PIN_LORA_MOSI, cfg::PIN_LORA_RST,
  };
  for (uint8_t pin : pins) gpio_sleep_sel_dis((gpio_num_t)pin);
  // The pull-ups of the receiver inputs (GPIO32, 33) and of PRG (GPIO0)
  // belong to the chip's low-power domain: keep that powered in sleep.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
}

void beginCycle() { s_cycleStartUs = micros(); }

void noteScreen(bool on) { s_screenOn = on; }

void endCycle(bool maySleep) {
  uint32_t elapsed = micros() - s_cycleStartUs;
  ++s_passes;

  if (elapsed < kCycleUs) {
    const uint32_t rest = kCycleUs - elapsed;
    bool slept = false;
    // Once in a while stay awake instead, so that the idle task of this
    // core gets a turn (it tidies up after the system).
    const bool housekeeping = (s_passes % 128u) == 0;
    if (maySleep && cfg::LIGHT_SLEEP && !housekeeping && rest >= kMinSleepUs) {
      esp_sleep_enable_timer_wakeup(rest);
      if (esp_light_sleep_start() == ESP_OK) {
        slept = true;
        ++s_sleeps;
        s_asleepUs += rest > kSleepOverheadUs ? rest - kSleepOverheadUs : 0;
      } else {
        ++s_refused;
      }
    }
    if (!slept) {
      // Whole milliseconds with delay() (which lets other tasks run), the
      // remainder by waiting.
      uint32_t left = kCycleUs - (micros() - s_cycleStartUs);
      if (left <= kCycleUs && left >= 2000u) delay(left / 1000u - 1u);
      left = kCycleUs - (micros() - s_cycleStartUs);
      if (left <= kCycleUs && left > 0) delayMicroseconds(left);
    }
    elapsed = micros() - s_cycleStartUs;
  }

  s_totalUs += elapsed;
  ++s_cycles;
  if (s_screenOn) ++s_screenCycles;
  if (elapsed > s_longestUs) s_longestUs = elapsed;
}

bool report(uint32_t nowMs, Report& out) {
  if (!s_periodStarted) {
    s_periodStarted = true;
    s_periodStartMs = nowMs;
  }
  if (cfg::POWER_REPORT_MS == 0) return false;
  if ((uint32_t)(nowMs - s_periodStartMs) < cfg::POWER_REPORT_MS) return false;

  const uint32_t emitUs[2] = {pulse::takeEmitterUs(0), pulse::takeEmitterUs(1)};
  const uint32_t ledUs = pulse::takeLedUs();

  out.awakePct = pct(s_totalUs > s_asleepUs ? s_totalUs - s_asleepUs : 0, s_totalUs);
  out.emitterPct[0] = pct(emitUs[0], s_totalUs);
  out.emitterPct[1] = pct(emitUs[1], s_totalUs);
  out.ledPct = pct(ledUs, (uint64_t)s_totalUs * cfg::LED_COUNT);
  out.screenPct = pct(s_screenCycles, s_cycles);
  out.sleeps = s_sleeps;
  out.refused = s_refused;
  out.longestCycleMs = (s_longestUs + 500u) / 1000u;

  s_periodStartMs = nowMs;
  s_totalUs = 0;
  s_asleepUs = 0;
  s_cycles = 0;
  s_screenCycles = 0;
  s_sleeps = 0;
  s_refused = 0;
  s_longestUs = 0;
  return true;
}

}  // namespace power
