#include "pulse.h"

#include <Arduino.h>

#include "driver/gpio.h"

#include "config.h"

namespace pulse {

namespace {

const uint8_t kEmitPin[kBeams] = {cfg::PIN_EMIT_DROP, cfg::PIN_EMIT_SHELF};
const uint8_t kRxPin[kBeams] = {cfg::PIN_BEAM_DROP, cfg::PIN_BEAM_SHELF};

constexpr uint32_t kCycleUs = cfg::CYCLE_MS * 1000u;
// An LED asked to be lit for at least this long is simply left on.
constexpr uint32_t kSteadyUs = kCycleUs - kCycleUs / 32u;
// While waiting for a slow receiver, look this often.
constexpr uint32_t kPollUs = 100;

uint32_t s_ledOnUs[cfg::LED_COUNT] = {0, 0, 0, 0};
bool s_ledSteady[cfg::LED_COUNT] = {false, false, false, false};

uint32_t s_emitUs[kBeams] = {0, 0};
uint32_t s_ledUs = 0;

}  // namespace

void begin() {
  for (uint8_t i = 0; i < cfg::LED_COUNT; ++i) {
    pinMode(cfg::PIN_LED[i], OUTPUT);
    digitalWrite(cfg::PIN_LED[i], LOW);
    s_ledSteady[i] = false;
  }
  for (uint8_t b = 0; b < kBeams; ++b) {
    pinMode(kRxPin[b], INPUT_PULLUP);
    pinMode(kEmitPin[b], OUTPUT);
    digitalWrite(kEmitPin[b], LOW);
    // The emitters draw about 10 mA each, and by default both hang on one
    // pin. The strongest pad setting keeps the voltage they get (and with
    // it the range of the beams) as high as possible.
    gpio_set_drive_capability((gpio_num_t)kEmitPin[b], GPIO_DRIVE_CAP_3);
  }
}

void setLedOnUs(uint8_t led, uint32_t onUs) {
  if (led >= cfg::LED_COUNT) return;
  s_ledOnUs[led] = onUs;
}

void ledsOff() {
  for (uint8_t i = 0; i < cfg::LED_COUNT; ++i) {
    s_ledOnUs[i] = 0;
    s_ledSteady[i] = false;
    digitalWrite(cfg::PIN_LED[i], LOW);
  }
}

// True if another beam that is still being looked at is lit by the same
// emitter pin as `beam` (by default both emitters hang on one pin).
static bool pinStillNeeded(uint8_t beam, const bool pending[kBeams]) {
  for (uint8_t other = 0; other < kBeams; ++other) {
    if (other != beam && pending[other] && kEmitPin[other] == kEmitPin[beam]) return true;
  }
  return false;
}

void window(Look looks[kBeams]) {
  // ---- 1. the beams, with the candle dark ------------------------------------
  // (LED light that finds its way to a receiver must not be taken for the
  // beam. Only an LED at full brightness - the short flare after an event -
  // stays lit through this part.)

  // What the receivers show with the emitters dark (stray light check).
  for (uint8_t b = 0; b < kBeams; ++b) {
    looks[b].levelBefore = (uint8_t)digitalRead(kRxPin[b]);
  }

  bool pending[kBeams];
  bool waited[kBeams];            // a read after the settle time came back dark
  bool any = false;
  for (uint8_t b = 0; b < kBeams; ++b) {
    pending[b] = looks[b].wanted;
    waited[b] = false;
    looks[b].seenAfterUs = kNever;
    looks[b].level = looks[b].darkLevel;
    any = any || pending[b];
  }

  if (any) {
    const uint32_t t0 = micros();
    for (uint8_t b = 0; b < kBeams; ++b) {
      if (pending[b]) digitalWrite(kEmitPin[b], HIGH);
    }
    for (;;) {
      const uint32_t el = micros() - t0;
      uint32_t nextDue = kNever;
      for (uint8_t b = 0; b < kBeams; ++b) {
        if (!pending[b]) continue;
        Look& look = looks[b];
        if (el < look.settleUs) {
          if (look.settleUs < nextDue) nextDue = look.settleUs;
          continue;
        }
        look.level = (uint8_t)digitalRead(kRxPin[b]);
        const bool light = look.level != look.darkLevel;
        if (!light && el < look.giveUpUs) {
          // Dark so far: look again shortly.
          waited[b] = true;
          uint32_t again = el + kPollUs;
          if (again > look.giveUpUs) again = look.giveUpUs;
          if (again < nextDue) nextDue = again;
          continue;
        }
        // Decided. Light at the very first read counts as "in time" however
        // many microseconds this code took to get round to reading.
        if (light) look.seenAfterUs = waited[b] ? el : look.settleUs;
        pending[b] = false;
        if (!pinStillNeeded(b, pending)) {
          digitalWrite(kEmitPin[b], LOW);
          for (uint8_t other = 0; other < kBeams; ++other) {
            if (kEmitPin[other] == kEmitPin[b]) s_emitUs[other] += el;
          }
        }
      }
      if (nextDue == kNever) break;
      if (nextDue > el) delayMicroseconds(nextDue - el);
    }
  }

  // ---- 2. the candle: each LED lit for its share of the cycle ----------------
  const uint32_t t1 = micros();
  bool ledLit[cfg::LED_COUNT];
  for (uint8_t i = 0; i < cfg::LED_COUNT; ++i) {
    const uint32_t on = s_ledOnUs[i];
    if (on >= kSteadyUs) {
      if (!s_ledSteady[i]) {
        digitalWrite(cfg::PIN_LED[i], HIGH);
        s_ledSteady[i] = true;
      }
      ledLit[i] = false;          // stays on; nothing to switch off
      s_ledUs += kCycleUs;
    } else {
      s_ledSteady[i] = false;
      digitalWrite(cfg::PIN_LED[i], on > 0 ? HIGH : LOW);
      ledLit[i] = on > 0;
    }
  }
  for (;;) {
    const uint32_t el = micros() - t1;
    uint32_t nextDue = kNever;
    for (uint8_t i = 0; i < cfg::LED_COUNT; ++i) {
      if (!ledLit[i]) continue;
      if (el >= s_ledOnUs[i]) {
        digitalWrite(cfg::PIN_LED[i], LOW);
        ledLit[i] = false;
        s_ledUs += el;
      } else if (s_ledOnUs[i] < nextDue) {
        nextDue = s_ledOnUs[i];
      }
    }
    if (nextDue == kNever) break;
    if (nextDue > el) delayMicroseconds(nextDue - el);
  }
}

uint32_t measureResponse(uint8_t beam, uint8_t darkLevel, uint32_t timeoutUs) {
  if (beam >= kBeams) return kNever;
  uint32_t result = kNever;
  // A receiver that shows light before its emitter is lit cannot be
  // measured: that is stray light, or an emitter that is lit all the time
  // because it is wired to Vext.
  // (Give it a few milliseconds to let go of the light of an earlier
  // pulse first.)
  const uint32_t waitStart = micros();
  while ((uint8_t)digitalRead(kRxPin[beam]) != darkLevel) {
    if ((uint32_t)(micros() - waitStart) >= 5000u) return kNever;
    delayMicroseconds(50);
  }
  const uint32_t t0 = micros();
  digitalWrite(kEmitPin[beam], HIGH);
  for (;;) {
    const uint32_t el = micros() - t0;
    if ((uint8_t)digitalRead(kRxPin[beam]) != darkLevel) {
      result = el;
      break;
    }
    if (el >= timeoutUs) break;
    delayMicroseconds(20);
  }
  digitalWrite(kEmitPin[beam], LOW);
  const uint32_t lit = micros() - t0;
  for (uint8_t other = 0; other < kBeams; ++other) {
    if (kEmitPin[other] == kEmitPin[beam]) s_emitUs[other] += lit;
  }
  return result;
}

uint8_t readLevel(uint8_t beam) {
  return beam < kBeams ? (uint8_t)digitalRead(kRxPin[beam]) : 0;
}

void emittersSteady(bool on) {
  for (uint8_t b = 0; b < kBeams; ++b) {
    pinMode(kEmitPin[b], OUTPUT);
    gpio_set_drive_capability((gpio_num_t)kEmitPin[b], GPIO_DRIVE_CAP_3);
    digitalWrite(kEmitPin[b], on ? HIGH : LOW);
  }
}

uint32_t takeEmitterUs(uint8_t beam) {
  if (beam >= kBeams) return 0;
  const uint32_t v = s_emitUs[beam];
  s_emitUs[beam] = 0;
  return v;
}

uint32_t takeLedUs() {
  const uint32_t v = s_ledUs;
  s_ledUs = 0;
  return v;
}

}  // namespace pulse
