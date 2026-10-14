#include "candle_flicker.h"

namespace duck {

CandleFlicker::CandleFlicker(const CandleConfig& config) : cfg_(config) {
  for (int i = 0; i < kLeds; ++i) {
    own_[i] = 0.0f;
    ownTarget_[i] = 0.0f;
    ownHold_[i] = 0;
  }
}

void CandleFlicker::seed(uint32_t value) { rng_ = value ? value : 0x1234ABCDu; }

// xorshift32: tiny, fast, and plenty random for a candle.
uint32_t CandleFlicker::nextRandom() {
  uint32_t x = rng_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_ = x;
  return x;
}

float CandleFlicker::randomSigned() {
  // Difference of two uniform numbers: triangular distribution on -1..1,
  // so small movements are common and big ones are rare.
  const float a = (float)(nextRandom() & 0xFFFF) / 65535.0f;
  const float b = (float)(nextRandom() & 0xFFFF) / 65535.0f;
  return a - b;
}

void CandleFlicker::step() {
  // Shared component: slow drift with the occasional sharp dip, like a
  // flame caught by a draught.
  if (--sharedHold_ <= 0) {
    sharedTarget_ = randomSigned();
    sharedHold_ = 3 + (int)(nextRandom() % 10);
    if (nextRandom() % 12 == 0) {
      sharedTarget_ = -1.0f;
      sharedHold_ = 3;
    }
  }
  shared_ += (sharedTarget_ - shared_) * cfg_.sharedSmoothing;

  // Private component: faster, smaller shimmer per LED.
  for (int i = 0; i < kLeds; ++i) {
    if (--ownHold_[i] <= 0) {
      ownTarget_[i] = randomSigned();
      ownHold_[i] = 1 + (int)(nextRandom() % 6);
    }
    own_[i] += (ownTarget_[i] - own_[i]) * cfg_.ownSmoothing;
  }

  flare_ *= cfg_.flareDecay;
  if (flare_ < 0.01f) flare_ = 0.0f;
}

float CandleFlicker::level(int led) const {
  if (led < 0 || led >= kLeds) return 0.0f;
  const float swing = cfg_.sharedPart * shared_ + (1.0f - cfg_.sharedPart) * own_[led];
  float duty = cfg_.average * (1.0f + 2.0f * cfg_.depth * swing);
  if (duty < 0.0f) duty = 0.0f;
  if (flare_ > 0.0f && cfg_.flarePeak > duty) {
    duty += flare_ * (cfg_.flarePeak - duty);
  }
  if (duty > 1.0f) duty = 1.0f;
  return duty;
}

}  // namespace duck
