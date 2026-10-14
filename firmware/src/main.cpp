// Duck Box - "drop a duck / take a duck" tally box for a cabin door.
//
// This file is the application: it wires the hardware modules to the
// counting logic and decides what the screen and the candle LEDs show.
//
//   beams                  beam sensors -> timestamped "blocked / clear"
//     pulse, beam_sampler    POWER_SAVE on: emitters lit once per cycle
//     edge_capture           POWER_SAVE off: emitters always lit, interrupts
//   power, screen_policy   POWER_SAVE on: sleep between cycles, screen peeks
//   duck_logic             counting state machine (plain C++, unit tested)
//   button_decoder         PRG button -> press / double press / hold
//   candle_flicker, leds   flame simulation -> four PWM outputs
//   ui, phrases            screen layouts and every word shown
//   storage, save_timer    tallies and running time in flash (NVS)
//   radios                 LoRa chip asleep, WiFi / Bluetooth never started
//
// Life of the box after power-on:
//
//   Splash (3 s) --PRG held--> Beam test --tap--.
//       |                                       |
//       '--------------> "Last run" (3 s) <-----'
//                               |
//                            Running (for ever)
//
// Every pin and every tunable number is in include/config.h.

#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <U8g2lib.h>

#include "esp_system.h"

#include "beams.h"
#include "button_decoder.h"
#include "candle_flicker.h"
#include "config.h"
#include "duck_logic.h"
#include "leds.h"
#include "logic_config.h"
#include "phrases.h"
#include "power.h"
#include "radios.h"
#include "save_timer.h"
#include "screen_policy.h"
#include "storage.h"
#include "ui.h"

static_assert(cfg::LED_COUNT == duck::CandleFlicker::kLeds,
              "the candle simulation is written for exactly four LEDs");

namespace {

// ------------------------------------------------------------------ objects

// Full-buffer SSD1306 driver on the hardware I2C bus, pins from config.h.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_oled(
    cfg::DISPLAY_FLIP_180 ? U8G2_R2 : U8G2_R0,
    cfg::PIN_OLED_RST, cfg::PIN_OLED_SCL, cfg::PIN_OLED_SDA);
ui::Screen g_screen(g_oled);

duck::DuckLogic g_logic(makeLogicConfig());
duck::ButtonDecoder g_button(makeButtonConfig());
duck::CandleFlicker g_candle(makeCandleConfig());
duck::SaveTimer g_saveTimer(cfg::SAVE_QUIET_MS, cfg::SAVE_MAX_WAIT_MS);

// -------------------------------------------------------------------- state

enum class Stage : uint8_t { Splash, BeamTest, LastRun, Running };
Stage g_stage = Stage::Splash;
uint32_t g_stageStart = 0;

// What a short press of PRG cycles through.
enum class LightMode : uint8_t { Normal, Dim, Stealth };
LightMode g_mode = LightMode::Normal;

// Pages of the idle rotation.
enum class Page : uint8_t { CallDrop, CallTake, Full, Stuck, Drops, Plundered, Trades };
constexpr uint8_t kMaxIdlePages = 6;
uint8_t g_idleIndex = 0;
uint32_t g_idlePageStart = 0;

// A temporary full-screen message. "owner" notices (mode change, reset...)
// are shown even in Stealth and during restocking; event messages are for
// passers-by.
struct Banner {
  bool active = false;
  bool owner = false;
  phrases::TwoLines text{nullptr, nullptr};
  uint32_t start = 0;
  uint32_t duration = 0;
  bool hasFollowUp = false;  // idle page to show once the banner is over
  Page followUp = Page::CallDrop;
};
Banner g_banner;
uint32_t g_phraseTurn[5] = {0, 0, 0, 0, 0};  // drop, reach-in, trade, full, stuck

bool g_restocking = false;
uint32_t g_restockStart = 0;
bool g_resetHold = false;  // PRG is being held towards a tally reset

// Burn-in protection: the picture steps round this pattern (pixels).
const int8_t kShiftPattern[][2] = {
    {0, 0}, {1, 1}, {2, 0}, {1, -1}, {0, -2}, {-1, -1}, {-2, 0}, {-1, 1}, {0, 2},
};
constexpr uint8_t kShiftSteps = sizeof(kShiftPattern) / sizeof(kShiftPattern[0]);
uint8_t g_shiftIndex = 0;
uint32_t g_shiftStart = 0;

// Running time of this power-up, counted in whole minutes so that it keeps
// working after millis() wraps.
uint32_t g_runMinutes = 0;
uint32_t g_runMinuteMark = 0;
uint32_t g_runMinutesSaved = 0;
uint32_t g_lastRunMinutes = 0;

uint32_t g_ledStepAt = 0;
uint32_t g_uiRefreshedAt = 0;

// POWER_SAVE: when somebody last did something (event or button), and the
// state of the "peeks" the idle screen takes after that.
uint32_t g_lastActivityMs = 0;
bool g_peeking = false;
bool g_wasPeeking = false;
uint32_t g_peekNumber = 0;
// The idle rotation stands still while the screen is dark and during a
// peek (one page per peek, the next page at the next peek).
bool g_holdIdlePage = false;
// POWER_SAVE: the processor stays awake until this time (serial output is
// still leaving the chip).
uint32_t g_awakeUntilMs = 0;

duck::ScreenPolicy makeScreenPolicy() {
  duck::ScreenPolicy p;
  p.stayOnMs = cfg::SCREEN_STAY_ON_MS;
  p.peekEveryMs = cfg::SCREEN_PEEK_EVERY_MS;
  p.peekOnMs = cfg::SCREEN_PEEK_ON_MS;
  return p;
}

// Splash: has PRG been down long enough to mean "beam test"?
bool g_entryPressed = false;
uint32_t g_entryPressedAt = 0;

// Beam test mode.
struct BeamTest {
  uint32_t breaks[beams::kCount] = {0, 0};
  uint32_t drawnAt = 0;
  uint32_t lastActivity = 0;  // last beam change or button event
};
BeamTest g_test;
constexpr uint32_t kTestScreenMs = 100;

