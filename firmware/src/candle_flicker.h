// Candle-flame brightness generator for the four duck-emblem LEDs.
//
// Plain C++ (no Arduino dependencies). Each LED follows a smoothed random
// walk around a common average; part of the movement is shared between all
// four ("the draught") and part is private to each LED, which is what makes
// it read as one flame rather than four blinking lamps.
#pragma once

#include <stdint.h>

namespace duck {

struct CandleConfig {
  float average = 0.30f;       // mean duty cycle, 0..1 (sets battery drain)
  float depth = 0.55f;         // how far the flame swings around the mean
  float sharedPart = 0.45f;    // fraction of the swing common to all LEDs
  float sharedSmoothing = 0.12f;  // per step, smaller = slower
  float ownSmoothing = 0.30f;
  float flarePeak = 1.0f;      // duty at the top of an event flare
  float flareDecay = 0.90f;    // flare is multiplied by this every step
};

class CandleFlicker {
 public:
  static const int kLeds = 4;

  explicit CandleFlicker(const CandleConfig& config = CandleConfig());

  void seed(uint32_t value);
  // Advance the simulation by one fixed step (the caller picks the rate;
  // the defaults are tuned for 50 steps per second).
  void step();
  // Duty for one LED, 0..1, including any flare.
  float level(int led) const;
  // Briefly push all LEDs towards flarePeak.
  void flare() { flare_ = 1.0f; }

 private:
  uint32_t nextRandom();
  float randomSigned();  // -1..1, values near 0 most likely

  CandleConfig cfg_;
  uint32_t rng_ = 0x1234ABCDu;

  float shared_ = 0.0f;
  float sharedTarget_ = 0.0f;
  int sharedHold_ = 0;

  float own_[kLeds];
  float ownTarget_[kLeds];
  int ownHold_[kLeds];

  float flare_ = 0.0f;
};

}  // namespace duck
