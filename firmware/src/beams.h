// The two break-beam sensors as the rest of the firmware sees them:
// "blocked" or "clear", whatever the electrical polarity turns out to be.
//
// Two ways of working, chosen by cfg::POWER_SAVE:
//
//   on   The emitters are lit for a moment once per cycle (pulse.*) and
//        the receivers are read then; beam_sampler.* turns the looks into
//        "blocked" / "clear". The main loop must call service() once per
//        cycle.
//   off  The emitters are lit all the time; edge_capture (interrupt + ring
//        buffer) records every change, with a polling safety net.
#pragma once

#include <stdint.h>

namespace beams {

constexpr uint8_t kDrop = 0;
constexpr uint8_t kShelf = 1;
constexpr uint8_t kCount = 2;

struct Polarity {
  uint8_t blockedLevel;  // electrical level that means "blocked", 0 or 1
  bool forced;           // fixed in config.h rather than measured
  bool unstable;         // the idle level was not steady while measuring
  uint8_t idleLevel;     // level seen most of the time while measuring
                         // (POWER_SAVE on: measured with the emitter dark)
  uint8_t agreePct;      // share of samples at idleLevel, 0..100
  // POWER_SAVE on only:
  bool lightSeen;        // the receiver answered its emitter when measured
  uint32_t responseUs;   // how long that took (valid if lightSeen)
};

// Switch on Vext, which powers both sensors (and the OLED).
void powerOn();

// Set up the pins and start watching both receivers. Returns false on
// failure (POWER_SAVE off: the interrupt could not be installed).
bool begin();

// POWER_SAVE on: one look at both beams (and one flash of the candle LEDs,
// which share the same timing window). Call once per cycle. Does nothing
// with POWER_SAVE off.
void service(uint32_t nowMs);

// Work out which level means "blocked" for each beam. Blocks for
// cfg::BEAM_POLARITY_SAMPLE_MS. Edges recorded so far are thrown away.
// POWER_SAVE off: auto-detection assumes that both beams are clear while
// this runs. POWER_SAVE on: the level is read with the emitters dark, so
// it does not matter what is in the beams; the receivers' response time is
// measured as well (that part does need a clear beam, and says so if not).
void detectPolarity();

const Polarity& polarity(uint8_t beam);

// True when auto-detection should not be trusted: a noisy reading, or two
// identical sensors that came out with opposite polarity (which means one
// of them was most likely blocked or misaligned at power-up).
bool polarityDoubtful();

uint8_t rawLevel(uint8_t beam);
bool isBlocked(uint8_t beam);

struct Change {
  uint8_t beam;
  bool blocked;
  uint32_t ms;  // when it happened, millis() time base
  // POWER_SAVE on: this "blocked" is not a duck or a hand. Both beams went
  // dark in the very same look and stayed dark, which is what a loose
  // emitter wire looks like. Take note of the level, but count nothing.
  bool uncounted;
};

// Next recorded beam change, oldest first. Returns false when there is
// nothing more for now. Call until it returns false, every loop.
bool next(Change& out, uint32_t nowMs);

// Throw away everything recorded so far.
void discardPending();

// Diagnostics.
uint32_t resyncCount();     // times the polling safety net had to step in
uint32_t overflowCount();   // edges lost because the buffer was full
bool captureIsFlashSafe();  // interrupt keeps running during flash writes
// POWER_SAVE on:
uint32_t lateCount(uint8_t beam);   // looks at which the receiver answered late
uint32_t raiseCount(uint8_t beam);  // times the emitter lead time was raised for it
uint32_t settleUs(uint8_t beam);    // emitter lead time in use now
// The receiver has been showing light while its emitter is dark for a
// second or more (bright ambient light, or its OUT wire is loose).
bool strayLight(uint8_t beam);
// Both beams went dark together and have stayed dark (see Change).
bool emittersSuspect();

}  // namespace beams