// Diagnostics already reported on serial.
uint32_t g_reportedRaises[beams::kCount] = {0, 0};
bool g_reportedStray[beams::kCount] = {false, false};
bool g_reportedMeasured[beams::kCount] = {true, true};   // set after start-up
bool g_reportedSuspect = false;
// The idle screen was dark when PRG went down (POWER_SAVE).
bool g_buttonWasDown = false;
bool g_pressOnDarkScreen = false;
// The panel was switched off: send the whole picture when it is next lit.
bool g_repaintAll = false;
uint32_t g_reportedResyncs = 0;
uint32_t g_reportedOverflows = 0;
uint32_t g_reportedEventsLost = 0;

// ------------------------------------------------------------------ helpers

bool buttonDown() { return digitalRead(cfg::PIN_BUTTON) == LOW; }

const char* beamName(uint8_t beam) { return beam == beams::kDrop ? "DROP" : "SHELF"; }

// Start of a serial log line: "[  123.456] ".
void logStamp(uint32_t ms) {
  // Sleeping cuts a line that is still being sent into pieces (nothing is
  // lost, but it arrives in bursts). Stay awake until it has gone.
  g_awakeUntilMs = (uint32_t)millis() + 40u;
  Serial.printf("[%5lu.%03lu] ", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
}

void logCounters() {
  const duck::Counters& c = g_logic.counters();
  Serial.printf("drops=%lu reach_ins=%lu trades=%lu", (unsigned long)c.drops,
                (unsigned long)c.reachIns, (unsigned long)c.trades);
}

// ------------------------------------------------------------------ display

// One complete picture, described by value so that "has anything changed?"
// is a simple comparison. The OLED is only redrawn (about 25 ms of I2C
// traffic) when the answer is yes.
struct Frame {
  enum class Kind : uint8_t { Message, MessageBar, Tally };
  Kind kind = Kind::Message;
  char line1[24] = "";
  char line2[24] = "";
  uint32_t value = 0;
  uint8_t percent = 0;
  int8_t dx = 0;
  int8_t dy = 0;
};

bool sameFrame(const Frame& a, const Frame& b) {
  return a.kind == b.kind && a.value == b.value && a.percent == b.percent &&
         a.dx == b.dx && a.dy == b.dy && strcmp(a.line1, b.line1) == 0 &&
         strcmp(a.line2, b.line2) == 0;
}

Frame messageFrame(const char* line1, const char* line2) {
  Frame f;
  f.kind = Frame::Kind::Message;
  snprintf(f.line1, sizeof(f.line1), "%s", line1 ? line1 : "");
  snprintf(f.line2, sizeof(f.line2), "%s", line2 ? line2 : "");
  return f;
}

Frame messageFrame(const phrases::TwoLines& text) {
  return messageFrame(text.line1, text.line2);
}

Frame barFrame(const char* line1, const char* line2, uint8_t percent) {
  Frame f = messageFrame(line1, line2);
  f.kind = Frame::Kind::MessageBar;
  f.percent = percent;
  return f;
}

Frame tallyFrame(const char* label, uint32_t value) {
  Frame f = messageFrame(label, "");
  f.kind = Frame::Kind::Tally;
  f.value = value;
  return f;
}

bool g_displayOn = true;       // U8g2 begin() leaves the panel switched on
int16_t g_contrast = -1;       // -1 = not set yet
bool g_frameValid = false;     // g_lastFrame is what the panel shows
Frame g_lastFrame;

// POWER_SAVE: a picture goes to the panel one strip (8 pixel rows, a few
// milliseconds of I2C traffic) per cycle instead of all at once (about
// 25 ms), so that the beams are never left unwatched for long. Only strips
// that differ from what the panel already holds are sent: g_panelCopy is a
// copy of the panel's memory, g_dirtyStrips has a bit for every strip of
// the frame buffer that differs from it.
constexpr uint8_t kStrips = 8;
constexpr uint8_t kStripTiles = 16;
constexpr uint16_t kStripBytes = 128;
uint8_t g_panelCopy[kStrips * kStripBytes];
uint8_t g_dirtyStrips = 0;
uint8_t g_nextStrip = 0;        // strips take turns, top to bottom
bool g_panelWanted = true;
uint8_t g_panelWaitCycles = 0;

// A new picture is in the frame buffer: which strips have to go out?
void findDirtyStrips() {
  const uint8_t* buffer = g_oled.getBufferPtr();
  g_dirtyStrips = 0;
  for (uint8_t r = 0; r < kStrips; ++r) {
    if (memcmp(buffer + r * kStripBytes, g_panelCopy + r * kStripBytes, kStripBytes) != 0) {
      g_dirtyStrips |= (uint8_t)(1u << r);
    }
  }
}

// The whole frame buffer was just sent with sendBuffer().
void panelHasWholeBuffer() {
  memcpy(g_panelCopy, g_oled.getBufferPtr(), sizeof(g_panelCopy));
  g_dirtyStrips = 0;
}

// Send the next strip that needs it, if any. Called once per cycle. A
// picture that changes while it is being sent (the reset countdown) does
// not start again from the top: the strips simply keep taking turns.
void pumpScreen() {
  for (uint8_t i = 0; i < kStrips && g_dirtyStrips != 0; ++i) {
    const uint8_t r = (uint8_t)((g_nextStrip + i) % kStrips);
    if ((g_dirtyStrips & (1u << r)) == 0) continue;
    g_oled.updateDisplayArea(0, r, kStripTiles, 1);
    memcpy(g_panelCopy + r * kStripBytes, g_oled.getBufferPtr() + r * kStripBytes, kStripBytes);
    g_dirtyStrips &= (uint8_t)~(1u << r);
    g_nextStrip = (uint8_t)((r + 1) % kStrips);
    break;
  }
  if (g_panelWanted && !g_displayOn) {
    // Light the panel once the picture is complete (or, should a picture
    // keep changing faster than it can be sent, after a third of a second).
    if (g_dirtyStrips == 0 || ++g_panelWaitCycles > 30) {
      g_oled.setPowerSave(0);
      g_displayOn = true;
    }
  }
}

// Put a frame on the panel. `on` = false switches the panel off instead
// (Stealth, or between peeks). The new picture is always sent before the
// panel is switched back on, so that it never flashes stale content.
// `atOnce` sends the whole picture now even with POWER_SAVE on.
void present(Frame frame, bool on, uint8_t contrast, bool atOnce = false) {
  if (on && !g_panelWanted) g_panelWaitCycles = 0;
  g_panelWanted = on;
  if (!on) {
    g_frameValid = false;
    if (g_displayOn) {
      g_oled.setPowerSave(1);
      g_displayOn = false;
      // Nothing ever tells us whether a strip arrived. Sending the whole
      // picture each time the panel is lit again puts right whatever may
      // have gone wrong since.
      g_repaintAll = true;
    }
    return;
  }

  if ((int16_t)contrast != g_contrast) {
    g_oled.setContrast(contrast);
    g_contrast = contrast;
  }

  frame.dx = kShiftPattern[g_shiftIndex][0];
  frame.dy = kShiftPattern[g_shiftIndex][1];
  if (!g_frameValid || !sameFrame(frame, g_lastFrame)) {
    g_screen.setShift(frame.dx, frame.dy);
    switch (frame.kind) {
      case Frame::Kind::Message:
        g_screen.message(frame.line1, frame.line2);
        break;
      case Frame::Kind::MessageBar:
        g_screen.messageWithBar(frame.line1, frame.line2, frame.percent);
        break;
      case Frame::Kind::Tally:
        g_screen.tally(frame.line1, frame.value);
        break;
    }
    if (cfg::POWER_SAVE && !atOnce) {
      findDirtyStrips();           // pumpScreen() sends them
      if (g_repaintAll) g_dirtyStrips = 0xFF;
      g_repaintAll = false;
    } else {
      g_oled.sendBuffer();
      panelHasWholeBuffer();
    }
    g_lastFrame = frame;
    g_frameValid = true;
  }

  if (g_dirtyStrips == 0 && !g_displayOn) {
    g_oled.setPowerSave(0);
    g_displayOn = true;
  }
}

// --------------------------------------------------------------- banners

void showBanner(const phrases::TwoLines& text, uint32_t durationMs, bool owner,
                uint32_t nowMs) {
  g_banner.active = true;
  g_banner.owner = owner;
  g_banner.text = text;
  g_banner.start = nowMs;
  g_banner.duration = durationMs;
  g_banner.hasFollowUp = false;
}

void showNotice(const phrases::TwoLines& text, uint32_t nowMs) {
  showBanner(text, cfg::NOTICE_BANNER_MS, /*owner=*/true, nowMs);
}

void showEvent(const phrases::TwoLines& text, Page followUp, uint32_t nowMs) {
  showBanner(text, cfg::EVENT_BANNER_MS, /*owner=*/false, nowMs);
  g_banner.hasFollowUp = true;
  g_banner.followUp = followUp;
}

// ------------------------------------------------------------ idle pages

// The idle rotation: a call to action, then the tally it leads to. While
// the box is full or a duck is parked in the shelf beam, the matching call
// to action is replaced by the warning (and a full box says so twice).
uint8_t buildIdlePages(Page* pages) {
  const bool full = g_logic.isFull();
  const bool stuck = g_logic.isShelfStuck();
  uint8_t n = 0;
  pages[n++] = full ? Page::Full : Page::CallDrop;
  pages[n++] = Page::Drops;
  pages[n++] = stuck ? Page::Stuck : Page::CallTake;
  pages[n++] = Page::Plundered;
  if (full) pages[n++] = Page::Full;
  pages[n++] = Page::Trades;
  return n;
}

Frame idleFrame(Page page) {
  const duck::Counters& c = g_logic.counters();
  switch (page) {
    case Page::CallDrop:  return messageFrame(phrases::kCallDrop);
    case Page::CallTake:  return messageFrame(phrases::kCallTake);
    case Page::Full:      return messageFrame(phrases::kIdleFull);
    case Page::Stuck:     return messageFrame(phrases::kIdleStuck);
    case Page::Drops:     return tallyFrame(phrases::kLabelDrops, c.drops);
    case Page::Plundered: return tallyFrame(phrases::kLabelReach, c.reachIns);
    case Page::Trades:    return tallyFrame(phrases::kLabelTrades, c.trades);
  }
  return messageFrame(phrases::kCallDrop);
}

// Restart the rotation at `page` (if it is in the current rotation).
void jumpToIdlePage(Page page, uint32_t nowMs) {
  Page pages[kMaxIdlePages];
  const uint8_t n = buildIdlePages(pages);
  for (uint8_t i = 0; i < n; ++i) {
    if (pages[i] == page) {
      g_idleIndex = i;
      break;
    }
  }
  g_idlePageStart = nowMs;
}

// ------------------------------------------------------------ persistence

void saveCountersNow() {
  storage::saveCounters(g_logic.counters());
  g_saveTimer.markSaved();
}

void saveIfDue(uint32_t nowMs) {
  if (!g_saveTimer.due(nowMs)) return;
  saveCountersNow();
  logStamp(nowMs);
  Serial.print("SAVED ");
  logCounters();
  Serial.println();
}

void updateRunTime(uint32_t nowMs) {
  while ((uint32_t)(nowMs - g_runMinuteMark) >= 60000u) {
    g_runMinuteMark += 60000u;
    ++g_runMinutes;
  }
  const uint32_t interval = cfg::RUNTIME_SAVE_MS / 60000u;
  if (g_runMinutes - g_runMinutesSaved >= (interval ? interval : 1)) {
    g_runMinutesSaved = g_runMinutes;
    storage::saveCurrentRunMinutes(g_runMinutes);
    logStamp(nowMs);
    Serial.printf("RUNTIME %lu h %lu min\n", (unsigned long)(g_runMinutes / 60),
                  (unsigned long)(g_runMinutes % 60));
  }
}

// ---------------------------------------------------------------- stages

void enterRunning(uint32_t nowMs);

// "LAST RUN 12 h 30 min". Entering this stage is also the moment the
// previous run's record is closed (see storage.h); beam test mode comes
// before it and therefore never writes to flash.
void enterLastRun(uint32_t nowMs) {
  const uint32_t ended = storage::loadCurrentRunMinutes();
  if (ended > 0) {
    storage::savePreviousRunMinutes(ended);
    storage::saveCurrentRunMinutes(0);
  }
  g_lastRunMinutes = storage::loadPreviousRunMinutes();

  char text[24];
  phrases::formatRunTime(text, sizeof(text), g_lastRunMinutes);
  logStamp(nowMs);
  Serial.printf("LAST_RUN %s\n", text);

  g_stage = Stage::LastRun;
  g_stageStart = nowMs;
  g_frameValid = false;
  present(messageFrame(phrases::kLastRunTitle, text), true, cfg::OLED_CONTRAST_NORMAL,
          /*atOnce=*/true);
}

void enterBeamTest(uint32_t nowMs) {
  g_stage = Stage::BeamTest;
  g_stageStart = nowMs;
  g_test = BeamTest();
  g_test.lastActivity = nowMs;
  beams::discardPending();
  // PRG is still down from entering; that press must not count as "exit".
  g_button.reset(true, nowMs);

  logStamp(nowMs);
  Serial.println("BEAM_TEST start: tap PRG to leave, hold PRG 1 s to re-learn "
                 "polarity (beams clear). Nothing is saved in this mode.");
}

void exitBeamTest(uint32_t nowMs) {
  digitalWrite(cfg::PIN_BOARD_LED, LOW);
  leds::allOff();
  logStamp(nowMs);
  Serial.printf("BEAM_TEST end: drop breaks=%lu shelf breaks=%lu\n",
                (unsigned long)g_test.breaks[beams::kDrop],
                (unsigned long)g_test.breaks[beams::kShelf]);
  enterLastRun(nowMs);
}

void printPolarity() {
  for (uint8_t b = 0; b < beams::kCount; ++b) {
    const beams::Polarity& p = beams::polarity(b);
    if (cfg::POWER_SAVE) {
      Serial.printf("  beam %-5s GPIO%u: dark level %s (%u%% of samples), blocked = %s [%s]%s, ",
                    beamName(b),
                    (unsigned)(b == beams::kDrop ? cfg::PIN_BEAM_DROP : cfg::PIN_BEAM_SHELF),
                    p.idleLevel ? "HIGH" : "LOW", (unsigned)p.agreePct,
                    p.blockedLevel ? "HIGH" : "LOW", p.forced ? "config.h" : "auto",
                    p.unstable ? "  ** UNSTABLE **" : "");
      if (p.lightSeen) {
        Serial.printf("light seen after %lu us, emitter lead %lu us\n",
                      (unsigned long)p.responseUs, (unsigned long)beams::settleUs(b));
      } else {
        Serial.println("** NO LIGHT SEEN ** (blocked, misaligned, or emitter not on its pin?)");
      }
      continue;
    }
    Serial.printf("  beam %-5s GPIO%u: idle level %s (%u%% of samples), blocked = %s [%s]%s\n",
                  beamName(b),
                  (unsigned)(b == beams::kDrop ? cfg::PIN_BEAM_DROP : cfg::PIN_BEAM_SHELF),
                  p.idleLevel ? "HIGH" : "LOW", (unsigned)p.agreePct,
                  p.blockedLevel ? "HIGH" : "LOW", p.forced ? "config.h" : "auto",
                  p.unstable ? "  ** UNSTABLE **" : "");
  }
  if (beams::polarityDoubtful()) {
    if (cfg::POWER_SAVE) {
      Serial.println("  ** WARNING: check the beams. A receiver did not answer its emitter, "
                     "or its level is not what config.h says. See README, \"Beam check\".");
    } else {
      Serial.println("  ** WARNING: beam polarity is doubtful. Was a beam blocked or "
                     "misaligned at power-up? See README, \"Beam polarity\".");
    }
  }
}

void enterRunning(uint32_t nowMs) {
  // Start from the levels as they are now: a beam that is already blocked
  // is not a fresh duck.
  beams::discardPending();
  g_logic.begin(nowMs, beams::isBlocked(beams::kDrop), beams::isBlocked(beams::kShelf));
  g_button.reset(buttonDown(), nowMs);

  g_stage = Stage::Running;
  g_stageStart = nowMs;
  g_idleIndex = 0;
  g_idlePageStart = nowMs;
  g_shiftStart = nowMs;
  g_banner = Banner();
  g_restocking = false;
  g_resetHold = false;
  g_frameValid = false;
  g_lastActivityMs = nowMs;
  g_peeking = false;
  g_wasPeeking = false;
  g_holdIdlePage = false;

  if (beams::polarityDoubtful()) {
    showBanner(phrases::kBeamWarning, cfg::BEAM_WARNING_MS, /*owner=*/true, nowMs);
  }

  logStamp(nowMs);
  Serial.print("RUNNING ");
  logCounters();
  Serial.println();
}

// ---------------------------------------------------------- stage: splash

void loopSplash(uint32_t nowMs) {
  beams::discardPending();  // nobody is counting yet
  // PRG seen down for a moment during the splash = beam test mode.
  if (buttonDown()) {
    if (!g_entryPressed) {
      g_entryPressed = true;
      g_entryPressedAt = nowMs;
    } else if ((uint32_t)(nowMs - g_entryPressedAt) >= cfg::TEST_ENTRY_HOLD_MS) {
      enterBeamTest(nowMs);
      return;
    }
  } else {
    g_entryPressed = false;
  }

  if ((uint32_t)(nowMs - g_stageStart) >= cfg::SPLASH_MS) enterLastRun(nowMs);
}

// ------------------------------------------------------- stage: last run

void loopLastRun(uint32_t nowMs) {
  beams::discardPending();
  if ((uint32_t)(nowMs - g_stageStart) >= cfg::LAST_RUN_MS) enterRunning(nowMs);
}

// ------------------------------------------------------ stage: beam test

uint8_t testLedIndex(uint32_t nowMs) {
  return (uint8_t)(((uint32_t)(nowMs - g_stageStart) / cfg::TEST_LED_STEP_MS) %
                   cfg::LED_COUNT);
}

void loopBeamTest(uint32_t nowMs) {
  // Every change of either beam, straight from the interrupt capture, so
  // that even a very short break shows up in the counts.
  beams::Change c;
  while (beams::next(c, nowMs)) {
    if (c.blocked) ++g_test.breaks[c.beam];
    g_test.lastActivity = nowMs;
    logStamp(c.ms);
    Serial.printf("TEST %s beam %s\n", beamName(c.beam), c.blocked ? "BLOCKED" : "clear");
  }

  const bool dropBlocked = beams::isBlocked(beams::kDrop);
  const bool shelfBlocked = beams::isBlocked(beams::kShelf);
  // The small white LED on the board lights while either beam is blocked:
  // handy when aligning a sensor without watching the screen.
  digitalWrite(cfg::PIN_BOARD_LED, (dropBlocked || shelfBlocked) ? HIGH : LOW);

  switch (g_button.update(buttonDown(), nowMs)) {
    case duck::ButtonEvent::SinglePress:
    case duck::ButtonEvent::DoublePress:
      exitBeamTest(nowMs);
      return;
    case duck::ButtonEvent::HoldStart:
      logStamp(nowMs);
      Serial.println("TEST re-learning polarity (both beams must be clear)");
      beams::detectPolarity();
      printPolarity();
      for (uint8_t b = 0; b < beams::kCount; ++b) {
        g_reportedMeasured[b] = beams::polarity(b).lightSeen;
      }
      g_test.breaks[beams::kDrop] = 0;
      g_test.breaks[beams::kShelf] = 0;
      g_test.lastActivity = nowMs;
      break;
    default:
      break;
  }

  // Left alone for a long time: assume it was entered by accident.
  if ((uint32_t)(nowMs - g_test.lastActivity) >= cfg::BEAM_TEST_TIMEOUT_MS) {
    logStamp(nowMs);
    Serial.println("TEST nothing happened for a long time, leaving beam test");
    exitBeamTest(nowMs);
    return;
  }

  if ((uint32_t)(nowMs - g_test.drawnAt) >= kTestScreenMs) {
    g_test.drawnAt = nowMs;
    ui::BeamTestView view;
    const bool blocked[beams::kCount] = {dropBlocked, shelfBlocked};
    ui::BeamRow* rows[beams::kCount] = {&view.drop, &view.shelf};
    for (uint8_t b = 0; b < beams::kCount; ++b) {
      const beams::Polarity& p = beams::polarity(b);
      rows[b]->blocked = blocked[b];
      rows[b]->rawLevel = beams::rawLevel(b);
      rows[b]->blockedLevel = p.blockedLevel;
      rows[b]->forced = p.forced;
      rows[b]->unstable = p.unstable;
      rows[b]->breaks = g_test.breaks[b];
    }
    const uint8_t led = testLedIndex(nowMs);
    view.ledNumber = (uint8_t)(led + 1);
    view.ledPin = cfg::PIN_LED[led];
    g_screen.beamTest(view);
    if (cfg::POWER_SAVE) {
      findDirtyStrips();       // sent strip by strip, see pumpScreen()
    } else {
      g_oled.sendBuffer();
      panelHasWholeBuffer();
    }
  }
}

// -------------------------------------------------------- stage: running

void startRestock(uint32_t nowMs) {
  g_restocking = true;
  g_restockStart = nowMs;
  g_logic.setPaused(true, nowMs);
  g_banner.active = false;
  logStamp(nowMs);
  Serial.printf("RESTOCK_START counting paused for %lu s\n",
                (unsigned long)(cfg::RESTOCK_PAUSE_MS / 1000));
}

void endRestock(uint32_t nowMs, bool early) {
  g_restocking = false;
  g_logic.setPaused(false, nowMs);
  showNotice(phrases::kRestockOver, nowMs);
  logStamp(nowMs);
  Serial.printf("RESTOCK_END %s\n", early ? "(ended by double press)" : "(time up)");
}

void cycleLightMode(uint32_t nowMs) {
  const char* name = "";
  switch (g_mode) {
    case LightMode::Normal:
      g_mode = LightMode::Dim;
      showNotice(phrases::kModeDim, nowMs);
      name = "DIM";
      break;
    case LightMode::Dim:
      g_mode = LightMode::Stealth;
      showNotice(phrases::kModeStealth, nowMs);
      name = "STEALTH";
      break;
    case LightMode::Stealth:
      g_mode = LightMode::Normal;
      showNotice(phrases::kModeNormal, nowMs);
      name = "NORMAL";
      break;
  }
  logStamp(nowMs);
  Serial.printf("MODE %s\n", name);
}

void handleButton(uint32_t nowMs) {
  // POWER_SAVE: while the idle screen is dark, a short press only lights
  // it; the next one changes the mode. (In Stealth the screen is dark on
  // purpose, and a press moves on to Normal as before.) What counts is the
  // screen at the moment the button went down.
  const bool down = buttonDown();
  if (down && !g_buttonWasDown) {
    g_pressOnDarkScreen =
        cfg::POWER_SAVE && !g_panelWanted && g_mode != LightMode::Stealth;
  }
  g_buttonWasDown = down;
  const duck::ButtonEvent event = g_button.update(down, nowMs);
  const bool screenAsleep = g_pressOnDarkScreen;
  if (event != duck::ButtonEvent::None) g_lastActivityMs = nowMs;
  switch (event) {
    case duck::ButtonEvent::SinglePress:
      if (screenAsleep) {
        logStamp(nowMs);
        Serial.println("WAKE screen lit (press again to change the mode)");
      } else {
        cycleLightMode(nowMs);
      }
      break;

    case duck::ButtonEvent::DoublePress:
      if (g_restocking) endRestock(nowMs, /*early=*/true);
      else startRestock(nowMs);
      break;

    case duck::ButtonEvent::HoldStart:
      g_resetHold = true;
      break;

    case duck::ButtonEvent::HoldCancel:
      g_resetHold = false;
      showNotice(phrases::kResetCancel, nowMs);
      logStamp(nowMs);
      Serial.println("RESET_CANCELLED");
      break;

    case duck::ButtonEvent::HoldComplete:
      g_resetHold = false;
      g_logic.resetCounters();
      saveCountersNow();
      showNotice(phrases::kResetDone, nowMs);
      logStamp(nowMs);
      Serial.println("RESET all tallies set to zero");
      break;

    case duck::ButtonEvent::None:
      break;
  }
}

// One event from the counting logic: log it, and tell the passers-by.
void handleEvent(const duck::Event& e, uint32_t nowMs) {
  g_lastActivityMs = nowMs;
  logStamp(e.timeMs);
  Serial.print(duck::eventName(e.type));
  Serial.print(' ');
  logCounters();
  Serial.println();

  switch (e.type) {
    case duck::EventType::Drop:
      showEvent(phrases::pick(phrases::kDrop, g_phraseTurn[0]++), Page::Drops, nowMs);
      g_candle.flare();
      g_saveTimer.markDirty(nowMs);
      break;
    case duck::EventType::ReachIn:
      showEvent(phrases::pick(phrases::kReachIn, g_phraseTurn[1]++), Page::Plundered, nowMs);
      g_candle.flare();
      g_saveTimer.markDirty(nowMs);
      break;
    case duck::EventType::Trade:
      // Arrives straight after the drop or reach-in that completed it and
      // takes over the screen from that message.
      showEvent(phrases::pick(phrases::kTrade, g_phraseTurn[2]++), Page::Trades, nowMs);
      g_candle.flare();
      g_saveTimer.markDirty(nowMs);
      break;
    case duck::EventType::FullOn:
      showEvent(phrases::pick(phrases::kFull, g_phraseTurn[3]++), Page::Full, nowMs);
      break;
    case duck::EventType::ShelfStuckOn:
      showEvent(phrases::pick(phrases::kStuck, g_phraseTurn[4]++), Page::Stuck, nowMs);
      break;
    case duck::EventType::FullOff:
    case duck::EventType::ShelfStuckOff:
      break;  // the warning page simply drops out of the idle rotation
  }
}

void reportDiagnostics(uint32_t nowMs) {
  if (cfg::POWER_SAVE) {
    for (uint8_t b = 0; b < beams::kCount; ++b) {
      if (beams::raiseCount(b) != g_reportedRaises[b]) {
        g_reportedRaises[b] = beams::raiseCount(b);
        logStamp(nowMs);
        Serial.printf("NOTE %s receiver is slower than assumed (late %lu times): "
                      "emitter lead raised to %lu us\n",
                      beamName(b), (unsigned long)beams::lateCount(b),
                      (unsigned long)beams::settleUs(b));
      }
      if (beams::strayLight(b) != g_reportedStray[b]) {
        g_reportedStray[b] = beams::strayLight(b);
        logStamp(nowMs);
        if (g_reportedStray[b]) {
          Serial.printf("WARN %s receiver sees light while its emitter is dark "
                        "(bright light, or OUT wire loose)\n", beamName(b));
          showBanner(phrases::kBeamWarning, cfg::BEAM_WARNING_MS, /*owner=*/true, nowMs);
        } else {
          Serial.printf("NOTE %s receiver is dark between looks again\n", beamName(b));
        }
      }
      if (beams::polarity(b).lightSeen != g_reportedMeasured[b]) {
        g_reportedMeasured[b] = beams::polarity(b).lightSeen;
        logStamp(nowMs);
        Serial.printf("NOTE %s receiver measured now: light after %lu us, emitter lead %lu us\n",
                      beamName(b), (unsigned long)beams::polarity(b).responseUs,
                      (unsigned long)beams::settleUs(b));
      }
    }
    if (beams::emittersSuspect() != g_reportedSuspect) {
      g_reportedSuspect = beams::emittersSuspect();
      logStamp(nowMs);
      if (g_reportedSuspect) {
        Serial.println("WARN both beams went dark at the same moment: emitter wire loose? "
                       "Nothing counted.");
        showBanner(phrases::kBeamWarning, cfg::BEAM_WARNING_MS, /*owner=*/true, nowMs);
      } else {
        Serial.println("NOTE a beam sees light again");
      }
    }
  }
  if (beams::resyncCount() != g_reportedResyncs) {
    g_reportedResyncs = beams::resyncCount();
    logStamp(nowMs);
    Serial.printf("WARN beam level re-read by polling (%lu so far)\n",
                  (unsigned long)g_reportedResyncs);
  }
  if (beams::overflowCount() != g_reportedOverflows) {
    g_reportedOverflows = beams::overflowCount();
    logStamp(nowMs);
    Serial.printf("WARN edge buffer overflowed (%lu edges dropped so far) - noisy beam?\n",
                  (unsigned long)g_reportedOverflows);
  }
  if (g_logic.eventsLost() != g_reportedEventsLost) {
    g_reportedEventsLost = g_logic.eventsLost();
    logStamp(nowMs);
    Serial.printf("WARN %lu event messages skipped (tallies are not affected)\n",
                  (unsigned long)g_reportedEventsLost);
  }
}

// Decide what the screen shows right now, most urgent first.
Frame chooseFrame(uint32_t nowMs) {
  if (g_resetHold) {
    const uint32_t total = cfg::BUTTON_HOLD_RESET_MS;
    uint32_t held = g_button.heldMs(nowMs);
    if (held > total) held = total;
    char text[24];
    phrases::formatResetCountdown(text, sizeof(text), total - held);
    return barFrame(phrases::kResetTitle, text, (uint8_t)(held * 100u / total));
  }

  if (g_banner.active && g_banner.owner) return messageFrame(g_banner.text);

  if (g_restocking) {
    const uint32_t elapsed = (uint32_t)(nowMs - g_restockStart);
    const uint32_t left = elapsed >= cfg::RESTOCK_PAUSE_MS ? 0 : cfg::RESTOCK_PAUSE_MS - elapsed;
    char text[24];
    phrases::formatRestock(text, sizeof(text), left);
    return messageFrame(phrases::kRestockTitle, text);
  }

  if (g_banner.active) return messageFrame(g_banner.text);

  Page pages[kMaxIdlePages];
  const uint8_t n = buildIdlePages(pages);
  // A peek shows one page from start to end; refreshScreen() moves on to
  // the next page when the next peek begins.
  if (g_holdIdlePage) {
    g_idlePageStart = nowMs;
  } else if ((uint32_t)(nowMs - g_idlePageStart) >= cfg::IDLE_PAGE_MS) {
    g_idlePageStart = nowMs;
    ++g_idleIndex;
  }
  if (g_idleIndex >= n) g_idleIndex = 0;
  return idleFrame(pages[g_idleIndex]);
}

void refreshScreen(uint32_t nowMs) {
  if ((uint32_t)(nowMs - g_shiftStart) >= cfg::BURNIN_SHIFT_MS) {
    g_shiftStart = nowMs;
    g_shiftIndex = (uint8_t)((g_shiftIndex + 1) % kShiftSteps);
  }

  // Stealth keeps the panel dark, except while the owner is being told
  // something (mode change, reset countdown, restocking).
  const bool ownerScreen = g_resetHold || g_restocking || (g_banner.active && g_banner.owner);
  bool on = (g_mode != LightMode::Stealth) || ownerScreen;

  // POWER_SAVE: with nothing to announce, the screen is only lit for a
  // while after the last activity and then in short peeks.
  g_peeking = false;
  g_holdIdlePage = false;
  if (cfg::POWER_SAVE && on && !ownerScreen && !g_banner.active) {
    static const duck::ScreenPolicy policy = makeScreenPolicy();
    const duck::ScreenState st =
        duck::screenState(policy, (uint32_t)(nowMs - g_lastActivityMs));
    on = st.on;
    g_peeking = st.peeking;
    g_holdIdlePage = st.peeking || !st.on;
    if (st.peeking && (!g_wasPeeking || st.peek != g_peekNumber)) {
      g_peekNumber = st.peek;      // a new peek: the next page of the rotation
      ++g_idleIndex;
      g_idlePageStart = nowMs;
    }
  }
  g_wasPeeking = g_peeking;

  const uint8_t contrast =
      (g_mode == LightMode::Dim) ? cfg::OLED_CONTRAST_DIM : cfg::OLED_CONTRAST_NORMAL;
  present(chooseFrame(nowMs), on, contrast);
}

void loopRunning(uint32_t nowMs) {
  // 1. Beam changes recorded by the interrupt since the last pass, with
  //    their true times, then let time-based rules (FULL etc.) catch up.
  beams::Change c;
  while (beams::next(c, nowMs)) {
    const duck::Beam beam = c.beam == beams::kDrop ? duck::Beam::Drop : duck::Beam::Shelf;
    if (c.uncounted) {
      g_logic.noteLevel(beam, c.blocked, c.ms);   // the level, but no count (see beams.h)
    } else {
      g_logic.onBeamEdge(beam, c.blocked, c.ms);
    }
  }
  g_logic.tick(nowMs);

  // 2. What that produced.
  duck::Event e;
  while (g_logic.popEvent(e)) handleEvent(e, nowMs);

  // 3. The owner.
  handleButton(nowMs);
  if (g_restocking && (uint32_t)(nowMs - g_restockStart) >= cfg::RESTOCK_PAUSE_MS) {
    endRestock(nowMs, /*early=*/false);
  }

  // 4. Housekeeping.
  if (g_banner.active && (uint32_t)(nowMs - g_banner.start) >= g_banner.duration) {
    g_banner.active = false;
    if (g_banner.hasFollowUp) jumpToIdlePage(g_banner.followUp, nowMs);
  }
  updateRunTime(nowMs);
  saveIfDue(nowMs);
  reportDiagnostics(nowMs);

  // 5. Screen (the slow part; beam edges keep being captured meanwhile).
  if ((uint32_t)(nowMs - g_uiRefreshedAt) >= cfg::UI_REFRESH_MS) {
    g_uiRefreshedAt = nowMs;
    refreshScreen(nowMs);
  }
}

// ------------------------------------------------------------------- LEDs

void updateLeds(uint32_t nowMs) {
  if ((uint32_t)(nowMs - g_ledStepAt) < cfg::LED_STEP_MS) return;
  g_ledStepAt = nowMs;

  if (g_stage == Stage::BeamTest) {
    // Wiring check: LED1, LED2, LED3, LED4 one after another.
    const uint8_t lit = testLedIndex(nowMs);
    for (uint8_t i = 0; i < cfg::LED_COUNT; ++i) {
      leds::set(i, i == lit ? cfg::TEST_LED_BRIGHTNESS : 0.0f);
    }
    return;
  }

  g_candle.step();
  float scale = 1.0f;
  if (g_mode == LightMode::Dim) scale = cfg::LED_DIM_FACTOR;
  if (g_mode == LightMode::Stealth) scale = 0.0f;
  for (uint8_t i = 0; i < cfg::LED_COUNT; ++i) {
    leds::set(i, g_candle.level(i) * scale);
  }
}

// ------------------------------------------------------------ power report

// POWER_SAVE: once a minute, how the battery was spent. See README,
// "Power", for turning these shares into milliamps.
void reportPower(uint32_t nowMs) {
  power::Report r;
  if (!power::report(nowMs, r)) return;
  logStamp(nowMs);
  // Kept under 120 characters: a longer line does not fit the serial
  // port's buffer in one go and would hold the loop up while it drains.
  const unsigned emit = r.emitterPct[0] > r.emitterPct[1] ? r.emitterPct[0] : r.emitterPct[1];
  Serial.printf("POWER awake=%u%% emit=%u%% leds=%u%% screen=%u%% sleeps=%lu refused=%lu "
                "slowest=%lums lead=%lu/%luus\n",
                (unsigned)r.awakePct, emit, (unsigned)r.ledPct, (unsigned)r.screenPct,
                (unsigned long)r.sleeps, (unsigned long)r.refused,
                (unsigned long)r.longestCycleMs,
                (unsigned long)beams::settleUs(beams::kDrop),
                (unsigned long)beams::settleUs(beams::kShelf));
}

}  // namespace

