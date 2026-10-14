// Interrupt-driven capture of beam level changes.
//
// The main loop is sometimes busy for 25 ms or more (a full OLED redraw
// over I2C, a flash write). A falling duck can come and go in that time, so
// the two beam pins are watched by a GPIO interrupt that records every
// level change, with its time, into a small ring buffer. The main loop
// empties the buffer when it gets round to it and the counting logic works
// from the recorded times, not from "now".
#pragma once

#include <stdint.h>

namespace edgecap {

constexpr uint8_t kBeamCount = 2;

struct Edge {
  uint32_t ms;    // time of the change, same clock as millis()
  uint8_t beam;   // 0 or 1 (order of the pins given to begin())
  uint8_t level;  // electrical level after the change, 0 or 1
};

// Configure both pins as inputs with pull-up and start capturing.
// Returns false if the interrupt could not be installed.
bool begin(uint8_t pinBeam0, uint8_t pinBeam1);

// Stop capturing. Safe to call when not started.
void end();

// True once begin() has succeeded.
bool started();

// Fetch the oldest recorded edge. Returns false when the buffer is empty.
bool pop(Edge& out);

// Electrical level of a beam pin right now.
uint8_t readLevel(uint8_t beam);

// Level after the most recent edge handed out by pop() (or the level at
// begin() / the last resync). If this differs from readLevel() while the
// buffer is empty, an edge was missed.
uint8_t deliveredLevel(uint8_t beam);

// Tell the capture layer that the main loop has noticed, by polling, that
// a beam is at `level` (recovery from a missed edge or a full buffer).
void resync(uint8_t beam, uint8_t level);

// Number of edges discarded because the buffer was full.
uint32_t overflowCount();

// True if the interrupt service accepted the "runs while flash is busy"
// flag. False means edges can be delayed during a flash write.
bool flashSafe();

}  // namespace edgecap
