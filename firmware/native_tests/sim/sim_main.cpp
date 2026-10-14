// Whole-firmware simulation: the real setup() and loop() from src/main.cpp
// run against a pretend board (sim_board.*) while scripted "hands" break
// the beams and press PRG. Each scenario checks the serial log, the LEDs,
// the display state and what ends up in flash, and saves pictures of the
// screen to .native_build/screens/ so the layouts can be looked at.
//
// Every scenario phase runs in its own process (fork), because the firmware
// keeps its state in globals, exactly as it does on the real chip. A new
// phase is a power cycle: only the NVS file carries over.
//
// Run with:  sh native_tests/run_sim.sh      (Linux, macOS or WSL)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include <Arduino.h>
#include <U8g2lib.h>

#include "config.h"
#include "phrases.h"
#include "sim_board.h"
#include "ui.h"
#include "../mini_test.h"

using sim::board;

namespace {

const int kDrop = cfg::PIN_BEAM_DROP;
const int kShelf = cfg::PIN_BEAM_SHELF;
const int kButton = cfg::PIN_BUTTON;
const char* const kScreenDir =
    cfg::POWER_SAVE ? ".native_build/screens/" : ".native_build/screens_power_save_off/";

// ------------------------------------------------------------ the "hands"

void run(double ms) {
  const uint64_t end = board().nowUs + (uint64_t)(ms * 1000.0);
  while (board().nowUs < end) loop();
}

const bool kPowerSave = cfg::POWER_SAVE;
// The idle screen goes dark between peeks (off in the shipped setting: always on).
const bool kPeeks = cfg::SCREEN_PEEK_EVERY_MS != 0;

int beamOf(int pin) { return pin == kDrop ? 0 : 1; }

// Something enters / leaves the beam whose receiver is on `pin`.
void block(int pin, double afterMs, bool blocked) {
  sim::scheduleBlocked(afterMs, beamOf(pin), blocked);
}

// Break a beam `afterMs` from now for `forMs`.
void breakBeam(int pin, double afterMs, double forMs) {
  block(pin, afterMs, true);
  block(pin, afterMs + forMs, false);
}

// The quickest duck the firmware promises to count. With POWER_SAVE on the
// beams are looked at once per cycle, so a break has to last a little
// longer to be certain of being seen (README, "Power").
const double kQuickDuckMs = kPowerSave ? 30.0 : 20.0;

void pressButton(double afterMs, double forMs) {
  sim::scheduleLevel(afterMs, kButton, 0);
  sim::scheduleLevel(afterMs + forMs, kButton, 1);
}

void tap() {
  pressButton(10, 120);
  run(130);
}

void doubleTap() {
  pressButton(10, 100);
  pressButton(260, 100);
  run(370);
}

void bootToRunning() {
  setup();
  run(cfg::SPLASH_MS + cfg::LAST_RUN_MS + 300);
}

uint32_t msNow() { return (uint32_t)(board().nowUs / 1000u); }

void shot(const std::string& name) { sim::saveScreen(std::string(kScreenDir) + name + ".bmp"); }

bool logHas(const std::string& text) { return sim::countLines(text) > 0; }

// "drops=3 reach_ins=1 trades=1" as printed on the most recent line that
// carries the tallies.
std::string tallies() {
  const std::string line = sim::lastLineWith("drops=");
  const size_t at = line.find("drops=");
  return at == std::string::npos ? "" : line.substr(at);
}

uint32_t nvs(const char* key) {
  const auto it = board().nvs.find(key);
  return it == board().nvs.end() ? 0xFFFFFFFFu : it->second;
}

int ledsLit(double above = 0.02) {
  int n = 0;
  for (uint8_t i = 0; i < cfg::LED_COUNT; ++i) n += (sim::ledDuty(cfg::PIN_LED[i]) > above);
  return n;
}

double ledSum() {
  double s = 0;
  for (uint8_t i = 0; i < cfg::LED_COUNT; ++i) s += sim::ledDuty(cfg::PIN_LED[i]);
  return s;
}

// ---------------------------------------------------------------- scenarios

struct Scenario {
  std::string name;
  std::vector<std::function<void()>> phases;  // one process each, NVS carried over
};

std::vector<Scenario>& scenarios() {
  static std::vector<Scenario> all;
  return all;
}

struct AddScenario {
  AddScenario(const char* name, std::vector<std::function<void()>> phases) {
    scenarios().push_back({name, std::move(phases)});
  }
};

#define SCENARIO(name) \
  static void scenario_##name(); \
  static AddScenario add_##name(#name, {scenario_##name}); \
  static void scenario_##name()

// --------------------------------------------------------------------------

SCENARIO(boot_powers_down_radios_and_reaches_running) {
  setup();
  CHECK(logHas("cpu: 80 MHz"));
  CHECK_EQ(board().cpuMhz, 80u);
  CHECK(logHas("lora: SX1276 found, asleep"));
  CHECK_EQ((int)(board().loraOpMode & 0x07), 0);          // SX1276 mode bits = Sleep
  CHECK(board().loraResets >= 1);
  CHECK(logHas("bluetooth: off"));
  CHECK(logHas("nvs: ok"));
  CHECK(board().isOutput[cfg::PIN_VEXT_CTRL]);
  CHECK_EQ((int)board().outputLevel[cfg::PIN_VEXT_CTRL], 0);  // Vext on
  CHECK_EQ((int)board().outputLevel[cfg::PIN_BOARD_LED], 0);
  CHECK(board().displayOn);
  CHECK_EQ(board().contrast, (int)cfg::OLED_CONTRAST_NORMAL);
  if (kPowerSave) {
    CHECK(logHas("power save: on (cycle 10 ms, light sleep on, both emitters on GPIO12)"));
    CHECK(logHas("beam DROP  GPIO32: dark level LOW (100% of samples), blocked = LOW [auto], "
                 "light seen after"));
    CHECK(logHas("beam SHELF GPIO33: dark level LOW (100% of samples), blocked = LOW [auto], "
                 "light seen after"));
    CHECK(!logHas("NO LIGHT"));
    // The emitters are dark between looks.
    CHECK_EQ((int)board().outputLevel[cfg::PIN_EMIT_DROP], 0);
    CHECK_EQ((int)board().outputLevel[cfg::PIN_EMIT_SHELF], 0);
  } else {
    CHECK(logHas("power save: off"));
    CHECK(logHas("beam DROP  GPIO32: idle level HIGH (100% of samples), blocked = LOW [auto]"));
    CHECK(logHas("beam SHELF GPIO33: idle level HIGH (100% of samples), blocked = LOW [auto]"));
    CHECK(logHas("beam capture runs during flash writes: yes"));
    // Emitters wired to the GPIO pins are lit for good.
    CHECK_EQ((int)board().outputLevel[cfg::PIN_EMIT_DROP], 1);
    CHECK_EQ((int)board().outputLevel[cfg::PIN_EMIT_SHELF], 1);
  }
  CHECK(!logHas("WARNING"));
  shot("01_splash");

  run(cfg::SPLASH_MS);
  CHECK(logHas("LAST_RUN NO RECORD"));
  shot("02_last_run_none");
  run(cfg::LAST_RUN_MS);
  CHECK(logHas("RUNNING drops=0 reach_ins=0 trades=0"));
  CHECK_EQ(board().nvsWrites, 0u);                         // nothing to save yet

  // The idle rotation: five pages, each IDLE_PAGE_MS long.
  run(300);
  const char* names[] = {"03_idle_call_drop", "04_idle_tally_drops", "05_idle_call_take",
                         "06_idle_tally_plundered", "07_idle_tally_trades"};
  for (const char* name : names) {
    shot(name);
    CHECK(sim::litPixels() > 300);
    run(cfg::IDLE_PAGE_MS);
  }
  CHECK_EQ(ledsLit(), 4);                                  // candle is burning
}

SCENARIO(drop_reach_in_and_trade_are_counted_and_saved_once) {
  bootToRunning();
  const uint32_t dropAt = msNow() + 100;
  breakBeam(kDrop, 100, 40);
  run(200);
  CHECK_EQ(sim::countLines("] DROP "), 1);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
  if (!kPowerSave) {
    // Logged with the time of the break (+ debounce), not of the main loop
    // (to the millisecond, give or take the rounding).
    const std::string line = sim::lastLineWith("] DROP ");
    unsigned long sec = 0, ms = 0;
    CHECK_EQ(std::sscanf(line.c_str(), "[%lu.%lu]", &sec, &ms), 2);
    const uint32_t at = (uint32_t)(sec * 1000 + ms);
    CHECK(at + 1 >= dropAt + 5 && at <= dropAt + 5 + 1);
  } else {
    // Logged with the time of the look that found the beam blocked (+
    // debounce): at most one cycle after the break.
    const std::string line = sim::lastLineWith("] DROP ");
    unsigned long sec = 0, ms = 0;
    CHECK_EQ(std::sscanf(line.c_str(), "[%lu.%lu]", &sec, &ms), 2);
    const uint32_t at = (uint32_t)(sec * 1000 + ms);
    CHECK(at + 1 >= dropAt + 5);
    CHECK(at <= dropAt + 5 + cfg::CYCLE_MS + 1);
  }
  CHECK(ledSum() > 2.0);                                   // flare
  shot("10_event_drop");

  breakBeam(kShelf, 2000, 800);
  run(3000);
  CHECK_EQ(sim::countLines("] REACH_IN "), 1);
  CHECK_EQ(sim::countLines("] TRADE "), 1);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=1 trades=1"));
  shot("11_event_trade");

  CHECK_EQ(board().nvsWrites, 0u);                         // still inside the quiet time
  run(cfg::SAVE_QUIET_MS + 500);
  CHECK_EQ(sim::countLines("] SAVED "), 1);
  CHECK_EQ(board().nvsWrites, 3u);                         // three tallies, written once
  CHECK_EQ(nvs("drops"), 1u);
  CHECK_EQ(nvs("reach"), 1u);
  CHECK_EQ(nvs("trades"), 1u);
  run(60000);
  CHECK_EQ(board().nvsWrites, 3u);                         // and not again

  breakBeam(kShelf, 100, 600);                             // a lone reach-in, 65 s later
  run(1000);
  shot("12_event_reach_in");
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=2 trades=1"));
}

SCENARIO(sensors_that_idle_low_work_the_same) {
  // Receivers whose output is HIGH without light and LOW with it.
  board().beamDarkLevel[0] = 1;
  board().beamDarkLevel[1] = 1;
  bootToRunning();
  if (kPowerSave) {
    CHECK(logHas("beam DROP  GPIO32: dark level HIGH (100% of samples), blocked = HIGH [auto]"));
  } else {
    CHECK(logHas("beam DROP  GPIO32: idle level LOW (100% of samples), blocked = HIGH [auto]"));
  }
  CHECK(!logHas("WARNING"));
  breakBeam(kDrop, 100, 40);
  breakBeam(kShelf, 1500, 700);
  run(3000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=1 trades=1"));
}

SCENARIO(no_duck_is_missed_while_the_screen_redraws_or_flash_is_written) {
  bootToRunning();
  // A quick duck (20 ms in the beam; 30 ms with POWER_SAVE) every 1.337 s
  // for five minutes. The odd period walks the breaks across every phase
  // of the screen redraws, banner changes and flash writes.
  const int ducks = 224;
  for (int i = 0; i < ducks; ++i) breakBeam(kDrop, 500.0 + i * 1337.0, kQuickDuckMs);
  run(500.0 + ducks * 1337.0 + 1000.0);
  CHECK_EQ(sim::countLines("] DROP "), ducks);
  char want[64];
  std::snprintf(want, sizeof(want), "drops=%d reach_ins=0 trades=0", ducks);
  CHECK_EQ(tallies(), std::string(want));
  CHECK(kPowerSave ? board().stripsSent > 800              // the screen really was busy
                   : board().framesSent > 200);
  CHECK(!logHas("WARN"));
}

SCENARIO(tumbling_duck_is_one_count_end_to_end) {
  bootToRunning();
  breakBeam(kDrop, 100, 25);
  breakBeam(kDrop, 160, 15);
  breakBeam(kDrop, 300, 30);
  run(2000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
}

SCENARIO(full_box_and_parked_duck_are_shown_and_recover) {
  bootToRunning();
  block(kDrop, 100, true);                                 // a duck comes to rest in the beam
  run(3900);
  CHECK(!logHas("FULL_ON"));
  run(400);
  CHECK_EQ(sim::countLines("] FULL_ON "), 1);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
  shot("20_event_full");
  run(cfg::EVENT_BANNER_MS + 500);
  shot("21_idle_full_page");
  run(30000);
  CHECK_EQ(sim::countLines("] DROP "), 1);                 // nothing more while full

  block(kDrop, 100, false);
  run(1900);
  CHECK(!logHas("FULL_OFF"));
  run(400);
  CHECK_EQ(sim::countLines("] FULL_OFF "), 1);

  block(kShelf, 100, true);                                // a duck parked in the shelf beam
  run(cfg::SHELF_STUCK_MS + 300);
  CHECK_EQ(sim::countLines("] SHELF_STUCK_ON "), 1);
  shot("22_event_parked_duck");
  block(kShelf, 100, false);
  run(2500);
  CHECK_EQ(sim::countLines("] SHELF_STUCK_OFF "), 1);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
}

SCENARIO(short_press_cycles_normal_dim_stealth) {
  bootToRunning();
  run(1000);
  const double normalLight = ledSum();
  CHECK(normalLight > 0.3);

  tap();
  run(cfg::BUTTON_DOUBLE_GAP_MS + 100);
  CHECK_EQ(sim::countLines("] MODE DIM"), 1);
  CHECK_EQ(board().contrast, (int)cfg::OLED_CONTRAST_DIM);
  CHECK(board().displayOn);
  shot("30_notice_dim");
  double dimLight = 0;
  for (int i = 0; i < 100; ++i) {                          // average over 2 s of flicker
    run(20);
    dimLight += ledSum() / 100.0;
  }
  CHECK(dimLight > 0.02 && dimLight < 4 * cfg::LED_AVERAGE_BRIGHTNESS * cfg::LED_DIM_FACTOR * 1.6);

  tap();
  run(cfg::BUTTON_DOUBLE_GAP_MS + 100);
  CHECK_EQ(sim::countLines("] MODE STEALTH"), 1);
  CHECK(board().displayOn);                                // the notice is still up
  shot("31_notice_stealth");
  run(cfg::NOTICE_BANNER_MS + 200);
  CHECK(!board().displayOn);                               // then dark
  CHECK_EQ(ledsLit(0.0), 0);

  breakBeam(kDrop, 100, 40);                               // still counting in the dark
  breakBeam(kShelf, 900, 500);
  run(3000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=1 trades=1"));
  CHECK(!board().displayOn);
  CHECK_EQ(ledsLit(0.0), 0);

  tap();
  run(cfg::BUTTON_DOUBLE_GAP_MS + 100);
  CHECK_EQ(sim::countLines("] MODE NORMAL"), 1);
  // POWER_SAVE: the picture is sent in eight strips, one per cycle, and
  // the panel lights when the last one is there.
  if (kPowerSave) run(150);
  CHECK(board().displayOn);
  CHECK_EQ(board().contrast, (int)cfg::OLED_CONTRAST_NORMAL);
  run(500);
  CHECK_EQ(ledsLit(), 4);
}

SCENARIO(double_press_pauses_counting_for_restocking) {
  bootToRunning();
  doubleTap();
  CHECK_EQ(sim::countLines("] RESTOCK_START"), 1);
  CHECK(!logHas("MODE"));                                  // not mistaken for single presses
  run(1000);
  shot("40_restocking");
  breakBeam(kDrop, 100, 40);                               // the owner loads ducks
  breakBeam(kDrop, 1000, 40);
  breakBeam(kShelf, 2000, 3000);
  run(6000);
  CHECK_EQ(sim::countLines("] DROP "), 0);
  CHECK_EQ(sim::countLines("] REACH_IN "), 0);

  doubleTap();                                             // done early
  CHECK_EQ(sim::countLines("] RESTOCK_END (ended by double press)"), 1);
  shot("41_notice_restock_over");
  breakBeam(kDrop, 500, 40);
  run(1500);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));

  doubleTap();                                             // and once more, left to time out
  run(cfg::RESTOCK_PAUSE_MS - 2000);
  CHECK_EQ(sim::countLines("] RESTOCK_END"), 1);
  run(2500);
  CHECK_EQ(sim::countLines("] RESTOCK_END (time up)"), 1);
}

SCENARIO(restocking_is_shown_even_in_stealth) {
  bootToRunning();
  tap();
  run(600);
  tap();
  run(600 + cfg::NOTICE_BANNER_MS);
  CHECK(!board().displayOn);
  doubleTap();
  run(500);
  CHECK(board().displayOn);
  doubleTap();
  run(cfg::NOTICE_BANNER_MS + 500);
  CHECK(!board().displayOn);
}

SCENARIO(holding_prg_five_seconds_resets_the_tallies) {
  bootToRunning();
  breakBeam(kDrop, 100, 40);
  breakBeam(kDrop, 1100, 40);
  breakBeam(kShelf, 2000, 500);
  run(3000 + cfg::SAVE_QUIET_MS + 500);
  CHECK_EQ(nvs("drops"), 2u);

  // Released after 3 s: cancelled, nothing changes.
  pressButton(100, 3000);
  run(2600);
  shot("50_reset_countdown");
  run(1500);
  CHECK_EQ(sim::countLines("] RESET_CANCELLED"), 1);
  shot("51_notice_reset_cancelled");
  CHECK_EQ(nvs("drops"), 2u);
  CHECK(!logHas("] MODE"));                                // a long press is not a short press

  // Held all the way.
  pressButton(100, 6000);
  run(4900);
  CHECK(!logHas("] RESET all"));
  run(400);
  CHECK_EQ(sim::countLines("] RESET all"), 1);
  shot("52_notice_reset_done");
  CHECK_EQ(nvs("drops"), 0u);                              // saved at once
  CHECK_EQ(nvs("reach"), 0u);
  CHECK_EQ(nvs("trades"), 0u);
  run(3000);
  breakBeam(kDrop, 100, 40);
  run(1000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
}

SCENARIO(beam_test_mode_shows_beams_and_chases_leds_without_saving) {
  setup();
  pressButton(300, 900);                                   // PRG during the splash
  run(1500);
  CHECK_EQ(sim::countLines("] BEAM_TEST start"), 1);
  run(700);                                                // past the double-press gap
  CHECK(!logHas("BEAM_TEST end"));                         // the entry press is not an exit

  // LED chase: exactly one LED lit at a time, in the order LED1..LED4.
  int seenOrder[8];
  int seen = 0;
  int last = -1;
  for (int i = 0; i < 1500 && seen < 8; ++i) {
    run(10);
    int lit = -1, litCount = 0;
    for (uint8_t k = 0; k < cfg::LED_COUNT; ++k) {
      if (sim::ledDuty(cfg::PIN_LED[k]) > 0.5) {
        lit = cfg::PIN_LED[k];
        ++litCount;
      }
    }
    if (kPowerSave) {
      // Flashed outputs are measured over 30 ms, so for a moment at each
      // change-over neither LED is above one half.
      CHECK(litCount <= 1);
      if (litCount == 0) continue;
    } else {
      CHECK_EQ(litCount, 1);
    }
    if (lit != last) {
      seenOrder[seen++] = lit;
      last = lit;
    }
  }
  CHECK_EQ(seen, 8);
  for (int i = 1; i < seen; ++i) {
    int prevIndex = -1, index = -1;
    for (uint8_t k = 0; k < cfg::LED_COUNT; ++k) {
      if (cfg::PIN_LED[k] == seenOrder[i - 1]) prevIndex = k;
      if (cfg::PIN_LED[k] == seenOrder[i]) index = k;
    }
    CHECK_EQ(index, (prevIndex + 1) % cfg::LED_COUNT);
  }

  // Three very short breaks of the drop beam (the interrupt catches 3 ms;
  // a look once per cycle needs 30), one long block of the shelf.
  const double shortBreak = kPowerSave ? 30 : 3;
  breakBeam(kDrop, 100, shortBreak);
  breakBeam(kDrop, 300, shortBreak);
  breakBeam(kDrop, 500, shortBreak);
  block(kShelf, 700, true);
  run(1000);
  CHECK_EQ(sim::countLines("TEST DROP beam BLOCKED"), 3);
  CHECK_EQ(sim::countLines("TEST SHELF beam BLOCKED"), 1);
  CHECK_EQ((int)board().outputLevel[cfg::PIN_BOARD_LED], 1);   // board LED = "a beam is blocked"
  shot("60_beam_test_shelf_blocked");
  block(kShelf, 10, false);
  run(300);
  CHECK_EQ((int)board().outputLevel[cfg::PIN_BOARD_LED], 0);
  shot("61_beam_test_clear");

  pressButton(100, 1200);                                  // hold = re-learn polarity
  run(2500);
  CHECK_EQ(sim::countLines("TEST re-learning polarity"), 1);
  CHECK(!logHas("BEAM_TEST end"));

  CHECK_EQ(board().nvsWrites, 0u);                         // nothing saved in test mode
  tap();
  run(cfg::BUTTON_DOUBLE_GAP_MS + 200);
  CHECK_EQ(sim::countLines("] BEAM_TEST end"), 1);
  CHECK_EQ((int)board().outputLevel[cfg::PIN_BOARD_LED], 0);
  run(cfg::LAST_RUN_MS + 300);
  CHECK(logHas("RUNNING drops=0 reach_ins=0 trades=0"));
  CHECK(!logHas("] DROP "));
  breakBeam(kDrop, 100, 40);                               // and normal counting works
  run(1000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
}

SCENARIO(beam_test_mode_left_alone_ends_by_itself) {
  setup();
  pressButton(300, 900);
  run(1500);
  CHECK_EQ(sim::countLines("] BEAM_TEST start"), 1);
  run(cfg::BEAM_TEST_TIMEOUT_MS - 5000);
  CHECK(!logHas("BEAM_TEST end"));
  run(6000 + cfg::LAST_RUN_MS);
  CHECK_EQ(sim::countLines("] BEAM_TEST end"), 1);
  CHECK(logHas("RUNNING drops=0"));
  CHECK_EQ(ledsLit(), 4);                                  // candle again, not the chase
}

SCENARIO(a_beam_blocked_at_power_up_is_flagged) {
  board().beamBlocked[0] = true;                           // duck resting in the drop beam
  bootToRunning();
  if (!kPowerSave) {
    // The blocked beam was taken for the idle level: polarity is in doubt.
    CHECK(logHas("WARNING: beam polarity is doubtful"));
    run(500);
    shot("70_notice_check_beams");
    return;
  }
  // POWER_SAVE: the polarity is read with the emitters dark, so it is
  // right all the same. The box says that it saw no light and asks for the
  // beams to be checked, treats the beam as blocked, does not count the
  // duck that is lying there, and reports the chamber as full after the
  // usual time.
  CHECK(logHas("beam DROP  GPIO32: dark level LOW (100% of samples), blocked = LOW [auto], "
               "** NO LIGHT SEEN **"));
  CHECK(logHas("beam SHELF GPIO33: dark level LOW (100% of samples), blocked = LOW [auto], "
               "light seen after"));
  CHECK(logHas("WARNING: check the beams"));
  run(500);
  shot("70_notice_check_beams");
  run(cfg::DROP_FULL_AFTER_MS);
  CHECK(logHas("FULL_ON"));
  CHECK(!logHas("] DROP "));
  block(kDrop, 100, false);                                // taken out again
  run(cfg::DROP_FULL_CLEAR_MS + 600);
  CHECK(logHas("FULL_OFF"));
  CHECK_EQ(tallies(), std::string("drops=0 reach_ins=0 trades=0"));
  breakBeam(kDrop, 100, 40);                               // and a real duck counts
  run(1000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
}

SCENARIO(polling_safety_net_recovers_a_missed_edge) {
  if (kPowerSave) return;                                  // no interrupt to miss there
  bootToRunning();
  run(1000);
  sim::blockSilently(0, true);                             // level changes with no interrupt
  run(cfg::BEAM_RESYNC_MS + 100);
  CHECK_EQ(sim::countLines("WARN beam level re-read by polling"), 1);
  run(200);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
  run(cfg::DROP_FULL_AFTER_MS);
  CHECK(logHas("FULL_ON"));
}

SCENARIO(works_across_millis_rollover) {
  // Power-up 20 s before the 32-bit millisecond clock wraps.
  board().nowUs = (0x100000000ull - 20000ull) * 1000ull;
  bootToRunning();
  breakBeam(kDrop, 500, 40);                               // before the wrap
  breakBeam(kShelf, 16000, 600);                           // spans nothing, after the wrap
  breakBeam(kDrop, 20000, 40);
  run(30000);
  CHECK(msNow() < 60000u);                                 // the clock did wrap
  CHECK_EQ(tallies(), std::string("drops=2 reach_ins=1 trades=1"));
  CHECK_EQ(sim::countLines("] SAVED "), 2);
  run(11 * 60000.0);
  CHECK(logHas("RUNTIME 0 h 10 min"));
  CHECK(!logHas("WARN"));
}

SCENARIO(works_across_microsecond_timer_rollover) {
  // The edge capture keeps 32-bit microsecond stamps (wrap every 71.6 min).
  board().nowUs = 0x100000000ull - 9000000ull;             // 9 s before that wrap
  bootToRunning();
  for (int i = 0; i < 8; ++i) breakBeam(kDrop, 500.0 + i * 800.0, 30);
  run(8000);
  CHECK_EQ(tallies(), std::string("drops=8 reach_ins=0 trades=0"));
  CHECK(!logHas("WARN"));
}

// ---- POWER_SAVE only --------------------------------------------------------

// Share of the time between two calls that a pin was HIGH / the chip slept.
struct Meter {
  uint64_t t0 = 0, sleep0 = 0, high0[sim::kPins] = {};
  void start() {
    t0 = board().nowUs;
    sleep0 = board().sleptUs;
    for (int p = 0; p < sim::kPins; ++p) high0[p] = sim::highUs(p);
  }
  double elapsed() const { return (double)(board().nowUs - t0); }
  double high(int pin) const { return (double)(sim::highUs(pin) - high0[pin]) / elapsed(); }
  double asleep() const { return (double)(board().sleptUs - sleep0) / elapsed(); }
  double leds() const {
    double s = 0;
    for (uint8_t i = 0; i < cfg::LED_COUNT; ++i) s += high(cfg::PIN_LED[i]);
    return s / cfg::LED_COUNT;
  }
};

// The figures of the most recent "POWER" line on the serial log.
struct PowerLine {
  bool ok = false;
  unsigned awake = 0, emit = 0, leds = 0, screen = 0;
  unsigned long sleeps = 0, refused = 0, slowest = 0, lead0 = 0, lead1 = 0;
  size_t length = 0;
};
PowerLine lastPowerLine() {
  PowerLine p;
  const std::string line = sim::lastLineWith("] POWER ");
  const size_t at = line.find("POWER ");
  if (at == std::string::npos) return p;
  p.length = line.size();
  p.ok = std::sscanf(line.c_str() + at,
                     "POWER awake=%u%% emit=%u%% leds=%u%% screen=%u%% sleeps=%lu refused=%lu "
                     "slowest=%lums lead=%lu/%luus",
                     &p.awake, &p.emit, &p.leds, &p.screen, &p.sleeps, &p.refused, &p.slowest,
                     &p.lead0, &p.lead1) == 9;
  return p;
}

SCENARIO(power_save_idle_box_sleeps_most_of_the_time) {
  if (!kPowerSave) return;
  bootToRunning();
  run(40000);                                              // past the "stay on" time
  Meter m;
  m.start();
  run(120000);
  // Emitters: lit for the settle time once per 10 ms cycle (600 us here).
  CHECK(m.high(cfg::PIN_EMIT_DROP) > 0.03 && m.high(cfg::PIN_EMIT_DROP) < 0.10);
  CHECK(m.high(cfg::PIN_EMIT_SHELF) > 0.03 && m.high(cfg::PIN_EMIT_SHELF) < 0.10);
  // Candle: the configured average, give or take the flicker.
  CHECK(m.leds() > cfg::LED_AVERAGE_BRIGHTNESS * 0.75);
  CHECK(m.leds() < cfg::LED_AVERAGE_BRIGHTNESS * 1.25);
  // The processor is awake while a beam is looked at and while an LED is
  // lit, and each sleep costs over a millisecond of waking up.
  CHECK(m.asleep() > 0.35);
  const PowerLine p = lastPowerLine();
  CHECK(p.ok);
  // A line longer than the serial port's 128 byte buffer would hold the
  // loop up while it drains.
  CHECK(p.length < 120);
  CHECK(p.awake >= 45 && p.awake <= 65);
  // What the report says must agree with what the pins and the clock did.
  CHECK(p.awake + 5 >= (unsigned)((1.0 - m.asleep()) * 100));
  CHECK(p.awake <= (unsigned)((1.0 - m.asleep()) * 100) + 5);
  CHECK(p.lead0 >= 500 && p.lead0 <= 800);                 // 2 x 250..300 us + 100
  CHECK(p.lead1 + 30 >= p.lead0 && p.lead1 <= p.lead0 + 30);
  CHECK(p.emit >= 3 && p.emit <= 10);
  CHECK(p.leds >= 22 && p.leds <= 38);
  if (kPeeks) CHECK(p.screen >= 15 && p.screen <= 25);     // 4 s in every 20 s
  else CHECK(p.screen >= 99);                              // always on
  CHECK(p.sleeps > 5000);
  CHECK(p.slowest <= 2 * cfg::CYCLE_MS);
  CHECK(!logHas("NOTE"));
  CHECK(!logHas("WARN"));
  CHECK(!logHas("] DROP "));
  CHECK(!logHas("] REACH_IN "));
  CHECK_EQ(sim::countLines("] POWER "), 2);               // one a minute
}

SCENARIO(power_save_stealth_is_the_cheapest_mode) {
  if (!kPowerSave) return;
  bootToRunning();
  tap();
  run(600);
  tap();
  run(600 + cfg::NOTICE_BANNER_MS + 500);
  CHECK(logHas("] MODE STEALTH"));
  Meter m;
  m.start();
  run(60000);
  CHECK(m.leds() == 0.0);
  CHECK(!board().displayOn);
  CHECK(m.asleep() > 0.72);
  breakBeam(kDrop, 100, 40);                               // still counting
  run(1000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
}

SCENARIO(power_save_screen_peeks_when_idle_and_wakes_for_a_duck) {
  if (!kPowerSave || !kPeeks) return;
  bootToRunning();
  run(cfg::SCREEN_STAY_ON_MS - 1000);
  CHECK(board().displayOn);                                // still in the "stay on" time
  run(2000);
  CHECK(!board().displayOn);                               // then dark

  // Three whole periods: lit for the peek time in each, one picture per
  // peek, and each peek a different page with something on it.
  const uint32_t stripsBefore = board().stripsSent;
  int litSamples = 0, samples = 0, peeks = 0;
  bool was = false;
  int lastLit = -1;
  for (uint32_t t = 0; t < 3 * cfg::SCREEN_PEEK_EVERY_MS; t += 50) {
    run(50);
    ++samples;
    litSamples += board().displayOn;
    if (board().displayOn && !was) {
      ++peeks;
      // The picture was complete before the panel was lit.
      CHECK(sim::litPixels() > 300);
      CHECK(sim::litPixels() != lastLit);
      lastLit = sim::litPixels();
      if (peeks == 1) shot("90_peek");
    }
    was = board().displayOn;
  }
  CHECK_EQ(peeks, 3);
  const double share = (double)litSamples / samples;
  const uint32_t every = cfg::SCREEN_PEEK_EVERY_MS ? cfg::SCREEN_PEEK_EVERY_MS : 1;
  const double want = (double)cfg::SCREEN_PEEK_ON_MS / every;
  CHECK(share > want - 0.03 && share < want + 0.03);
  // One picture per peek, and of it only the strips that changed.
  const uint32_t strips = board().stripsSent - stripsBefore;
  CHECK(strips >= 3 && strips <= 3 * 8);

  // A duck lights the screen at once (well, within a couple of tenths).
  while (board().displayOn) run(50);
  run(1000);
  CHECK(!board().displayOn);
  breakBeam(kDrop, 100, 40);
  run(400);
  CHECK(board().displayOn);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
  run(cfg::SCREEN_STAY_ON_MS - 2000);
  CHECK(board().displayOn);                                // and it stays on for a while
  run(4000);
  CHECK(!board().displayOn);
}

SCENARIO(power_save_slow_receivers_are_measured_and_fake_no_ducks) {
  if (!kPowerSave) return;
  board().beamResponseUs[0] = 1700;                        // close to the 2 ms of the data sheet
  board().beamResponseUs[1] = 900;
  bootToRunning();
  CHECK(logHas("beam DROP  GPIO32: dark level LOW (100% of samples), blocked = LOW [auto], "
               "light seen after 17"));
  CHECK(logHas("emitter lead 3000 us"));                   // 2 x 17xx + 100, capped
  CHECK(logHas("emitter lead 19"));                        // 2 x 9xx + 100
  Meter m;
  m.start();
  run(60000);
  CHECK(!logHas("] DROP "));
  CHECK(!logHas("] REACH_IN "));
  CHECK(!logHas("NOTE"));
  // Both emitters hang on one pin, lit for the longer of the two leads.
  CHECK(m.high(cfg::PIN_EMIT_DROP) > 0.27 && m.high(cfg::PIN_EMIT_DROP) < 0.36);
  for (int i = 0; i < 20; ++i) breakBeam(kDrop, 500.0 + i * 1337.0, 40);
  breakBeam(kShelf, 300, 500);
  run(30000);
  CHECK_EQ(tallies(), std::string("drops=20 reach_ins=1 trades=1"));
}

SCENARIO(power_save_receiver_turning_slow_later_is_noticed_not_counted) {
  if (!kPowerSave) return;
  bootToRunning();                                         // measured at 250 us, lead 600 us
  run(5000);
  board().beamResponseUs[0] = 1300;                        // e.g. the emitter dimming as cells sag
  board().beamResponseUs[1] = 2900;
  run(30000);
  CHECK(!logHas("] DROP "));
  CHECK(!logHas("] REACH_IN "));
  CHECK(logHas("NOTE DROP receiver is slower than assumed (late 3 times): "
               "emitter lead raised to 2"));
  CHECK(logHas("NOTE SHELF receiver is slower than assumed (late 3 times): "
               "emitter lead raised to 3000 us"));
  CHECK_EQ(sim::countLines("] NOTE "), 2);                 // once each, then it fits
  breakBeam(kDrop, 100, 40);
  breakBeam(kShelf, 900, 500);
  run(3000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=1 trades=1"));
}

SCENARIO(power_save_full_box_does_not_cost_more_battery) {
  if (!kPowerSave) return;
  bootToRunning();
  block(kDrop, 100, true);                                 // chamber full for ten minutes
  block(kShelf, 400, true);                                // and a duck parked on the shelf
  run(30000);
  CHECK(logHas("FULL_ON"));
  CHECK(logHas("SHELF_STUCK_ON"));
  Meter m;
  m.start();
  run(600000);
  // Short looks, plus one long one in fifty.
  CHECK(m.high(cfg::PIN_EMIT_DROP) < 0.10);
  CHECK(m.high(cfg::PIN_EMIT_SHELF) < 0.10);
  CHECK(m.asleep() > 0.35);
  CHECK_EQ(sim::countLines("] DROP "), 1);
  block(kDrop, 100, false);
  block(kShelf, 100, false);
  run(5000);
  CHECK(logHas("FULL_OFF"));
  CHECK(logHas("SHELF_STUCK_OFF"));
}

SCENARIO(power_save_beam_blocked_at_power_up_with_a_slow_receiver_recovers) {
  if (!kPowerSave) return;
  // Nothing can be measured at start-up, and the receiver then turns out
  // to be slower than the 1.5 ms it is given by default.
  board().beamBlocked[1] = true;
  board().beamResponseUs[1] = 2600;
  bootToRunning();
  CHECK(logHas("beam SHELF GPIO33: dark level LOW (100% of samples), blocked = LOW [auto], "
               "** NO LIGHT SEEN **"));
  run(2000);
  block(kShelf, 100, false);
  run(3000);                                               // found by a long look within 0.5 s
  // Measured as soon as it shows light for the first time.
  CHECK(logHas("NOTE SHELF receiver measured now: light after 26"));
  CHECK(logHas("emitter lead 3000 us"));
  breakBeam(kShelf, 100, 500);
  run(3000);
  CHECK_EQ(sim::countLines("] REACH_IN "), 1);
}

SCENARIO(power_save_bright_flare_does_not_stall_the_box) {
  if (!kPowerSave) return;
  bootToRunning();
  run(56000);                                              // past the first POWER line
  CHECK_EQ(sim::countLines("] POWER "), 1);
  // Ducks in quick succession keep the LEDs flaring; the loop must keep
  // its pace and miss none of them.
  for (int i = 0; i < 40; ++i) breakBeam(kDrop, 200.0 + i * 477.0, 40);
  run(200.0 + 40 * 477.0 + 1000.0);
  CHECK_EQ(tallies(), std::string("drops=40 reach_ins=0 trades=0"));
  run(45000);
  CHECK_EQ(sim::countLines("] POWER "), 2);
  const PowerLine p = lastPowerLine();
  CHECK(p.ok);
  CHECK(p.slowest >= cfg::CYCLE_MS);
  CHECK(p.slowest <= 25);                                  // a flash write is the slowest thing
}

// What must not count is a receiver output that is dark for less than the
// debounce time. A slow receiver stretches a shadow (it lets go of the
// light quickly and takes its time to answer again), so the shadows here
// are chosen to stay under 5 ms at the receiver's output.
static void shadowsDoNotCount(uint32_t responseUs, const double lengthsMs[4]) {
  board().beamResponseUs[0] = responseUs;
  board().beamResponseUs[1] = responseUs;
  bootToRunning();
  // 800 shadows at every phase of the cycle.
  for (int i = 0; i < 800; ++i) breakBeam(kDrop, 300.0 + i * 61.37, lengthsMs[i % 4]);
  run(300.0 + 800 * 61.37 + 2000.0);
  CHECK_EQ(sim::countLines("] DROP "), 0);
  CHECK_EQ(tallies(), std::string("drops=0 reach_ins=0 trades=0"));
  breakBeam(kDrop, 100, 40);                               // a duck still counts
  run(1000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
}

// With POWER_SAVE on, the darkness is timed in steps of 0.1 ms and anything
// under 5 ms is thrown out. With it off, edges are stamped in whole
// milliseconds, so a shadow has to be under 4 ms to be certain not to
// count (power saving off).
SCENARIO(shadows_shorter_than_the_debounce_never_count) {
  const double strict[4] = {2.0, 3.0, 4.0, 4.5};           // output dark 0.15 ms less
  const double coarse[4] = {2.0, 2.5, 3.0, 3.5};
  shadowsDoNotCount(250, kPowerSave ? strict : coarse);
}

SCENARIO(shadows_do_not_count_with_slow_receivers_either) {
  // Receivers so slow that the emitter lead is at its maximum, which
  // leaves the least room in the cycle. Output dark 1.3 ms longer.
  const double strict[4] = {2.0, 2.5, 3.0, 3.5};
  const double coarse[4] = {1.0, 1.5, 2.0, 2.5};
  shadowsDoNotCount(1700, kPowerSave ? strict : coarse);
  if (kPowerSave) CHECK(logHas("emitter lead 3000 us"));
}

SCENARIO(power_save_flickering_beam_does_not_raise_the_emitter_lead_for_good) {
  if (!kPowerSave) return;
  bootToRunning();
  // Fingers wiggling in the shelf beam: hundreds of 3 ms interruptions,
  // many of which end in the middle of a look.
  for (int i = 0; i < 400; ++i) breakBeam(kShelf, 300.0 + i * 13.7, 3.0);
  run(300.0 + 400 * 13.7 + 3000.0);
  CHECK_EQ(sim::countLines("] REACH_IN "), 0);             // nothing of 60 ms or more
  run(130000);
  const PowerLine p = lastPowerLine();
  CHECK(p.ok);
  CHECK(p.lead0 >= 500 && p.lead0 <= 800);                 // as measured at start-up
  CHECK(p.lead1 >= 500 && p.lead1 <= 800);
  Meter m;
  m.start();
  run(20000);
  CHECK(m.high(cfg::PIN_EMIT_SHELF) < 0.10);
}

SCENARIO(power_save_reset_countdown_is_painted_completely) {
  if (!kPowerSave) return;
  bootToRunning();
  run(1000);
  // The countdown picture changes at every refresh. Its text is at the
  // top, its progress bar at the bottom: both must be there, and the bar
  // must grow.
  auto barPixels = [] {
    int n = 0;
    for (int y = 48; y < 64; ++y) {
      for (int x = 0; x < 128; ++x) n += board().screen[y][x];
    }
    return n;
  };
  auto textPixels = [] {
    int n = 0;
    for (int y = 0; y < 40; ++y) {
      for (int x = 0; x < 128; ++x) n += board().screen[y][x];
    }
    return n;
  };
  pressButton(100, 6000);
  run(1500);
  const int barEarly = barPixels();
  CHECK(textPixels() > 150);
  CHECK(barEarly > 100);                                   // the outline at least
  run(1500);
  const int barMid = barPixels();
  shot("91_reset_countdown_strips");
  run(1500);
  const int barLate = barPixels();
  CHECK(barMid > barEarly + 40);
  CHECK(barLate > barMid + 40);
  run(1000);
  CHECK_EQ(sim::countLines("] RESET all"), 1);
}

SCENARIO(power_save_first_press_on_a_dark_screen_only_lights_it) {
  if (!kPowerSave || !kPeeks) return;
  bootToRunning();
  run(cfg::SCREEN_STAY_ON_MS + 2000);
  CHECK(!board().displayOn);
  tap();
  run(cfg::BUTTON_DOUBLE_GAP_MS + 300);
  CHECK_EQ(sim::countLines("] WAKE "), 1);
  CHECK(!logHas("] MODE"));
  CHECK(board().displayOn);
  CHECK_EQ(board().contrast, (int)cfg::OLED_CONTRAST_NORMAL);
  tap();                                                   // now it changes the mode
  run(cfg::BUTTON_DOUBLE_GAP_MS + 300);
  CHECK_EQ(sim::countLines("] MODE DIM"), 1);
  CHECK_EQ(sim::countLines("] WAKE "), 1);
  // In Stealth the screen is dark on purpose: one press leaves Stealth.
  tap();
  run(cfg::BUTTON_DOUBLE_GAP_MS + 300 + cfg::NOTICE_BANNER_MS + 500);
  CHECK_EQ(sim::countLines("] MODE STEALTH"), 1);
  CHECK(!board().displayOn);
  tap();
  run(cfg::BUTTON_DOUBLE_GAP_MS + 300);
  CHECK_EQ(sim::countLines("] MODE NORMAL"), 1);
  CHECK_EQ(sim::countLines("] WAKE "), 1);
}

SCENARIO(power_save_light_on_a_receiver_between_looks_is_reported) {
  if (!kPowerSave) return;
  bootToRunning();
  run(cfg::SCREEN_STAY_ON_MS + 2000);
  CHECK(board().displayOn == !kPeeks);
  board().beamStrayLight[0] = true;                        // sun on the drop receiver
  run(500);
  CHECK(!logHas("WARN"));                                  // not for a flash of light
  run(1000);
  CHECK_EQ(sim::countLines("WARN DROP receiver sees light while its emitter is dark"), 1);
  CHECK(board().displayOn);                                // CHECK BEAMS is on the screen
  shot("92_stray_light");
  CHECK(!logHas("] DROP "));
  board().beamStrayLight[0] = false;
  run(5000);
  CHECK(!logHas("NOTE DROP receiver is dark"));            // not taken back in a hurry
  run(30000);
  CHECK_EQ(sim::countLines("NOTE DROP receiver is dark between looks again"), 1);
  // Light that comes and goes every second gives one warning, not sixty.
  for (int i = 0; i < 60; ++i) {
    board().beamStrayLight[1] = (i % 2) == 0;
    run(1100);
  }
  board().beamStrayLight[1] = false;
  CHECK_EQ(sim::countLines("WARN SHELF receiver sees light"), 1);
  CHECK(!logHas("] REACH_IN "));
  run(35000);
  breakBeam(kDrop, 100, 40);
  run(1000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
}

SCENARIO(power_save_emitters_left_on_vext_are_flagged_at_start) {
  if (!kPowerSave) return;
  // Wired wrongly: the emitters are lit whenever Vext is on,
  // so "dark" and "lit" are upside down and nothing can be counted. The
  // box cannot fix that, but it must say so.
  board().emittersOnVext = true;
  bootToRunning();
  CHECK(logHas("beam DROP  GPIO32: dark level HIGH (100% of samples), blocked = HIGH [auto], "
               "** NO LIGHT SEEN **"));
  CHECK(logHas("beam SHELF GPIO33: dark level HIGH (100% of samples), blocked = HIGH [auto], "
               "** NO LIGHT SEEN **"));
  CHECK(logHas("WARNING: check the beams"));
  run(500);
  CHECK(board().displayOn);
  shot("93_emitters_on_vext");
  // And nothing later mistakes the permanent light for a measurement.
  run(30000);
  CHECK(!logHas("measured now"));
  CHECK(!logHas("] DROP "));
}

SCENARIO(power_save_flash_page_erase_fakes_nothing) {
  if (!kPowerSave) return;
  bootToRunning();
  breakBeam(kDrop, 100, 40);
  run(1000);
  // The save that follows runs into a page erase: the loop stands still
  // for 300 ms, with a hand in the shelf beam for part of that time.
  board().nvsStallNextWriteUs = 300000;
  breakBeam(kShelf, cfg::SAVE_QUIET_MS - 1000 + 100, 400);
  run(cfg::SAVE_QUIET_MS + 3000);
  CHECK_EQ(sim::countLines("] SAVED "), 1);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=1 trades=1"));
  run(20000);
  CHECK_EQ(sim::countLines("] DROP "), 1);
  CHECK_EQ(sim::countLines("] REACH_IN "), 1);
  CHECK(!logHas("WARN"));
  CHECK(!logHas("NOTE"));
  CHECK_EQ(ledsLit(), 4);
}

SCENARIO(power_save_a_week_of_quiet_looks_the_same_as_a_minute) {
  if (!kPowerSave) return;
  // Not a week, but long enough to cross the 71.6 minute wrap of the
  // microsecond clock and many reports: the figures must not drift.
  bootToRunning();
  run(150 * 60000.0);
  CHECK(!logHas("WARN"));
  CHECK(!logHas("NOTE"));
  CHECK(!logHas("] DROP "));
  CHECK(!logHas("] REACH_IN "));
  CHECK(logHas("RUNTIME 2 h 20 min"));
  const PowerLine p = lastPowerLine();
  CHECK(p.ok);
  CHECK(p.awake >= 45 && p.awake <= 65);
  CHECK(p.emit >= 3 && p.emit <= 10);
  CHECK(p.leds >= 22 && p.leds <= 38);
  if (kPeeks) CHECK(p.screen >= 15 && p.screen <= 25);
  else CHECK(p.screen >= 99);
  breakBeam(kDrop, 100, 40);
  run(1000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
}

SCENARIO(power_save_loose_emitter_wire_counts_nothing) {
  if (!kPowerSave) return;
  bootToRunning();
  run(2000);
  // Both emitters share one wire. It loses contact five times for a tenth
  // of a second: both beams go dark together, which must not be taken for
  // a duck and a hand.
  for (int i = 0; i < 5; ++i) {
    board().emitterWireOpen = true;
    sim::beamsChanged();
    run(100);
    board().emitterWireOpen = false;
    sim::beamsChanged();
    run(900);
  }
  CHECK_EQ(tallies(), std::string("drops=0 reach_ins=0 trades=0"));
  CHECK(!logHas("] DROP "));
  CHECK(!logHas("] REACH_IN "));
  CHECK(!logHas("WARN"));                                  // too short to complain about

  // Then it comes off for good. The box says so, counts nothing, and goes
  // on to show the two "blocked for a long time" messages, which is what
  // the owner will notice.
  board().emitterWireOpen = true;
  sim::beamsChanged();
  run(1000);
  CHECK_EQ(sim::countLines("WARN both beams went dark at the same moment"), 1);
  run(cfg::SHELF_STUCK_MS + 2000);
  CHECK(logHas("FULL_ON"));
  CHECK(logHas("SHELF_STUCK_ON"));
  CHECK(!logHas("] DROP "));
  CHECK(!logHas("] REACH_IN "));
  CHECK(!logHas("] TRADE "));

  // Soldered back on: the messages go away, still nothing counted, and
  // real ducks count again.
  board().emitterWireOpen = false;
  sim::beamsChanged();
  run(5000);
  CHECK_EQ(sim::countLines("NOTE a beam sees light again"), 1);
  CHECK(logHas("FULL_OFF"));
  CHECK(logHas("SHELF_STUCK_OFF"));
  CHECK_EQ(tallies(), std::string("drops=0 reach_ins=0 trades=0"));
  breakBeam(kDrop, 100, 40);
  breakBeam(kShelf, 900, 500);
  run(3000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=1 trades=1"));
}

SCENARIO(power_save_loose_wire_with_unequal_receivers_counts_nothing) {
  if (!kPowerSave) return;
  // One receiver much slower than the other, so that their lead times
  // differ. Both are still read at the same moment, and a wire that opens
  // at any point of the cycle is seen by both looks or by neither.
  board().beamResponseUs[0] = 250;
  board().beamResponseUs[1] = 1300;
  bootToRunning();
  run(1000);
  for (int i = 0; i < 400; ++i) {
    board().emitterWireOpen = true;
    sim::beamsChanged();
    run(100.0 + (i % 7) * 0.37);                           // every phase of the cycle
    board().emitterWireOpen = false;
    sim::beamsChanged();
    run(150.0 + (i % 11) * 0.73);
  }
  run(3000);
  CHECK_EQ(tallies(), std::string("drops=0 reach_ins=0 trades=0"));
  CHECK(!logHas("] DROP "));
  CHECK(!logHas("] REACH_IN "));
}

SCENARIO(power_save_trade_partner_survives_a_wire_fault) {
  if (!kPowerSave) return;
  bootToRunning();
  breakBeam(kDrop, 100, 40);                               // a duck goes in
  run(3000);
  board().emitterWireOpen = true;                          // the wire is off for a second
  sim::beamsChanged();
  run(1000);
  CHECK_EQ(sim::countLines("WARN both beams went dark at the same moment"), 1);
  board().emitterWireOpen = false;
  sim::beamsChanged();
  run(3000);
  breakBeam(kShelf, 100, 500);                             // and one is taken out
  run(3000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=1 trades=1"));
}

SCENARIO(power_save_duck_and_hand_at_the_same_moment_both_count) {
  if (!kPowerSave) return;
  bootToRunning();
  // Fifty pairs that begin 0 to 9 ms apart: the pair is held back for as
  // long as both beams are dark, and counted when the duck has passed.
  for (int i = 0; i < 50; ++i) {
    const double at = 500.0 + i * 3000.0 + (i % 10) * 0.7;
    breakBeam(kDrop, at + (i % 10), 60);
    breakBeam(kShelf, at, 600);
  }
  run(500.0 + 50 * 3000.0 + 3000.0);
  CHECK_EQ(tallies(), std::string("drops=50 reach_ins=50 trades=50"));
  CHECK(!logHas("WARN"));
}

SCENARIO(power_save_duck_and_hand_close_together_both_count) {
  if (!kPowerSave) return;
  bootToRunning();
  // A duck, and a hand on the shelf 30 ms later (three looks apart): two
  // separate things, both counted. Only the very same look is held back.
  breakBeam(kDrop, 100, 200);
  breakBeam(kShelf, 130, 400);
  run(2000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=1 trades=1"));
  CHECK(!logHas("WARN"));
}

SCENARIO(power_save_beam_blocked_at_power_up_is_measured_when_it_clears) {
  if (!kPowerSave) return;
  board().beamBlocked[0] = true;                           // duck resting in the drop beam
  bootToRunning();
  CHECK(logHas("beam DROP  GPIO32: dark level LOW (100% of samples), blocked = LOW [auto], "
               "** NO LIGHT SEEN **"));
  run(10000);
  Meter before;
  before.start();
  run(10000);
  CHECK(before.high(cfg::PIN_EMIT_DROP) > 0.12);           // the default 1.5 ms lead
  block(kDrop, 100, false);                                // the duck is taken out
  run(3000);
  CHECK_EQ(sim::countLines("NOTE DROP receiver measured now: light after 2"), 1);
  Meter after;
  after.start();
  run(10000);
  CHECK(after.high(cfg::PIN_EMIT_DROP) < 0.10);            // now the measured, shorter one
  CHECK_EQ(tallies(), std::string("drops=0 reach_ins=0 trades=0"));
  breakBeam(kDrop, 100, 40);
  run(1000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
}

SCENARIO(power_save_screen_set_to_always_on_never_goes_dark) {
  if (!kPowerSave || kPeeks) return;
  bootToRunning();
  // Ten quiet minutes: the panel is lit all the time, the pages keep
  // changing, and the processor still sleeps between cycles.
  Meter m;
  m.start();
  int dark = 0, pictures = 0, last = -1;
  for (uint32_t t = 0; t < 600000; t += 250) {
    run(250);
    dark += !board().displayOn;
    if (sim::litPixels() != last) ++pictures;
    last = sim::litPixels();
  }
  CHECK_EQ(dark, 0);
  CHECK(pictures >= 20);
  CHECK(m.asleep() > 0.35);
  // The first press changes the mode straight away: there is no dark
  // screen to wake first.
  tap();
  run(cfg::BUTTON_DOUBLE_GAP_MS + 300);
  CHECK_EQ(sim::countLines("] MODE DIM"), 1);
  CHECK_EQ(sim::countLines("] WAKE "), 0);
  // Stealth still turns the screen off, and a duck is still counted.
  tap();
  run(cfg::BUTTON_DOUBLE_GAP_MS + 300 + cfg::NOTICE_BANNER_MS + 500);
  CHECK_EQ(sim::countLines("] MODE STEALTH"), 1);
  CHECK(!board().displayOn);
  breakBeam(kDrop, 100, 40);
  run(1000);
  CHECK_EQ(tallies(), std::string("drops=1 reach_ins=0 trades=0"));
  CHECK(!logHas("WARN"));
}

SCENARIO(power_save_press_just_before_a_peek_still_only_wakes) {
  if (!kPowerSave || !kPeeks) return;
  bootToRunning();
  // The screen is dark; PRG goes down 300 ms before the next peek would
  // light it. What counts is the screen at the moment of the press.
  const uint32_t firstPeekAt =
      cfg::SCREEN_STAY_ON_MS + cfg::SCREEN_PEEK_EVERY_MS - cfg::SCREEN_PEEK_ON_MS;
  run(firstPeekAt - 300 /* already running that long */ - 300 - 10);
  CHECK(!board().displayOn);
  tap();
  run(cfg::BUTTON_DOUBLE_GAP_MS + 300);
  CHECK_EQ(sim::countLines("] WAKE "), 1);
  CHECK(!logHas("] MODE"));
  CHECK(board().displayOn);
}

// ---- power cycles ----------------------------------------------------------

static void powerCycle1_countAndSave() {
  bootToRunning();
  for (int i = 0; i < 3; ++i) breakBeam(kDrop, 100.0 + i * 1000.0, 40);
  breakBeam(kShelf, 3500, 500);
  run(4500 + cfg::SAVE_QUIET_MS + 500);
  CHECK_EQ(sim::countLines("] SAVED "), 1);
  breakBeam(kDrop, 100, 40);                               // one more, 1 s before power is cut
  run(1000);
  CHECK_EQ(tallies(), std::string("drops=4 reach_ins=1 trades=1"));
}
static void powerCycle2_talliesAreBack() {
  bootToRunning();
  // The three saved drops are back. The fourth happened inside the save
  // delay and is lost: that is the price of sparing the flash.
  CHECK(logHas("RUNNING drops=3 reach_ins=1 trades=1"));
  run(1000);
  shot("80_tally_after_power_cycle");
}
static AddScenario add_powerCycle("tallies_survive_a_power_cycle",
                                  {powerCycle1_countAndSave, powerCycle2_talliesAreBack});

static void lastRun1_longRun() {
  bootToRunning();
  CHECK(logHas("LAST_RUN NO RECORD"));
  run(25 * 60000.0);                                       // 25 minutes
  CHECK_EQ(sim::countLines("] RUNTIME "), 2);
  CHECK(logHas("RUNTIME 0 h 20 min"));
  CHECK_EQ(nvs("run_cur"), 20u);
}
static void lastRun2_shownAfterRestart() {
  setup();
  run(cfg::SPLASH_MS + 300);
  CHECK(logHas("LAST_RUN 0 h 20 min"));
  shot("81_last_run");
  CHECK_EQ(nvs("run_prev"), 20u);
  CHECK_EQ(nvs("run_cur"), 0u);
  run(3 * 60000.0);                                        // dies after 3 minutes
  CHECK(!logHas("] RUNTIME "));
}
static void lastRun3_shortRunDidNotOverwrite() {
  setup();
  run(cfg::SPLASH_MS + 300);
  CHECK(logHas("LAST_RUN 0 h 20 min"));                    // a flat-battery restart loop is harmless
  run(125 * 60000.0);                                      // then a run of just over two hours
  CHECK(logHas("RUNTIME 2 h 0 min"));
}
static void lastRun4_newRecord() {
  setup();
  run(cfg::SPLASH_MS + 300);
  CHECK(logHas("LAST_RUN 2 h 0 min"));
}
static AddScenario add_lastRun("last_run_time_is_recorded_across_power_cycles",
                               {lastRun1_longRun, lastRun2_shownAfterRestart,
                                lastRun3_shortRunDidNotOverwrite, lastRun4_newRecord});

// ---- every screen, at every burn-in position --------------------------------

// Draws straight through ui::Screen (no main.cpp) and checks that moving
// the picture to each burn-in position never pushes a pixel off the glass:
// the number of lit pixels must stay the same.
SCENARIO(every_message_fits_the_screen_at_every_burn_in_shift) {
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, cfg::PIN_OLED_RST, cfg::PIN_OLED_SCL,
                                           cfg::PIN_OLED_SDA);
  oled.begin();
  ui::Screen screen(oled);

  struct Item {
    std::string name;
    std::function<void()> draw;
  };
  std::vector<Item> items;
  int serial = 0;
  auto addMessage = [&](const char* group, const phrases::TwoLines& t) {
    char name[64];
    std::snprintf(name, sizeof(name), "p%02d_%s", serial++, group);
    items.push_back({name, [&screen, t] { screen.message(t.line1, t.line2); }});
  };
  auto addTally = [&](const char* label, uint32_t value) {
    char name[64];
    std::snprintf(name, sizeof(name), "p%02d_tally_%lu", serial++, (unsigned long)value);
    items.push_back({name, [&screen, label, value] { screen.tally(label, value); }});
  };

  addMessage("splash", {phrases::kSplashTitle, phrases::kSplashHint});
  addMessage("call", phrases::kCallDrop);
  addMessage("call", phrases::kCallTake);
  addMessage("idle_full", phrases::kIdleFull);
  addMessage("idle_stuck", phrases::kIdleStuck);
  for (const auto& t : phrases::kDrop) addMessage("drop", t);
  for (const auto& t : phrases::kReachIn) addMessage("reach_in", t);
  for (const auto& t : phrases::kTrade) addMessage("trade", t);
  for (const auto& t : phrases::kFull) addMessage("full", t);
  for (const auto& t : phrases::kStuck) addMessage("stuck", t);
  for (const auto& t : {phrases::kModeNormal, phrases::kModeDim, phrases::kModeStealth,
                        phrases::kRestockOver, phrases::kResetDone, phrases::kResetCancel,
                        phrases::kBeamWarning}) {
    addMessage("notice", t);
  }
  addMessage("restock", {phrases::kRestockTitle, "BACK IN 2:59"});
  addMessage("last_run", {phrases::kLastRunTitle, "128 h 50 min"});
  addMessage("last_run", {phrases::kLastRunTitle, "NO RECORD"});
  for (const char* label : {phrases::kLabelDrops, phrases::kLabelReach, phrases::kLabelTrades}) {
    for (uint32_t value : {0u, 7u, 42u, 188u, 1888u, 28888u}) addTally(label, value);
  }
  items.push_back({"p_reset_bar", [&screen] {
                     screen.messageWithBar(phrases::kResetTitle, "IN 4", 35);
                   }});

  const int8_t shifts[][2] = {{0, 0}, {2, 2}, {-2, -2}, {2, -2}, {-2, 2},
                              {2, 0}, {-2, 0}, {0, 2}, {0, -2}};
  for (const Item& item : items) {
    int reference = -1;
    for (const auto& s : shifts) {
      screen.setShift(s[0], s[1]);
      item.draw();
      oled.sendBuffer();
      const int lit = sim::litPixels();
      if (reference < 0) {
        reference = lit;
        shot("all/" + item.name);
        CHECK(lit > 150);                                  // something was drawn
      } else if (lit != reference) {
        std::printf("    %s loses pixels at shift (%d,%d): %d vs %d\n", item.name.c_str(),
                    s[0], s[1], lit, reference);
        CHECK_EQ(lit, reference);
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string only = argc > 1 ? argv[1] : "";
  if (std::system((std::string("mkdir -p ") + kScreenDir + "all").c_str()) != 0) return 2;

  int passed = 0, failed = 0;
  for (const Scenario& sc : scenarios()) {
    if (!only.empty() && sc.name != only) continue;
    const std::string nvsFile = ".native_build/sim_" + sc.name + ".nvs";
    std::remove(nvsFile.c_str());
    bool ok = true;
    for (size_t phase = 0; phase < sc.phases.size() && ok; ++phase) {
      std::fflush(stdout);
      const pid_t pid = fork();
      if (pid == 0) {
        board().nvsFile = nvsFile;
        board().echoSerial = (std::getenv("SIM_ECHO") != nullptr);
        mini_test::failuresInCurrentTest() = 0;
        sc.phases[phase]();
        std::fflush(stdout);
        _exit(mini_test::failuresInCurrentTest() == 0 ? 0 : 1);
      }
      int status = 0;
      waitpid(pid, &status, 0);
      ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
      if (!ok && !WIFEXITED(status)) std::printf("    phase %zu crashed\n", phase + 1);
    }
    std::remove(nvsFile.c_str());
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", sc.name.c_str());
    ok ? ++passed : ++failed;
  }
  std::printf("\n%d scenarios: %d passed, %d failed\nscreen pictures: %s\n", passed + failed,
              passed, failed, kScreenDir);
  return failed;
}
