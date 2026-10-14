// Native unit tests for the hardware-free parts of POWER_SAVE:
// how a strobed beam is judged (beam_sampler) and when the idle screen is
// lit (screen_policy).
#include "beam_sampler.h"
#include "config.h"
#include "mini_test.h"
#include "screen_policy.h"

using duck::BeamSampler;
using duck::SamplerConfig;
using duck::ScreenPolicy;
using duck::ScreenState;
using duck::screenState;

namespace {

const uint32_t kNever = BeamSampler::kNever;

// One look as the hardware does it (pulse::window) for a receiver that
// answers `responseUs` after its emitter lights up (kNever = beam blocked):
// light that is there at the first read is reported as the planned settle
// time exactly; light that turns up while waiting, as the time it turned
// up; no light by the end of the look, as kNever.
bool look(BeamSampler& s, uint32_t responseUs) {
  const BeamSampler::Look plan = s.plan();
  uint32_t seen = kNever;
  if (responseUs != kNever) {
    if (responseUs <= plan.settleUs) seen = plan.settleUs;
    else if (responseUs <= plan.giveUpUs) seen = responseUs;
  }
  return s.report(seen);
}

SamplerConfig testConfig() {
  SamplerConfig c;
  c.settleUs = 1500;
  c.settleMinUs = 300;
  c.settleMaxUs = 3000;
  c.confirmUs = 5000;
  c.recheckCycles = 50;
  c.lateLooksToRaise = 3;
  c.relaxAfterCycles = 6000;
  return c;
}

}  // namespace

TEST(sampler_settle_is_twice_the_measured_response_plus_margin) {
  BeamSampler s(testConfig());
  s.reset(250);
  CHECK_EQ(s.settleUs(), 600u);
  CHECK(!s.blocked());
  s.reset(40);                       // very quick receiver: lower limit
  CHECK_EQ(s.settleUs(), 300u);
  s.reset(2500);                     // very slow one: upper limit
  CHECK_EQ(s.settleUs(), 3000u);
  s.reset(1200);
  CHECK_EQ(s.settleUs(), 2500u);
}

TEST(sampler_no_light_at_start_means_blocked) {
  BeamSampler s(testConfig());
  s.reset(kNever);
  CHECK(s.blocked());
  CHECK_EQ(s.settleUs(), 1500u);
}

TEST(sampler_clear_beam_stays_clear_and_is_never_called_late) {
  BeamSampler s(testConfig());
  s.reset(250);
  for (int i = 0; i < 20000; ++i) {
    const BeamSampler::Look plan = s.plan();
    CHECK_EQ(plan.settleUs, 600u);
    CHECK(!look(s, 250));
  }
  CHECK(!s.blocked());
  CHECK_EQ(s.lateCount(), 0u);
  CHECK_EQ(s.raiseCount(), 0u);
  CHECK_EQ(s.settleUs(), 600u);
}

TEST(sampler_blocked_needs_the_full_confirm_time_after_the_settle_time) {
  BeamSampler s(testConfig());
  s.reset(250);
  BeamSampler::Look plan = s.plan();
  CHECK_EQ(plan.settleUs, 600u);
  CHECK_EQ(plan.giveUpUs, 5600u);    // dark from 0.6 ms to 5.6 ms = 5 ms of darkness
  CHECK(look(s, kNever));            // dark all that time: blocked, a change
  CHECK(s.blocked());
  CHECK(!look(s, kNever));           // still blocked: no further change
  CHECK(look(s, 250));               // light again: clear, a change
  CHECK(!s.blocked());
  // The same with the longest settle time: still a full 5 ms of darkness.
  s.reset(2000);
  plan = s.plan();
  CHECK_EQ(plan.settleUs, 3000u);
  CHECK_EQ(plan.giveUpUs, 8000u);
}

TEST(sampler_one_or_two_late_answers_change_nothing) {
  // Something leaving the beam in the middle of a look gives light "late".
  BeamSampler s(testConfig());
  s.reset(250);
  CHECK(!look(s, 1400));
  CHECK(!look(s, 4000));
  CHECK(!s.blocked());
  CHECK_EQ(s.lateCount(), 2u);
  CHECK_EQ(s.settleUs(), 600u);      // not raised
  CHECK(!look(s, 250));              // in time again: the run is over
  CHECK(!look(s, 1400));
  CHECK(!look(s, 1400));
  CHECK_EQ(s.settleUs(), 600u);
  CHECK_EQ(s.raiseCount(), 0u);
}

TEST(sampler_three_late_answers_in_a_row_raise_the_settle_time) {
  BeamSampler s(testConfig());
  s.reset(250);                      // settle 600
  CHECK(!look(s, 1100));
  CHECK(!look(s, 1300));
  CHECK(!look(s, 1200));
  CHECK(!s.blocked());               // never mistaken for a duck
  CHECK_EQ(s.raiseCount(), 1u);
  CHECK_EQ(s.settleUs(), 2700u);     // 2 x the slowest (1300) + 100
  for (int i = 0; i < 100; ++i) CHECK(!look(s, 1300));
  CHECK_EQ(s.lateCount(), 3u);       // now in time
}

