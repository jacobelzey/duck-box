// When the screen is lit while nobody is doing anything (POWER_SAVE on).
//
// Plain C++, header only, unit tested in native_tests/test_power_save.cpp.
#pragma once

#include <stdint.h>

namespace duck {

struct ScreenPolicy {
  uint32_t stayOnMs = 30000;     // lit this long after the last activity
  uint32_t peekEveryMs = 20000;  // afterwards: one peek per this period...
  uint32_t peekOnMs = 4000;      // ...this long. See config.h for the 0s.
};

struct ScreenState {
  bool on;        // the screen should be lit now
  bool peeking;   // lit as a peek (show one page, do not rotate)
  uint32_t peek;  // which peek this is: 0, 1, 2, ... (valid while peeking)
};

// `idleMs` = time since the last event or button press.
inline ScreenState screenState(const ScreenPolicy& p, uint32_t idleMs) {
  ScreenState s = {true, false, 0};
  if (idleMs < p.stayOnMs) return s;
  if (p.peekEveryMs == 0) return s;                     // never goes dark
  if (p.peekOnMs == 0) {                                // dark until the next event
    s.on = false;
    return s;
  }
  if (p.peekOnMs >= p.peekEveryMs) return s;            // peeks touch: always lit
  const uint32_t t = idleMs - p.stayOnMs;
  const uint32_t phase = t % p.peekEveryMs;
  // Dark first, then the peek at the end of each period.
  s.on = phase >= p.peekEveryMs - p.peekOnMs;
  s.peeking = s.on;
  s.peek = t / p.peekEveryMs;
  return s;
}

}  // namespace duck
