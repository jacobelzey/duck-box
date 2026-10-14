// Native unit tests for the small hardware-free helpers:
// button gestures, candle flicker and the save timer.
#include <cmath>
#include <vector>

#include "button_decoder.h"
#include "candle_flicker.h"
#include "mini_test.h"
#include "save_timer.h"

using duck::ButtonDecoder;
using duck::ButtonEvent;

namespace {

// Feeds a ButtonDecoder every 2 ms, like the main loop, and records events.
struct ButtonSim {
  ButtonDecoder dec;
  uint32_t now;
  std::vector<ButtonEvent> events;

  explicit ButtonSim(uint32_t start = 1000) : now(start) { dec.reset(false, now); }

  void hold(bool pressed, uint32_t ms) {
    for (uint32_t i = 0; i < ms; i += 2) {
      now += 2;
      const ButtonEvent e = dec.update(pressed, now);
      if (e != ButtonEvent::None) events.push_back(e);
    }
  }
  void press(uint32_t ms) { hold(true, ms); }
  void release(uint32_t ms) { hold(false, ms); }
};

bool same(const std::vector<ButtonEvent>& got, std::vector<ButtonEvent> want) {
  return got == want;
}

}  // namespace

TEST(button_single_press) {
  ButtonSim b;
  b.press(120);
  b.release(300);
  CHECK(b.events.empty());            // still waiting for a possible second press
  b.release(300);
  CHECK(same(b.events, {ButtonEvent::SinglePress}));
  b.release(2000);
  CHECK_EQ(b.events.size(), 1u);
}

TEST(button_double_press) {
  ButtonSim b;
  b.press(120);
  b.release(150);
  b.press(120);
  CHECK(same(b.events, {ButtonEvent::DoublePress}));
  b.release(2000);
  CHECK_EQ(b.events.size(), 1u);      // no stray single press afterwards
}

TEST(button_two_slow_presses_are_two_singles) {
  ButtonSim b;
  b.press(120);
  b.release(800);
  b.press(120);
  b.release(800);
  CHECK(same(b.events, {ButtonEvent::SinglePress, ButtonEvent::SinglePress}));
}

TEST(button_hold_completes_after_five_seconds) {
  ButtonSim b;
  b.press(600);
  CHECK(b.events.empty());
  b.press(200);
  CHECK(same(b.events, {ButtonEvent::HoldStart}));
  CHECK(b.dec.holding());
  b.press(4000);
  CHECK_EQ(b.events.size(), 1u);      // 4.8 s: not yet
  b.press(400);
  CHECK(same(b.events, {ButtonEvent::HoldStart, ButtonEvent::HoldComplete}));
  b.press(3000);                      // keeps holding: nothing repeats
  b.release(2000);
  CHECK_EQ(b.events.size(), 2u);
}

TEST(button_hold_released_early_cancels) {
  ButtonSim b;
  b.press(3000);
  b.release(1000);
  CHECK(same(b.events, {ButtonEvent::HoldStart, ButtonEvent::HoldCancel}));
  b.release(2000);
  CHECK_EQ(b.events.size(), 2u);      // a cancelled hold is not a press
}

TEST(button_contact_bounce_is_one_press) {
  ButtonSim b;
  for (int i = 0; i < 5; ++i) {       // 20 ms of chatter going down
    b.press(2);
    b.release(2);
  }
  b.press(150);
  for (int i = 0; i < 5; ++i) {       // and going up
    b.release(2);
    b.press(2);
  }
  b.release(1000);
  CHECK(same(b.events, {ButtonEvent::SinglePress}));
}

TEST(button_spike_shorter_than_debounce_is_ignored) {
  ButtonSim b;
  b.press(10);
  b.release(2000);
  CHECK(b.events.empty());
}

TEST(button_already_down_at_reset_is_ignored_until_released) {
  ButtonDecoder dec;
  uint32_t now = 5000;
  dec.reset(true, now);
  std::vector<ButtonEvent> events;
  auto feed = [&](bool pressed, uint32_t ms) {
    for (uint32_t i = 0; i < ms; i += 2) {
      now += 2;
      const ButtonEvent e = dec.update(pressed, now);
      if (e != ButtonEvent::None) events.push_back(e);
    }
  };
  feed(true, 8000);                   // would otherwise be a full reset hold
  feed(false, 1000);
  CHECK(events.empty());
  feed(true, 100);
  feed(false, 1000);
  CHECK(same(events, {ButtonEvent::SinglePress}));
}

TEST(button_works_across_clock_rollover) {
  ButtonSim b(0xFFFFFFFFu - 2000u);
  b.release(100);
  b.press(6000);                      // hold right through the wrap
  b.release(500);
  CHECK(same(b.events, {ButtonEvent::HoldStart, ButtonEvent::HoldComplete}));
}

