#include "beam_sampler.h"

namespace duck {

BeamSampler::BeamSampler(const SamplerConfig& config)
    : cfg_(config), settleUs_(config.settleUs), baseSettleUs_(config.settleUs),
      relaxAfter_(config.relaxAfterCycles) {
  if (cfg_.settleMinUs > cfg_.settleMaxUs) cfg_.settleMinUs = cfg_.settleMaxUs;
  if (cfg_.recheckCycles == 0) cfg_.recheckCycles = 1;
  if (cfg_.lateLooksToRaise == 0) cfg_.lateLooksToRaise = 1;
  if (cfg_.relaxAfterCycles == 0) cfg_.relaxAfterCycles = 1;
  relaxAfter_ = cfg_.relaxAfterCycles;
  if (settleUs_ < cfg_.settleMinUs) settleUs_ = cfg_.settleMinUs;
  if (settleUs_ > cfg_.settleMaxUs) settleUs_ = cfg_.settleMaxUs;
  baseSettleUs_ = settleUs_;
}

// Twice the measured response plus a margin, inside the allowed range.
uint32_t BeamSampler::settleFor(uint32_t responseUs) const {
  uint32_t s = cfg_.settleMaxUs;
  if (responseUs < cfg_.settleMaxUs) s = responseUs * 2u + 100u;
  if (s < cfg_.settleMinUs) s = cfg_.settleMinUs;
  if (s > cfg_.settleMaxUs) s = cfg_.settleMaxUs;
  return s;
}

void BeamSampler::reset(uint32_t responseUs) {
  cyclesBlocked_ = 0;
  lateRun_ = 0;
  lateRunSlowestUs_ = 0;
  inTimeRun_ = 0;
  relaxAfter_ = cfg_.relaxAfterCycles;
  justRelaxed_ = false;
  if (responseUs == kNever) {
    // No light at all: something is in the beam (or it is misaligned).
    // Keep the starting settle time; the long looks will correct it.
    blocked_ = true;
    settleUs_ = cfg_.settleUs;
    if (settleUs_ < cfg_.settleMinUs) settleUs_ = cfg_.settleMinUs;
    if (settleUs_ > cfg_.settleMaxUs) settleUs_ = cfg_.settleMaxUs;
  } else {
    blocked_ = false;
    settleUs_ = settleFor(responseUs);
  }
  baseSettleUs_ = settleUs_;
}

BeamSampler::Look BeamSampler::plan() {
  Look look;
  look.settleUs = settleUs_;
  look.giveUpUs = settleUs_;
  const bool longLook =
      !blocked_ || (cyclesBlocked_ % cfg_.recheckCycles == cfg_.recheckCycles - 1u);
  if (longLook) look.giveUpUs = settleUs_ + cfg_.confirmUs;
  return look;
}

bool BeamSampler::report(uint32_t seenAfterUs) {
  const bool was = blocked_;

  if (seenAfterUs == kNever) {
    lateRun_ = 0;
    inTimeRun_ = 0;
    if (!blocked_) {
      blocked_ = true;
      cyclesBlocked_ = 0;
    } else {
      ++cyclesBlocked_;
    }
    return blocked_ != was;
  }

  blocked_ = false;
  cyclesBlocked_ = 0;

  if (seenAfterUs > settleUs_) {
    // Light, but only after the settle time. Once or twice that is
    // something leaving the beam during the look; several times in a row
    // it is a receiver slower than we allow for.
    ++late_;
    inTimeRun_ = 0;
    if (lateRun_ == 0 || seenAfterUs > lateRunSlowestUs_) lateRunSlowestUs_ = seenAfterUs;
    if (++lateRun_ >= cfg_.lateLooksToRaise) {
      lateRun_ = 0;
      const uint32_t wanted = settleFor(lateRunSlowestUs_);
      if (wanted > settleUs_) {
        settleUs_ = wanted;
        ++raises_;
        if (justRelaxed_) {
          // The shorter time we had just tried was wrong: wait twice as
          // long before trying again (up to 64 times the normal wait).
          if (relaxAfter_ < cfg_.relaxAfterCycles * 64u) relaxAfter_ *= 2u;
        }
      }
      justRelaxed_ = false;
    }
  } else {
    lateRun_ = 0;
    if (inTimeRun_ < 0xFFFFFFFFu) ++inTimeRun_;
    if (justRelaxed_ && inTimeRun_ >= 200u) justRelaxed_ = false;  // it held
    if (settleUs_ > baseSettleUs_ && inTimeRun_ >= relaxAfter_) {
      // A long quiet run: try half-way back towards the measured value.
      inTimeRun_ = 0;
      settleUs_ = baseSettleUs_ + (settleUs_ - baseSettleUs_) / 2u;
      if (settleUs_ - baseSettleUs_ < 50u) settleUs_ = baseSettleUs_;
      justRelaxed_ = true;
    }
  }
  return blocked_ != was;
}

}  // namespace duck