TEST(sampler_dark_looks_break_a_run_of_late_answers) {
  // A tumbling duck: dark, late light, dark, late light... is not a slow
  // receiver.
  BeamSampler s(testConfig());
  s.reset(250);
  for (int i = 0; i < 20; ++i) {
    look(s, kNever);
    look(s, 2000);
    look(s, 2000);
  }
  CHECK_EQ(s.raiseCount(), 0u);
  CHECK_EQ(s.settleUs(), 600u);
}

TEST(sampler_raised_settle_time_relaxes_after_a_long_quiet_run) {
  BeamSampler s(testConfig());
  s.reset(250);
  for (int i = 0; i < 3; ++i) look(s, 1300);
  CHECK_EQ(s.settleUs(), 2700u);
  // The receiver is quick again (it was a passing thing).
  for (int i = 0; i < 5999; ++i) look(s, 250);
  CHECK_EQ(s.settleUs(), 2700u);
  look(s, 250);
  CHECK_EQ(s.settleUs(), 1650u);     // half-way back to 600
  for (int i = 0; i < 6000; ++i) look(s, 250);
  CHECK_EQ(s.settleUs(), 1125u);
  for (int i = 0; i < 6 * 6000; ++i) look(s, 250);
  CHECK_EQ(s.settleUs(), 600u);      // and never below what was measured
  CHECK_EQ(s.raiseCount(), 1u);
  CHECK(!s.blocked());
}

TEST(sampler_relaxing_a_truly_slow_receiver_backs_off) {
  // The receiver really does need 1300 us now. Every attempt to relax is
  // undone after three looks, and the attempts get rarer.
  BeamSampler s(testConfig());
  s.reset(250);
  uint32_t lastRaises = 0;
  int looksAtLastRaise = 0;
  int gaps[6];
  int n = 0;
  for (int i = 0; i < 2000000 && n < 6; ++i) {
    CHECK(!look(s, 1300));           // and never a duck
    if (s.raiseCount() != lastRaises) {
      gaps[n++] = i - looksAtLastRaise;
      looksAtLastRaise = i;
      lastRaises = s.raiseCount();
    }
  }
  CHECK_EQ(n, 6);
  for (int i = 2; i < 6; ++i) CHECK(gaps[i] > gaps[i - 1]);   // 6000, 12000, 24000...
  CHECK(s.settleUs() >= 1300u);
}

TEST(sampler_looks_are_short_while_blocked_with_a_long_one_now_and_then) {
  BeamSampler s(testConfig());
  s.reset(250);
  CHECK(look(s, kNever));
  int longLooks = 0;
  for (int i = 0; i < 500; ++i) {
    const BeamSampler::Look plan = s.plan();
    if (plan.giveUpUs > plan.settleUs) {
      ++longLooks;
      CHECK_EQ(plan.giveUpUs, 5600u);
    }
    CHECK(!s.report(kNever));
  }
  CHECK_EQ(longLooks, 10);           // one in 50
}

TEST(sampler_recovers_when_the_receiver_is_slower_than_the_settle_time) {
  // Blocked at start-up, so nothing was measured; the receiver then turns
  // out to need 2.5 ms, more than the 1.5 ms starting settle time.
  BeamSampler s(testConfig());
  s.reset(kNever);
  int looks = 0;
  while (s.blocked() && looks < 200) {
    look(s, 2500);
    ++looks;
  }
  CHECK(!s.blocked());
  CHECK(looks <= 50);                // found by the first long look
  for (int i = 0; i < 100; ++i) CHECK(!look(s, 2500));   // and it stays clear
  CHECK_EQ(s.settleUs(), 3000u);     // 2 x 2500 + 100, capped
  CHECK_EQ(s.raiseCount(), 1u);
}

TEST(sampler_config_is_made_consistent) {
  SamplerConfig c;
  c.settleUs = 100;
  c.settleMinUs = 300;
  c.settleMaxUs = 3000;
  c.confirmUs = 5000;
  c.recheckCycles = 0;
  c.lateLooksToRaise = 0;
  c.relaxAfterCycles = 0;
  BeamSampler s(c);
  CHECK_EQ(s.settleUs(), 300u);
  s.reset(kNever);
  s.plan();                          // must not divide by zero
  CHECK(s.blocked());
  s.report(2000);                    // one late look raises at once
  CHECK(!s.blocked());
}

// ------------------------------------------------------------ screen policy