TEST(button_held_time_is_reported) {
  ButtonSim b;
  b.press(2000);
  const uint32_t held = b.dec.heldMs(b.now);
  CHECK(held >= 1950 && held <= 2000);
  b.release(100);
  CHECK_EQ(b.dec.heldMs(b.now), 0u);
}

// ------------------------------------------------------------------ candle

TEST(candle_levels_stay_in_range_and_average_near_target) {
  duck::CandleConfig c;
  c.average = 0.30f;
  duck::CandleFlicker f(c);
  f.seed(12345);
  for (int led = 0; led < duck::CandleFlicker::kLeds; ++led) {
    CHECK(f.level(led) >= 0.0f && f.level(led) <= 1.0f);
  }
  const int steps = 50 * 600;         // ten minutes at 50 steps per second
  double sum[4] = {0, 0, 0, 0};
  float lo = 1.0f, hi = 0.0f;
  for (int i = 0; i < steps; ++i) {
    f.step();
    for (int led = 0; led < 4; ++led) {
      const float v = f.level(led);
      sum[led] += v;
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
  }
  CHECK(lo >= 0.0f);
  CHECK(hi <= 1.0f);
  for (int led = 0; led < 4; ++led) {
    const double mean = sum[led] / steps;
    CHECK(mean > 0.30 * 0.88 && mean < 0.30 * 1.12);
  }
  CHECK(hi > 0.30f * 1.2f);           // it really flickers...
  CHECK(lo < 0.30f * 0.8f);
  CHECK(lo > 0.02f);                  // ...but never goes dark
}

TEST(candle_is_smooth_not_a_square_blink) {
  duck::CandleFlicker f;
  f.seed(777);
  float prev[4];
  for (int led = 0; led < 4; ++led) prev[led] = f.level(led);
  float biggestJump = 0.0f;
  double variation = 0.0;
  const int steps = 5000;
  for (int i = 0; i < steps; ++i) {
    f.step();
    for (int led = 0; led < 4; ++led) {
      const float v = f.level(led);
      const float jump = std::fabs(v - prev[led]);
      biggestJump = std::max(biggestJump, jump);
      variation += jump;
      prev[led] = v;
    }
  }
  const float average = duck::CandleConfig().average;
  CHECK(biggestJump < 0.35f * average);            // no hard steps
  CHECK(variation / (steps * 4) > 0.01 * average); // but always moving
}

TEST(candle_leds_are_not_identical) {
  duck::CandleFlicker f;
  f.seed(42);
  int different = 0;
  for (int i = 0; i < 1000; ++i) {
    f.step();
    if (std::fabs(f.level(0) - f.level(1)) > 0.005f) ++different;
  }
  CHECK(different > 800);
}

TEST(candle_flare_rises_then_fades) {
  duck::CandleFlicker f;
  f.seed(9);
  for (int i = 0; i < 200; ++i) f.step();
  const float before = f.level(0);
  f.flare();
  CHECK(f.level(0) > 0.95f);
  CHECK(f.level(0) > before);
  for (int i = 0; i < 100; ++i) f.step();          // two seconds later
  CHECK(f.level(0) < 0.7f);
  for (int led = 0; led < 4; ++led) CHECK(f.level(led) <= 1.0f);
}

TEST(candle_out_of_range_led_index_is_dark) {
  duck::CandleFlicker f;
  CHECK_EQ(f.level(-1), 0.0f);
  CHECK_EQ(f.level(4), 0.0f);
}

// -------------------------------------------------------------- save timer

TEST(save_timer_waits_for_quiet_after_last_change) {
  duck::SaveTimer t(5000, 60000);
  CHECK(!t.due(0));
  t.markDirty(1000);
  CHECK(!t.due(5999));
  t.markDirty(4000);                  // another event: the wait restarts
  CHECK(!t.due(8999));
  CHECK(t.due(9000));
  t.markSaved();
  CHECK(!t.due(20000));
}

TEST(save_timer_cannot_be_postponed_for_ever) {
  duck::SaveTimer t(5000, 60000);
  uint32_t now = 1000;
  t.markDirty(now);
  for (int i = 0; i < 30; ++i) {      // an event every 2 s for a minute
    CHECK(!t.due(now));
    now += 2000;
    t.markDirty(now);
  }
  CHECK(t.due(now));
}

TEST(save_timer_works_across_clock_rollover) {
  duck::SaveTimer t(5000, 60000);
  t.markDirty(0xFFFFFFFFu - 1000u);
  CHECK(!t.due(0xFFFFFFFFu));
  CHECK(!t.due(3000));
  CHECK(t.due(4000));
}
