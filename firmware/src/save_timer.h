// Decides when changed data should be written to flash.
//
// Writes are coalesced: a save becomes due a few seconds after the *last*
// change, so a burst of events costs one write. A second limit makes sure
// that a steady trickle of changes cannot postpone the save for ever.
//
// Plain C++ (no Arduino dependencies).
#pragma once

#include <stdint.h>

namespace duck {

class SaveTimer {
 public:
  SaveTimer(uint32_t quietMs, uint32_t maxWaitMs)
      : quietMs_(quietMs), maxWaitMs_(maxWaitMs) {}

  void markDirty(uint32_t nowMs) {
    if (!dirty_) {
      dirty_ = true;
      firstChange_ = nowMs;
    }
    lastChange_ = nowMs;
  }

  bool dirty() const { return dirty_; }

  bool due(uint32_t nowMs) const {
    if (!dirty_) return false;
    return (uint32_t)(nowMs - lastChange_) >= quietMs_ ||
           (uint32_t)(nowMs - firstChange_) >= maxWaitMs_;
  }

  void markSaved() { dirty_ = false; }

 private:
  uint32_t quietMs_;
  uint32_t maxWaitMs_;
  bool dirty_ = false;
  uint32_t firstChange_ = 0;
  uint32_t lastChange_ = 0;
};

}  // namespace duck
