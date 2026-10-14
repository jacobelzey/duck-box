#include "duck_logic.h"

namespace duck {

const char* eventName(EventType type) {
  switch (type) {
    case EventType::Drop:          return "DROP";
    case EventType::ReachIn:       return "REACH_IN";
    case EventType::Trade:         return "TRADE";
    case EventType::FullOn:        return "FULL_ON";
    case EventType::FullOff:       return "FULL_OFF";
    case EventType::ShelfStuckOn:  return "SHELF_STUCK_ON";
    case EventType::ShelfStuckOff: return "SHELF_STUCK_OFF";
  }
  return "?";
}

// ---------------------------------------------------------------- TimeFifo

void DuckLogic::TimeFifo::push(uint32_t timeMs) {
  if (count == kCapacity) popOldest();  // keep the newest ones
  t[count++] = timeMs;
}

void DuckLogic::TimeFifo::popOldest() {
  if (count == 0) return;
  for (uint8_t i = 1; i < count; ++i) t[i - 1] = t[i];
  --count;
}

void DuckLogic::TimeFifo::prune(uint32_t nowMs, uint32_t windowMs) {
  // Entries are in time order, so expired ones are always at the front.
  while (count > 0 && (uint32_t)(nowMs - t[0]) > windowMs) popOldest();
}

// --------------------------------------------------------------- lifecycle

DuckLogic::DuckLogic(const LogicConfig& config) : cfg_(config) {}

void DuckLogic::begin(uint32_t nowMs, bool dropBlocked, bool shelfBlocked) {
  now_ = nowMs;
  paused_ = false;
  full_ = false;
  stuck_ = false;
  dropBlocked_ = dropBlocked;
  shelfBlocked_ = shelfBlocked;
  pendingDrops_.clear();
  pendingReachIns_.clear();
  eventHead_ = 0;
  eventCount_ = 0;
  cancelAllTimers();
  seedFromLevels();
}

void DuckLogic::cancelAllTimers() {
  for (uint8_t i = 0; i < kTimerCount; ++i) timers_[i].cancel();
}

// Set up the state machines from the raw beam levels alone. Anything that
// is blocked right now is treated as "already seen": it will not produce a
// count, but it can still turn into FULL or a parked duck if it stays.
void DuckLogic::seedFromLevels() {
  haveLastDropEnd_ = false;
  haveLastShelfEnd_ = false;

  dropDebounced_ = dropBlocked_;
  dropSuppressed_ = dropBlocked_;
  if (dropBlocked_) timers_[kDropFull].arm(now_, cfg_.fullAfterMs);

  shelfSuppressed_ = shelfBlocked_;
  shelfBlockStart_ = now_;
  if (shelfBlocked_) timers_[kShelfStuck].arm(now_, cfg_.shelfStuckMs);
}

void DuckLogic::setPaused(bool paused, uint32_t nowMs) {
  advanceTo(nowMs);
  if (paused == paused_) return;
  paused_ = paused;
  cancelAllTimers();
  if (paused) {
    full_ = false;
    stuck_ = false;
    pendingDrops_.clear();
    pendingReachIns_.clear();
  } else {
    seedFromLevels();
  }
}

void DuckLogic::resetCounters() {
  counters_ = Counters();
  pendingDrops_.clear();
  pendingReachIns_.clear();
}

// -------------------------------------------------------------------- time

void DuckLogic::tick(uint32_t nowMs) { advanceTo(nowMs); }

// Move the clock forward to tMs, firing every timer that falls due on the
// way, in the order they fall due. This matters when edges are delivered
// late (for example after a slow screen redraw): the debounce that expired
// 20 ms ago must be handled before the "beam clear" edge that followed it.
void DuckLogic::advanceTo(uint32_t tMs) {
  // A timestamp at or before the current time does not move the clock.
  const uint32_t target = ((int32_t)(tMs - now_) > 0) ? tMs : now_;

  for (;;) {
    const uint32_t span = target - now_;
    int best = -1;
    uint32_t bestRemaining = 0;
    for (uint8_t i = 0; i < kTimerCount; ++i) {
      if (!timers_[i].active) continue;
      const uint32_t r = timers_[i].remaining(now_);
      if (r <= span && (best < 0 || r < bestRemaining)) {
        best = i;
        bestRemaining = r;
      }
    }
    if (best < 0) break;
    now_ += bestRemaining;
    timers_[best].cancel();
    fire((TimerId)best);
  }

  now_ = target;
  housekeeping();
}

// Forget timestamps that are too old to matter. Besides keeping the trade
// queues short, this stops a very old timestamp from looking recent again
// after the 32-bit clock wraps.
void DuckLogic::housekeeping() {
  pendingDrops_.prune(now_, cfg_.tradeWindowMs);
  pendingReachIns_.prune(now_, cfg_.tradeWindowMs);
  if (haveLastDropEnd_ && (uint32_t)(now_ - lastDropEnd_) >= cfg_.dropLockoutMs) {
    haveLastDropEnd_ = false;
  }
  if (haveLastShelfEnd_ && (uint32_t)(now_ - lastShelfEnd_) >= cfg_.shelfLockoutMs) {
    haveLastShelfEnd_ = false;
  }
}

void DuckLogic::fire(TimerId id) {
  switch (id) {
    case kDropDebounce:
      // The break has lasted long enough to be real.
      dropDebounced_ = true;
      if (!full_ && !dropSuppressed_) countDrop();
      break;

    case kDropFull:
      full_ = true;
      emit(EventType::FullOn);
      break;

    case kFullClear:
      full_ = false;
      emit(EventType::FullOff);
      break;

    case kShelfStuck:
      stuck_ = true;
      emit(EventType::ShelfStuckOn);
      break;

    case kStuckClear:
      stuck_ = false;
      emit(EventType::ShelfStuckOff);
      break;

    case kTimerCount:
      break;
  }
}

// ------------------------------------------------------------------- edges

void DuckLogic::onBeamEdge(Beam beam, bool blocked, uint32_t tMs) {
  advanceTo(tMs);

  if (paused_) {
    // Only remember the level; see setPaused().
    if (beam == Beam::Drop) dropBlocked_ = blocked;
    else shelfBlocked_ = blocked;
    return;
  }

  if (beam == Beam::Drop) onDropEdge(blocked);
  else onShelfEdge(blocked);
}

void DuckLogic::noteLevel(Beam beam, bool blocked, uint32_t tMs) {
  advanceTo(tMs);

  // Paused, or the beam going clear: nothing here could count anyway.
  if (paused_ || !blocked) {
    onBeamEdge(beam, blocked, tMs);
    return;
  }

  if (beam == Beam::Drop) {
    if (dropBlocked_) return;
    dropBlocked_ = true;
    dropDebounced_ = true;    // as in seedFromLevels(): "already seen"
    dropSuppressed_ = true;
    timers_[kDropDebounce].cancel();
    if (full_) timers_[kFullClear].cancel();
    else timers_[kDropFull].arm(now_, cfg_.fullAfterMs);
  } else {
    if (shelfBlocked_) return;
    shelfBlocked_ = true;
    shelfSuppressed_ = true;
    shelfBlockStart_ = now_;
    if (stuck_) timers_[kStuckClear].cancel();
    else timers_[kShelfStuck].arm(now_, cfg_.shelfStuckMs);
  }
}

void DuckLogic::onDropEdge(bool blocked) {
  if (blocked == dropBlocked_) return;  // repeated report of the same level
  dropBlocked_ = blocked;

  if (blocked) {
    dropDebounced_ = false;
    // Lockout: the beam must have stayed clear for dropLockoutMs since the
    // last believed break ended, otherwise this is the same duck tumbling.
    dropSuppressed_ = haveLastDropEnd_ &&
                      (uint32_t)(now_ - lastDropEnd_) < cfg_.dropLockoutMs;
    timers_[kDropDebounce].arm(now_, cfg_.dropDebounceMs);
    if (full_) {
      timers_[kFullClear].cancel();  // still full
    } else {
      timers_[kDropFull].arm(now_, cfg_.fullAfterMs);
    }
  } else {
    timers_[kDropDebounce].cancel();
    timers_[kDropFull].cancel();
    if (dropDebounced_) {
      lastDropEnd_ = now_;
      haveLastDropEnd_ = true;
    }
    if (full_) timers_[kFullClear].arm(now_, cfg_.fullClearMs);
  }
}

void DuckLogic::onShelfEdge(bool blocked) {
  if (blocked == shelfBlocked_) return;
  shelfBlocked_ = blocked;

  if (blocked) {
    shelfBlockStart_ = now_;
    shelfSuppressed_ = haveLastShelfEnd_ &&
                       (uint32_t)(now_ - lastShelfEnd_) < cfg_.shelfLockoutMs;
    if (stuck_) {
      timers_[kStuckClear].cancel();  // still parked
    } else {
      timers_[kShelfStuck].arm(now_, cfg_.shelfStuckMs);
    }
    return;
  }

  // Beam cleared.
  timers_[kShelfStuck].cancel();
  const uint32_t blockedFor = now_ - shelfBlockStart_;

  if (stuck_) {
    // The parked duck (or the hand that moved it) just left. Nothing is
    // counted; wait for the beam to stay clear before leaving the state.
    timers_[kStuckClear].arm(now_, cfg_.shelfStuckClearMs);
    lastShelfEnd_ = now_;
    haveLastShelfEnd_ = true;
    return;
  }

  if (blockedFor < cfg_.shelfMinBlockMs) return;  // glitch

  // A real visit just ended. Count it unless it began inside the lockout
  // of the previous one; either way the lockout restarts from here.
  const bool count = !shelfSuppressed_;
  lastShelfEnd_ = now_;
  haveLastShelfEnd_ = true;
  if (count) countReachIn();
}

// ---------------------------------------------------------------- counting

void DuckLogic::countDrop() {
  ++counters_.drops;
  emit(EventType::Drop);

  // Pair with the oldest reach-in still inside the trade window, if any.
  pendingReachIns_.prune(now_, cfg_.tradeWindowMs);
  if (!pendingReachIns_.empty()) {
    pendingReachIns_.popOldest();
    ++counters_.trades;
    emit(EventType::Trade);
  } else {
    pendingDrops_.push(now_);
  }
}

void DuckLogic::countReachIn() {
  ++counters_.reachIns;
  emit(EventType::ReachIn);

  pendingDrops_.prune(now_, cfg_.tradeWindowMs);
  if (!pendingDrops_.empty()) {
    pendingDrops_.popOldest();
    ++counters_.trades;
    emit(EventType::Trade);
  } else {
    pendingReachIns_.push(now_);
  }
}

// ------------------------------------------------------------ event queue

void DuckLogic::emit(EventType type) {
  if (eventCount_ == kEventCapacity) {
    // Nobody is reading; drop the oldest so the newest state is kept.
    eventHead_ = (uint8_t)((eventHead_ + 1) % kEventCapacity);
    --eventCount_;
    ++eventsLost_;
  }
  const uint8_t slot = (uint8_t)((eventHead_ + eventCount_) % kEventCapacity);
  events_[slot].type = type;
  events_[slot].timeMs = now_;
  ++eventCount_;
}

bool DuckLogic::popEvent(Event& out) {
  if (eventCount_ == 0) return false;
  out = events_[eventHead_];
  eventHead_ = (uint8_t)((eventHead_ + 1) % kEventCapacity);
  --eventCount_;
  return true;
}

}  // namespace duck
