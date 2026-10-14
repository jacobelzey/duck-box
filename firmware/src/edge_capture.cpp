#include "edge_capture.h"

#include <Arduino.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

// Why not attachInterrupt()? In Arduino-ESP32 2.x its handlers are not
// registered as IRAM-safe, so they are held off whenever the flash cache is
// disabled - which is exactly what happens while the tallies are being
// saved a few seconds after an event. This file talks to the ESP-IDF GPIO
// driver directly and registers the interrupt service with
// ESP_INTR_FLAG_IRAM. In return the handler must obey the IRAM rules:
// the code is placed in RAM (IRAM_ATTR), it touches only data that lives
// in RAM (no const tables, no strings), and it calls nothing that might
// live in flash. It reads the pin straight from the GPIO input register
// and takes the time from esp_timer_get_time(), which is itself in IRAM.

namespace edgecap {

namespace {

constexpr uint32_t kBufferSize = 64;  // must be a power of two
constexpr uint32_t kBufferMask = kBufferSize - 1;

// What the interrupt stores: raw microsecond time (low 32 bits), so that
// the handler does not need a 64-bit division.
struct RawEdge {
  uint32_t us;
  uint8_t beam;
  uint8_t level;
};

// Everything below is written or read by the interrupt handler and must
// therefore be ordinary (non-const) data, which the linker keeps in RAM.
DRAM_ATTR RawEdge s_buffer[kBufferSize];
DRAM_ATTR volatile uint32_t s_head = 0;  // next slot the interrupt writes
DRAM_ATTR volatile uint32_t s_tail = 0;  // next slot the main loop reads
DRAM_ATTR volatile uint32_t s_overflow = 0;
DRAM_ATTR uint8_t s_pin[kBeamCount] = {0, 0};
DRAM_ATTR volatile uint8_t s_isrLevel[kBeamCount] = {0, 0};

uint8_t s_deliveredLevel[kBeamCount] = {0, 0};
bool s_started = false;
bool s_flashSafe = false;

void IRAM_ATTR beamIsr(void* arg) {
  const uint32_t beam = (uint32_t)(uintptr_t)arg;
  const uint32_t us = (uint32_t)esp_timer_get_time();
  // Pin level straight from the input registers (GPIO 0-31 / 32-39).
  const uint32_t pin = s_pin[beam];
  const uint8_t level = (pin < 32)
      ? (uint8_t)((REG_READ(GPIO_IN_REG) >> pin) & 1u)
      : (uint8_t)((REG_READ(GPIO_IN1_REG) >> (pin - 32)) & 1u);

  // Any-edge interrupts also fire for pulses so short that the pin is back
  // where it started by the time we look. Those are far below the 2 ms
  // response of the sensor and carry no information.
  if (level == s_isrLevel[beam]) return;
  s_isrLevel[beam] = level;

  const uint32_t head = s_head;
  const uint32_t next = (head + 1) & kBufferMask;
  if (next == s_tail) {
    // Buffer full: drop the edge. The main loop notices the mismatch
    // between the pin and the last delivered level and resyncs.
    s_overflow = s_overflow + 1;
    return;
  }
  s_buffer[head].us = us;
  s_buffer[head].beam = (uint8_t)beam;
  s_buffer[head].level = level;
  s_head = next;
}

}  // namespace

bool begin(uint8_t pinBeam0, uint8_t pinBeam1) {
  end();

  s_pin[0] = pinBeam0;
  s_pin[1] = pinBeam1;
  s_head = 0;
  s_tail = 0;

  for (uint8_t b = 0; b < kBeamCount; ++b) {
    pinMode(s_pin[b], INPUT_PULLUP);
  }
  delay(2);
  for (uint8_t b = 0; b < kBeamCount; ++b) {
    const uint8_t level = (uint8_t)digitalRead(s_pin[b]);
    s_isrLevel[b] = level;
    s_deliveredLevel[b] = level;
  }

  // One shared GPIO interrupt service for the whole program. If something
  // else installed it first (nothing in this firmware does) we can still
  // add our handlers, but lose the IRAM guarantee.
  const esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  if (err == ESP_OK) {
    s_flashSafe = true;
  } else if (err == ESP_ERR_INVALID_STATE) {
    s_flashSafe = false;
  } else {
    return false;
  }

  for (uint8_t b = 0; b < kBeamCount; ++b) {
    const gpio_num_t pin = (gpio_num_t)s_pin[b];
    if (gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE) != ESP_OK) return false;
    if (gpio_isr_handler_add(pin, beamIsr, (void*)(uintptr_t)b) != ESP_OK) return false;
  }
  s_started = true;

  // A level change between the digitalRead above and the handler being
  // added would have been missed; catch it here.
  for (uint8_t b = 0; b < kBeamCount; ++b) {
    const uint8_t level = (uint8_t)digitalRead(s_pin[b]);
    if (level != s_isrLevel[b]) {
      s_isrLevel[b] = level;
      s_deliveredLevel[b] = level;
    }
  }
  return true;
}

void end() {
  if (!s_started) return;
  for (uint8_t b = 0; b < kBeamCount; ++b) {
    gpio_isr_handler_remove((gpio_num_t)s_pin[b]);
    gpio_set_intr_type((gpio_num_t)s_pin[b], GPIO_INTR_DISABLE);
  }
  s_started = false;
}

bool started() { return s_started; }

bool pop(Edge& out) {
  const uint32_t tail = s_tail;
  if (tail == s_head) return false;
  const RawEdge raw = s_buffer[tail];
  s_tail = (tail + 1) & kBufferMask;

  // Convert "microseconds, 32 bit" into the millis() time base by working
  // out how long ago the edge happened.
  const uint32_t ageUs = (uint32_t)esp_timer_get_time() - raw.us;
  out.ms = (uint32_t)millis() - ageUs / 1000u;
  out.beam = raw.beam;
  out.level = raw.level;
  s_deliveredLevel[raw.beam] = raw.level;
  return true;
}

uint8_t readLevel(uint8_t beam) {
  return (uint8_t)digitalRead(s_pin[beam]);
}

uint8_t deliveredLevel(uint8_t beam) { return s_deliveredLevel[beam]; }

void resync(uint8_t beam, uint8_t level) {
  s_isrLevel[beam] = level;
  s_deliveredLevel[beam] = level;
}

uint32_t overflowCount() { return s_overflow; }

bool flashSafe() { return s_flashSafe; }

}  // namespace edgecap
