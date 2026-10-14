// Decides how one strobed beam is looked at each cycle, and what the looks
// add up to ("blocked" or "clear").
//
// Plain C++ (no Arduino dependencies), unit tested in
// native_tests/test_power_save.cpp.
//
// With POWER_SAVE on, an emitter is dark most of the time. Once per cycle
// it is lit, and after a "settle" time the receiver is read:
//
//   receiver sees light   -> the beam is clear. Light cannot be faked, so
//                            this is believed at once.
//   receiver stays dark   -> either something is in the beam, or the
//                            receiver is slower than the settle time.
//
// A beam that was clear is therefore only called blocked after the emitter
// has stayed lit for a further "confirm" time during which the receiver
// never answered (a "long look"). That also keeps anything shorter than
// the confirm time - a shadow, a glitch - from counting as a duck.
//
// If the receiver answers during the confirm time, it was late or
// something left the beam at that moment. Several late answers in a row
// mean the receiver really is slower than assumed, and the settle time is
// raised; after a long run of answers in time it is tried shorter again.
//
// While a beam is blocked the looks are short (a box that is full for
// hours must not drain the battery), with a long one every so often so
// that a receiver slower than the settle time is still found out.
#pragma once

#include <stdint.h>

namespace duck {

struct SamplerConfig {
  uint32_t settleUs = 1500;      // settle time until something is measured
  uint32_t settleMinUs = 300;
  uint32_t settleMaxUs = 3000;
  uint32_t confirmUs = 5000;     // dark this long after the settle time = blocked
  uint32_t recheckCycles = 50;   // while blocked: a long look this often
  uint32_t lateLooksToRaise = 3; // late answers in a row before settle is raised
  uint32_t relaxAfterCycles = 6000;  // answers in time before settle is tried lower
};

class BeamSampler {
 public:
  explicit BeamSampler(const SamplerConfig& config = SamplerConfig());

  // Start again, e.g. after measuring the receiver. `responseUs` is how
  // long the receiver took to answer the emitter (kNever = it did not).
  static const uint32_t kNever = 0xFFFFFFFFu;
  void reset(uint32_t responseUs);

  // How to look this cycle: read the receiver `settleUs` after lighting
  // the emitter; if it is dark then, keep the emitter lit and keep looking
  // until `giveUpUs` (equal to settleUs for a short look).
  struct Look {
    uint32_t settleUs;
    uint32_t giveUpUs;
  };
  Look plan();

  // What the look found: when light was seen, counted from lighting the
  // emitter - exactly the planned settleUs if it was there at the first
  // read, later if it only appeared while waiting - or kNever if the
  // receiver stayed dark until giveUpUs.
  // Returns true if the beam changed between blocked and clear.
  bool report(uint32_t seenAfterUs);

  bool blocked() const { return blocked_; }
  uint32_t settleUs() const { return settleUs_; }
  // Looks at which the receiver answered later than the settle time.
  uint32_t lateCount() const { return late_; }
  // Times the settle time was raised because of that.
  uint32_t raiseCount() const { return raises_; }

 private:
  uint32_t settleFor(uint32_t responseUs) const;

  SamplerConfig cfg_;
  uint32_t settleUs_;
  uint32_t baseSettleUs_;        // as measured; relaxing stops here
  bool blocked_ = false;
  uint32_t cyclesBlocked_ = 0;

  uint32_t late_ = 0;
  uint32_t raises_ = 0;
  uint32_t lateRun_ = 0;         // late answers in a row
  uint32_t lateRunSlowestUs_ = 0;
  uint32_t inTimeRun_ = 0;       // answers in time in a row
  uint32_t relaxAfter_;          // grows when relaxing turned out wrong
  bool justRelaxed_ = false;
};

}  // namespace duck