TEST(screen_stays_on_after_activity_then_peeks) {
  ScreenPolicy p;
  p.stayOnMs = 30000;
  p.peekEveryMs = 20000;
  p.peekOnMs = 4000;
  CHECK(screenState(p, 0).on);
  CHECK(screenState(p, 29999).on);
  CHECK(!screenState(p, 29999).peeking);
  CHECK(!screenState(p, 30000).on);          // dark first
  CHECK(!screenState(p, 45999).on);
  ScreenState s = screenState(p, 46000);     // 4 s peek at the end of the period
  CHECK(s.on);
  CHECK(s.peeking);
  CHECK_EQ(s.peek, 0u);
  CHECK(screenState(p, 49999).on);
  CHECK(!screenState(p, 50000).on);
  s = screenState(p, 66000);
  CHECK(s.on);
  CHECK_EQ(s.peek, 1u);
  // Share of the time lit, long after the last activity: 4 s in 20 s.
  uint32_t lit = 0;
  for (uint32_t t = 1000000; t < 1200000; t += 100) lit += screenState(p, t).on;
  CHECK_EQ(lit, 400u);
}

TEST(screen_policy_special_values) {
  ScreenPolicy p;
  p.stayOnMs = 1000;
  p.peekEveryMs = 0;                 // never dark
  p.peekOnMs = 4000;
  CHECK(screenState(p, 999999).on);
  CHECK(!screenState(p, 999999).peeking);
  p.peekEveryMs = 20000;
  p.peekOnMs = 0;                    // dark until the next event
  CHECK(screenState(p, 500).on);
  CHECK(!screenState(p, 1000).on);
  CHECK(!screenState(p, 999999).on);
  p.peekOnMs = 20000;                // peeks as long as the period: always on
  CHECK(screenState(p, 999999).on);
}

// ------------------------------------------------- firmware configuration

TEST(power_save_config_is_sane) {
  CHECK(cfg::CYCLE_MS >= 5 && cfg::CYCLE_MS <= 20);   // 50 Hz or slower would flicker
  CHECK(cfg::EMIT_SETTLE_MIN_US <= cfg::EMIT_SETTLE_US);
  CHECK(cfg::EMIT_SETTLE_US <= cfg::EMIT_SETTLE_MAX_US);
  // The longest look must fit into one cycle, or a blocked beam slows the loop.
  CHECK(cfg::EMIT_SETTLE_MAX_US + cfg::EMIT_CONFIRM_US < cfg::CYCLE_MS * 1000u);
  // Nothing shorter than the drop debounce time may count as a duck.
  CHECK(cfg::EMIT_CONFIRM_US >= cfg::DROP_DEBOUNCE_MS * 1000u);
  CHECK(cfg::EMIT_RECHECK_CYCLES >= 10);
  CHECK(cfg::EMIT_LATE_LOOKS_TO_RAISE >= 2);
  // The shortest block that is certain to be seen (two cycles, in case one
  // overruns, plus the longest look) must be shorter than the quickest
  // duck (assumed to need at least 50 ms to pass the beam) and than the
  // shortest hand visit that counts.
  const uint32_t certainMs =
      2 * cfg::CYCLE_MS + (cfg::EMIT_SETTLE_MAX_US + cfg::EMIT_CONFIRM_US) / 1000u;
  CHECK(certainMs < 50);
  CHECK(certainMs < cfg::SHELF_MIN_BLOCK_MS);
  CHECK(cfg::DROP_DEBOUNCE_MS <= cfg::CYCLE_MS);      // one blocked look is believed
  // LED steps are taken every LED_STEP_MS, which must be whole cycles.
  CHECK_EQ(cfg::LED_STEP_MS % cfg::CYCLE_MS, 0u);
  CHECK(cfg::SCREEN_PEEK_ON_MS <= cfg::SCREEN_PEEK_EVERY_MS || cfg::SCREEN_PEEK_EVERY_MS == 0);
}

TEST(emitter_pins_do_not_clash_with_anything_wired) {
  const int used[] = {cfg::PIN_BEAM_DROP, cfg::PIN_BEAM_SHELF, cfg::PIN_LED[0], cfg::PIN_LED[1],
                      cfg::PIN_LED[2], cfg::PIN_LED[3], cfg::PIN_OLED_SDA, cfg::PIN_OLED_SCL,
                      cfg::PIN_OLED_RST, cfg::PIN_VEXT_CTRL, cfg::PIN_BOARD_LED, cfg::PIN_BUTTON,
                      cfg::PIN_LORA_SS, cfg::PIN_LORA_SCK, cfg::PIN_LORA_MOSI, cfg::PIN_LORA_MISO,
                      cfg::PIN_LORA_RST, cfg::PIN_LORA_DIO0, 1, 3};
  for (int pin : used) {
    CHECK(pin != cfg::PIN_EMIT_DROP);
    CHECK(pin != cfg::PIN_EMIT_SHELF);
  }
  CHECK(cfg::PIN_EMIT_DROP < 34 && cfg::PIN_EMIT_SHELF < 34);   // 34-39 are inputs only
  // The wiring table was written from these numbers: both emitters on GPIO12.
  CHECK_EQ((int)cfg::PIN_EMIT_DROP, 12);
  CHECK_EQ((int)cfg::PIN_EMIT_SHELF, 12);
}
