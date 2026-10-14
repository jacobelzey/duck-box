// Turns the raw state of one push button into gestures.
//
// Plain C++ (no Arduino dependencies) so it can be unit tested natively.
//
//   short press            -> SinglePress (reported once the double-press
//                             gap has passed without a second press)
//   two short presses      -> DoublePress (reported as the second press
//                             goes down)
//   press and keep holding -> HoldStart after shortMaxMs, then either
//                             HoldComplete after holdMs or HoldCancel if
//                             released earlier
#pragma once

#include <stdint.h>

namespace duck {

enum class ButtonEvent : uint8_t {
  None,
  SinglePress,
  DoublePress,
  HoldStart,
  HoldComplete,
  HoldCancel,
};

struct ButtonConfig {
  uint32_t debounceMs = 30;    // raw level must be stable this long
  uint32_t shortMaxMs = 700;   // longer than this is a hold, not a press
  uint32_t doubleGapMs = 400;  // max pause between the two presses
  uint32_t holdMs = 5000;      // total hold time for HoldComplete
};

class ButtonDecoder {
 public:
  explicit ButtonDecoder(const ButtonConfig& config = ButtonConfig());

  // Forget everything. If the button is down right now, that press is
  // ignored until it is released.
  void reset(bool pressedNow, uint32_t nowMs);

  // Feed the raw button state; call every loop. Returns at most one event.
  ButtonEvent update(bool rawPressed, uint32_t nowMs);

  // True between HoldStart and HoldComplete / HoldCancel.
  bool holding() const { return state_ == State::Holding; }
  // How long the button has been down in the current press.
  uint32_t heldMs(uint32_t nowMs) const;
  bool pressed() const { return stable_; }

 private:
  enum class State : uint8_t {
    Idle,         // up, nothing pending
    FirstDown,    // first press is down
    WaitSecond,   // released after a short press, waiting for a second one
    Holding,      // long press in progress
    WaitRelease,  // gesture already reported, waiting for the button to rise
  };

  ButtonConfig cfg_;
  State state_ = State::Idle;
  bool stable_ = false;         // debounced state
  bool lastRaw_ = false;
  uint32_t rawChangedAt_ = 0;
  uint32_t pressStart_ = 0;
  uint32_t releasedAt_ = 0;
};

}  // namespace duck
