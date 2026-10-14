// Duck Box counting logic.
//
// Plain C++ with no Arduino or ESP-IDF dependencies, so the same file is
// compiled into the firmware and into the native unit tests
// (native_tests/test_duck_logic.cpp).
//
// Inputs:  timestamped beam edges (blocked / clear) and a periodic tick.
// Outputs: a small queue of events (DROP, REACH_IN, TRADE, FULL_ON, ...) and
//          the three running tallies.
//
// All times are milliseconds in a uint32_t that is allowed to wrap around
// (as millis() does after 49.7 days). Only differences between timestamps
// are ever used, so wrap-around is harmless.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace duck {

enum class Beam : uint8_t { Drop = 0, Shelf = 1 };

enum class EventType : uint8_t {
  Drop,           // one duck went through the drop hole
  ReachIn,        // one hand visit to the pickup shelf
  Trade,          // a drop and a reach-in paired within the trade window
  FullOn,         // drop beam blocked for a long time: chamber is full
  FullOff,        // drop beam has been clear again for a while
  ShelfStuckOn,   // shelf beam blocked for a long time: a duck is parked in it
  ShelfStuckOff,  // shelf beam has been clear again for a while
};

const char* eventName(EventType type);

struct Event {
  EventType type;
  uint32_t timeMs;  // when it happened (not when it was read)
};

struct Counters {
  uint32_t drops = 0;
  uint32_t reachIns = 0;
  uint32_t trades = 0;
};

// Every threshold used by the logic. The firmware fills this from
// include/config.h; the tests set the fields they care about directly.
struct LogicConfig {
  // Drop beam must stay blocked this long before the break is believed.
  uint32_t dropDebounceMs = 5;
  // After a believed break ends, further breaks are ignored until the beam
  // has stayed clear this long (a tumbling duck is one count).
  uint32_t dropLockoutMs = 400;
  // Drop beam continuously blocked this long => FULL.
  uint32_t fullAfterMs = 4000;
  // FULL ends once the drop beam has been continuously clear this long.
  uint32_t fullClearMs = 2000;

  // A shelf-beam break shorter than this is a glitch and is ignored.
  uint32_t shelfMinBlockMs = 60;
  // After a hand visit ends, breaks that begin within this time are part of
  // the same visit and are not counted again.
  uint32_t shelfLockoutMs = 1500;
  // Shelf beam continuously blocked this long => a duck is parked in it.
  uint32_t shelfStuckMs = 15000;
  // "Parked" ends once the shelf beam has been continuously clear this long.
  uint32_t shelfStuckClearMs = 2000;

  // A drop and a reach-in at most this far apart (either order) = one trade.
  uint32_t tradeWindowMs = 60000;
};

class DuckLogic {
 public:
  explicit DuckLogic(const LogicConfig& config = LogicConfig());

  // Start (or restart) the state machines at time nowMs. Tallies are kept.
  // A beam that is already blocked at this moment is not counted.
  void begin(uint32_t nowMs, bool dropBlocked = false, bool shelfBlocked = false);

  // Report a beam changing state at time tMs. Edges must be reported in the
  // order they happened. Timestamps slightly older than the last tick are
  // accepted and treated as "now".
  void onBeamEdge(Beam beam, bool blocked, uint32_t tMs);

  // Take note that a beam is at this level now, without counting anything
  // for it: a beam reported "blocked" this way is treated as one that was
  // already blocked when the box started (no drop, no reach-in when it
  // clears, but FULL / "parked duck" if it stays blocked). Unlike a pause,
  // this leaves everything else alone: the other beam, a drop or reach-in
  // waiting for its trade partner, FULL.
  void noteLevel(Beam beam, bool blocked, uint32_t tMs);

  // Advance time. Call often (every loop); all time-based transitions such
  // as debounce expiry, FULL and "parked duck" happen here.
  void tick(uint32_t nowMs);

  // Fetch the next pending event, oldest first. Returns false when empty.
  bool popEvent(Event& out);

  // While paused nothing is counted and no events are produced. Beam levels
  // are still tracked, so that on resume a beam that is already blocked is
  // not mistaken for a fresh duck or hand.
  void setPaused(bool paused, uint32_t nowMs);
  bool isPaused() const { return paused_; }

  const Counters& counters() const { return counters_; }
  void setCounters(const Counters& c) { counters_ = c; }
  // Zero the tallies and forget any drop / reach-in waiting for a partner.
  void resetCounters();

  bool isFull() const { return full_; }
  bool isShelfStuck() const { return stuck_; }
  bool dropBlocked() const { return dropBlocked_; }
  bool shelfBlocked() const { return shelfBlocked_; }
  uint32_t eventsLost() const { return eventsLost_; }

 private:
  // One-shot timer that survives wrap-around of the millisecond clock.
  struct Timer {
    bool active = false;
    uint32_t start = 0;
    uint32_t duration = 0;
    void arm(uint32_t startMs, uint32_t durationMs) {
      active = true;
      start = startMs;
      duration = durationMs;
    }
    void cancel() { active = false; }
    // Milliseconds until due, as seen from nowMs (0 = due now).
    uint32_t remaining(uint32_t nowMs) const {
      const uint32_t elapsed = nowMs - start;
      return elapsed >= duration ? 0 : duration - elapsed;
    }
  };

  // Short FIFO of timestamps: drops or reach-ins still waiting for a
  // trade partner.
  struct TimeFifo {
    static const uint8_t kCapacity = 8;
    uint32_t t[kCapacity];
    uint8_t count = 0;
    bool empty() const { return count == 0; }
    void clear() { count = 0; }
    void push(uint32_t timeMs);
    void popOldest();
    void prune(uint32_t nowMs, uint32_t windowMs);
  };

  enum TimerId : uint8_t {
    kDropDebounce = 0,
    kDropFull,
    kFullClear,
    kShelfStuck,
    kStuckClear,
    kTimerCount
  };

  void advanceTo(uint32_t tMs);
  void fire(TimerId id);
  void housekeeping();
  void seedFromLevels();
  void cancelAllTimers();
  void onDropEdge(bool blocked);
  void onShelfEdge(bool blocked);
  void countDrop();
  void countReachIn();
  void emit(EventType type);

  LogicConfig cfg_;
  Counters counters_;
  uint32_t now_ = 0;
  bool paused_ = false;

  Timer timers_[kTimerCount];

  // Drop beam
  bool dropBlocked_ = false;     // raw level
  bool dropDebounced_ = false;   // current block has passed the debounce
  bool dropSuppressed_ = false;  // current block began inside the lockout
  bool full_ = false;
  bool haveLastDropEnd_ = false;
  uint32_t lastDropEnd_ = 0;     // when the last debounced block ended

  // Shelf beam
  bool shelfBlocked_ = false;
  bool shelfSuppressed_ = false;  // current block began inside the lockout
  bool stuck_ = false;
  uint32_t shelfBlockStart_ = 0;
  bool haveLastShelfEnd_ = false;
  uint32_t lastShelfEnd_ = 0;     // when the last real (non-glitch) block ended

  // Trade pairing
  TimeFifo pendingDrops_;
  TimeFifo pendingReachIns_;

  // Output queue
  static const uint8_t kEventCapacity = 16;
  Event events_[kEventCapacity];
  uint8_t eventHead_ = 0;
  uint8_t eventCount_ = 0;
  uint32_t eventsLost_ = 0;
};

}  // namespace duck
