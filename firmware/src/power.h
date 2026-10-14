// POWER_SAVE on: paces the main loop at one pass per cycle and sleeps the
// processor for whatever is left of each cycle. Also keeps the figures for
// the "POWER" line on the serial log.
#pragma once

#include <stdint.h>

namespace power {

// Call once at the end of setup(): makes sure every pin the box uses keeps
// its level and direction while the processor sleeps.
void begin();

// Call first thing in loop().
void beginCycle();

// Call last thing in loop(): waits until the cycle is over. With
// `maySleep` (and cfg::LIGHT_SLEEP) the processor is put into light sleep
// for that time; otherwise it idles awake.
void endCycle(bool maySleep);

// Tell the report whether the screen is lit during this cycle.
void noteScreen(bool on);

struct Report {
  uint8_t awakePct;       // processor awake (approximate, see README)
  uint8_t emitterPct[2];  // each emitter lit
  uint8_t ledPct;         // average LED duty (all four together = 100)
  uint8_t screenPct;      // screen lit
  uint32_t sleeps;        // sleeps taken in the period
  uint32_t refused;       // sleeps the chip refused (too short)
  uint32_t longestCycleMs;  // slowest loop pass in the period
};

// True once per cfg::POWER_REPORT_MS, with the figures for that period.
bool report(uint32_t nowMs, Report& out);

}  // namespace power
