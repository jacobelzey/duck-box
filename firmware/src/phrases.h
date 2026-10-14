// Every word the box says to passers-by, in one place.
//
// Keep the text in CAPITALS and short: the layout code picks the largest
// type that fits 124 pixels, so "AHOY!" comes out huge and a 14 character
// line comes out small. Plain C++ (no Arduino dependencies). After changing
// anything here, "sh native_tests/run_sim.sh" checks that every line still
// fits the screen and writes a picture of each one.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace phrases {

struct TwoLines {
  const char* line1;
  const char* line2;
};

// ---- Idle pages ----------------------------------------------------------
constexpr TwoLines kCallDrop = {"DROP YER", "DUCK"};
constexpr TwoLines kCallTake = {"TAKE YER", "LOOT"};
constexpr const char* kLabelDrops  = "DUCKS DROPPED";
constexpr const char* kLabelReach  = "PLUNDERED";
constexpr const char* kLabelTrades = "TRADES";
// Shown in place of the two calls to action while the state lasts.
constexpr TwoLines kIdleFull  = {"HOLD FULL!", "TAKE A DUCK"};
constexpr TwoLines kIdleStuck = {"DUCK ADRIFT!", "PUSH IT BACK"};

// ---- Event messages (one is picked in turn) --------------------------------
constexpr TwoLines kDrop[] = {
    {"AHOY!", "DUCK ABOARD"},
    {"YO HO HO!", "NEW CREW"},
    {"SPLASH!", "THANK YE"},
    {"ARRR!", "FINE DUCK"},
};
constexpr TwoLines kReachIn[] = {
    {"PLUNDER!", "FINE CHOICE"},
    {"YARR!", "LOOT TAKEN"},
    {"AVAST!", "DUCK NABBED"},
    {"HEAVE HO!", "FAIR WINDS"},
};
constexpr TwoLines kTrade[] = {
    {"FAIR TRADE", "MATEY!"},
    {"DUCK SWAP!", "WELL DONE"},
    {"A PIRATE'S", "BARGAIN!"},
    {"SHIPSHAPE!", "FINE TRADE"},
};
constexpr TwoLines kFull[] = {
    {"HOLD FULL!", "TAKE A DUCK"},
    {"SHIP'S FULL!", "PLUNDER SOME"},
};
constexpr TwoLines kStuck[] = {
    {"DUCK ADRIFT!", "PUSH IT BACK"},
    {"DUCK ASTRAY!", "NUDGE IT IN"},
};

// ---- Owner notices ---------------------------------------------------------
constexpr TwoLines kModeNormal  = {"LIGHTS", "NORMAL"};
constexpr TwoLines kModeDim     = {"LIGHTS", "DIM"};
constexpr TwoLines kModeStealth = {"STEALTH", "LIGHTS OUT"};
constexpr TwoLines kRestockOver = {"OPEN FOR", "PLUNDER"};
constexpr TwoLines kResetDone   = {"CLEAN SLATE", "TALLY RESET"};
constexpr TwoLines kResetCancel = {"BELAY THAT!", "TALLY KEPT"};
constexpr TwoLines kBeamWarning = {"CHECK BEAMS", "SEE README"};
constexpr const char* kSplashTitle  = "DUCK BOX";
constexpr const char* kSplashHint   = "HOLD PRG = TEST";
constexpr const char* kRestockTitle = "RESTOCKING";
constexpr const char* kResetTitle   = "RESET TALLY";
constexpr const char* kLastRunTitle = "LAST RUN";

template <size_t N>
const TwoLines& pick(const TwoLines (&list)[N], uint32_t n) {
  return list[n % N];
}

// "BACK IN 2:59"
inline void formatRestock(char* out, size_t size, uint32_t remainingMs) {
  const uint32_t seconds = (remainingMs + 999) / 1000;
  snprintf(out, size, "BACK IN %lu:%02lu", (unsigned long)(seconds / 60),
           (unsigned long)(seconds % 60));
}

// "IN 4"
inline void formatResetCountdown(char* out, size_t size, uint32_t remainingMs) {
  snprintf(out, size, "IN %lu", (unsigned long)((remainingMs + 999) / 1000));
}

// "12 h 30 min", or "NO RECORD" when nothing has been saved yet.
inline void formatRunTime(char* out, size_t size, uint32_t minutes) {
  if (minutes == 0) {
    snprintf(out, size, "NO RECORD");
  } else {
    snprintf(out, size, "%lu h %lu min", (unsigned long)(minutes / 60),
             (unsigned long)(minutes % 60));
  }
}

}  // namespace phrases
