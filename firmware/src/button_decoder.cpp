#include "button_decoder.h"

namespace duck {

ButtonDecoder::ButtonDecoder(const ButtonConfig& config) : cfg_(config) {}

void ButtonDecoder::reset(bool pressedNow, uint32_t nowMs) {
  stable_ = pressedNow;
  lastRaw_ = pressedNow;
  rawChangedAt_ = nowMs;
  pressStart_ = nowMs;
  releasedAt_ = nowMs;
  state_ = pressedNow ? State::WaitRelease : State::Idle;
}

uint32_t ButtonDecoder::heldMs(uint32_t nowMs) const {
  return stable_ ? (uint32_t)(nowMs - pressStart_) : 0;
}

ButtonEvent ButtonDecoder::update(bool rawPressed, uint32_t nowMs) {
  // Debounce: accept a new level once it has been steady for debounceMs.
  if (rawPressed != lastRaw_) {
    lastRaw_ = rawPressed;
    rawChangedAt_ = nowMs;
  }
  bool wentDown = false;
  bool wentUp = false;
  if (rawPressed != stable_ && (uint32_t)(nowMs - rawChangedAt_) >= cfg_.debounceMs) {
    stable_ = rawPressed;
    wentDown = stable_;
    wentUp = !stable_;
    if (wentDown) pressStart_ = nowMs;
  }

  switch (state_) {
    case State::Idle:
      if (wentDown) state_ = State::FirstDown;
      break;

    case State::FirstDown:
      if (wentUp) {
        releasedAt_ = nowMs;
        state_ = State::WaitSecond;
      } else if ((uint32_t)(nowMs - pressStart_) >= cfg_.shortMaxMs) {
        state_ = State::Holding;
        return ButtonEvent::HoldStart;
      }
      break;

    case State::WaitSecond:
      if (wentDown) {
        state_ = State::WaitRelease;
        return ButtonEvent::DoublePress;
      }
      if ((uint32_t)(nowMs - releasedAt_) >= cfg_.doubleGapMs) {
        state_ = State::Idle;
        return ButtonEvent::SinglePress;
      }
      break;

    case State::Holding:
      if (wentUp) {
        state_ = State::Idle;
        return ButtonEvent::HoldCancel;
      }
      if ((uint32_t)(nowMs - pressStart_) >= cfg_.holdMs) {
        state_ = State::WaitRelease;
        return ButtonEvent::HoldComplete;
      }
      break;

    case State::WaitRelease:
      if (wentUp) state_ = State::Idle;
      break;
  }
  return ButtonEvent::None;
}

}  // namespace duck