// ==================================================================== setup

void setup() {
  g_runMinuteMark = millis();  // the running-time clock starts here
  Serial.begin(cfg::SERIAL_BAUD);
  Serial.println();
  Serial.printf("Duck Box firmware %s (built %s %s)\n", cfg::FIRMWARE_VERSION, __DATE__, __TIME__);

  // --- Power: slow the CPU down, keep every radio off -------------------
  setCpuFrequencyMhz(cfg::CPU_FREQ_MHZ);
  Serial.printf("  cpu: %lu MHz\n", (unsigned long)getCpuFrequencyMhz());
  if (cfg::POWER_SAVE) {
    if (cfg::PIN_EMIT_DROP == cfg::PIN_EMIT_SHELF) {
      Serial.printf("  power save: on (cycle %lu ms, light sleep %s, both emitters on GPIO%u)\n",
                    (unsigned long)cfg::CYCLE_MS, cfg::LIGHT_SLEEP ? "on" : "off",
                    (unsigned)cfg::PIN_EMIT_DROP);
    } else {
      Serial.printf("  power save: on (cycle %lu ms, light sleep %s, emitters on GPIO%u and GPIO%u)\n",
                    (unsigned long)cfg::CYCLE_MS, cfg::LIGHT_SLEEP ? "on" : "off",
                    (unsigned)cfg::PIN_EMIT_DROP, (unsigned)cfg::PIN_EMIT_SHELF);
    }
  } else {
    Serial.println("  power save: off (emitters always lit, no sleep)");
  }

  pinMode(cfg::PIN_BUTTON, INPUT_PULLUP);
  pinMode(cfg::PIN_BOARD_LED, OUTPUT);
  digitalWrite(cfg::PIN_BOARD_LED, LOW);

  // Vext feeds the beam sensors and the OLED; give it time to settle while
  // the other start-up work is done.
  beams::powerOn();
  const uint32_t vextOnAt = millis();

  leds::begin();

  Serial.printf("  wifi: driver not part of this firmware, bluetooth: %s\n",
                radios::bluetoothIsIdle() ? "off (controller idle)" : "** NOT OFF **");
  const radios::LoraSleepResult lora = radios::loraSleep();
  if (lora.chipFound) {
    Serial.printf("  lora: SX1276 found, %s (RegOpMode 0x%02X)\n",
                  lora.asleep ? "asleep" : "** NOT ASLEEP **", (unsigned)lora.opMode);
  } else {
    Serial.printf("  lora: ** no SX1276 answered (RegVersion read 0x%02X) **\n",
                  (unsigned)lora.version);
  }

  while ((uint32_t)(millis() - vextOnAt) < cfg::VEXT_SETTLE_MS) delay(1);

  // --- Screen -------------------------------------------------------------
  g_oled.setBusClock(cfg::OLED_I2C_HZ);
  g_oled.begin();
  g_stage = Stage::Splash;
  g_stageStart = millis();
  present(messageFrame(phrases::kSplashTitle, phrases::kSplashHint), true,
          cfg::OLED_CONTRAST_NORMAL, /*atOnce=*/true);

  // --- Saved tallies ------------------------------------------------------
  const bool nvsOk = storage::begin();
  g_logic.setCounters(storage::loadCounters());
  Serial.printf("  nvs: %s, ", nvsOk ? "ok" : "** UNAVAILABLE, nothing will be saved **");
  logCounters();
  Serial.println();

  // --- Beams ----------------------------------------------------------------
  // (POWER_SAVE off: both assumed clear right now, see README.)
  if (!beams::begin()) {
    Serial.println("  ** beam interrupt could not be installed: polling instead, "
                   "short breaks may be missed **");
  }
  beams::detectPolarity();
  printPolarity();
  for (uint8_t b = 0; b < beams::kCount; ++b) {
    g_reportedMeasured[b] = beams::polarity(b).lightSeen;
  }
  if (!cfg::POWER_SAVE) {
    Serial.printf("  beam capture runs during flash writes: %s\n",
                  beams::captureIsFlashSafe() ? "yes" : "no");
  }

  if (cfg::POWER_SAVE) power::begin();

  g_candle.seed(esp_random());
  Serial.println("  hold PRG now for beam test mode");
}

// ===================================================================== loop

void loop() {
  if (cfg::POWER_SAVE) power::beginCycle();
  const uint32_t nowMs = millis();

  // POWER_SAVE: the one moment per cycle when the emitters and the candle
  // LEDs are lit. (Does nothing with POWER_SAVE off.)
  beams::service(nowMs);

  switch (g_stage) {
    case Stage::Splash:   loopSplash(nowMs);   break;
    case Stage::BeamTest: loopBeamTest(nowMs); break;
    case Stage::LastRun:  loopLastRun(nowMs);  break;
    case Stage::Running:  loopRunning(nowMs);  break;
  }
  updateLeds(nowMs);

  if (!cfg::POWER_SAVE) {
    delay(cfg::LOOP_DELAY_MS);
    return;
  }

  pumpScreen();
  power::noteScreen(g_displayOn);
  reportPower(nowMs);
  // Sleep through the rest of the cycle, except while starting up or in
  // beam test mode, and while serial output is still on its way out.
  const bool quiet = (int32_t)((uint32_t)millis() - g_awakeUntilMs) >= 0;
  power::endCycle(g_stage == Stage::Running && quiet);
}
