#include "beams.h"

#include <Arduino.h>

#include "beam_sampler.h"
#include "config.h"
#include "edge_capture.h"
#include "pulse.h"

namespace beams {

namespace {

const uint8_t kPins[kCount] = {cfg::PIN_BEAM_DROP, cfg::PIN_BEAM_SHELF};

Polarity s_polarity[kCount] = {
    {0, false, false, 1, 0, false, 0},
    {0, false, false, 1, 0, false, 0},
};

// Safety net state: since when has the pin disagreed with the last level
// that was handed out?
bool s_mismatch[kCount] = {false, false};
uint32_t s_mismatchSince[kCount] = {0, 0};
uint32_t s_resyncs = 0;

cfg::BeamPolarity configured(uint8_t beam) {
  return beam == kDrop ? cfg::DROP_BEAM_POLARITY : cfg::SHELF_BEAM_POLARITY;
}

// ---- POWER_SAVE on ---------------------------------------------------------

duck::SamplerConfig samplerConfig() {
  duck::SamplerConfig c;
  c.settleUs = cfg::EMIT_SETTLE_US;
  c.settleMinUs = cfg::EMIT_SETTLE_MIN_US;
  c.settleMaxUs = cfg::EMIT_SETTLE_MAX_US;
  c.confirmUs = cfg::EMIT_CONFIRM_US;
  c.recheckCycles = cfg::EMIT_RECHECK_CYCLES;
  c.lateLooksToRaise = cfg::EMIT_LATE_LOOKS_TO_RAISE;
  c.relaxAfterCycles = cfg::EMIT_RELAX_AFTER_CYCLES;
  return c;
}

duck::BeamSampler s_sampler[kCount] = {duck::BeamSampler(samplerConfig()),
                                       duck::BeamSampler(samplerConfig())};
uint8_t s_lastLevel[kCount] = {0, 0};

// "Light while the emitter is dark": the receiver showed its lit level
// just before the emitter was switched on. One second of that in a row
// means bright light is falling on the receiver or its OUT wire has come
// off - either way the beam can no longer notice a duck.
constexpr uint16_t kStrayCycles = 100;
// ...and half a minute without it before the warning is taken back, so
// that light that comes and goes gives one warning, not hundreds.
constexpr uint16_t kStrayClearCycles = 3000;
uint16_t s_strayRun[kCount] = {0, 0};
uint16_t s_cleanRun[kCount] = {0, 0};
bool s_stray[kCount] = {false, false};

// Both emitters hang on one wire. If both beams, having been clear, go
// dark in the very same look, the likelier explanation by far is that
// wire (or the pin), not a duck and a hand arriving within the same
// hundredth of a second. Such a pair is held back: if either beam sees
// light again soon, nothing happened; if both stay dark for half a second,
// they are reported as blocked but marked "do not count".
constexpr bool kSharedEmitter = cfg::PIN_EMIT_DROP == cfg::PIN_EMIT_SHELF;
constexpr uint16_t kHoldCycles = 50;
uint16_t s_holdRun = 0;
uint32_t s_holdStartMs = 0;
bool s_emittersSuspect = false;

// A beam that showed no light at start-up is measured once it has shown
// light for a second without a break.
constexpr uint16_t kMeasureAfterCycles = 100;
uint16_t s_lightRun[kCount] = {0, 0};

// Changes found by service(), waiting for next(). Far more room than one
// loop pass can fill; if it does fill up the oldest entry is dropped.
constexpr uint8_t kQueueSize = 16;
Change s_queue[kQueueSize];
uint8_t s_queueHead = 0;
uint8_t s_queueCount = 0;
uint32_t s_queueOverflows = 0;

void pushChange(uint8_t beam, bool blocked, uint32_t ms, bool uncounted = false) {
  if (s_queueCount == kQueueSize) {
    s_queueHead = (uint8_t)((s_queueHead + 1) % kQueueSize);
    --s_queueCount;
    ++s_queueOverflows;
  }
  Change& c = s_queue[(s_queueHead + s_queueCount) % kQueueSize];
  c.beam = beam;
  c.blocked = blocked;
  c.ms = ms;
  c.uncounted = uncounted;
  ++s_queueCount;
}

// POWER_SAVE on. With the emitters dark the receivers show their "no
// light" level, which is by definition the "blocked" level - whatever is
// or is not in the beam at this moment. Then each emitter is lit a few
// times to measure how quickly its receiver answers.
void detectPolarityStrobed() {
  pulse::emittersSteady(false);
  delay(5);  // let the receivers settle into darkness

  const uint32_t samples = cfg::BEAM_POLARITY_SAMPLE_MS > 60 ? 60 : cfg::BEAM_POLARITY_SAMPLE_MS;
  uint32_t highCount[kCount] = {0, 0};
  for (uint32_t i = 0; i < samples; ++i) {
    for (uint8_t b = 0; b < kCount; ++b) highCount[b] += pulse::readLevel(b);
    delay(1);
  }

  for (uint8_t b = 0; b < kCount; ++b) {
    Polarity& p = s_polarity[b];
    const bool darkHigh = highCount[b] * 2 >= samples;
    const uint32_t agree = darkHigh ? highCount[b] : samples - highCount[b];
    p.idleLevel = darkHigh ? 1 : 0;  // here: the level with the emitter dark
    p.agreePct = (uint8_t)(samples ? agree * 100 / samples : 0);
    p.unstable = p.agreePct < cfg::BEAM_POLARITY_MIN_AGREE_PCT;

    switch (configured(b)) {
      case cfg::BeamPolarity::BlockedIsLow:
        p.forced = true;
        p.blockedLevel = 0;
        break;
      case cfg::BeamPolarity::BlockedIsHigh:
        p.forced = true;
        p.blockedLevel = 1;
        break;
      case cfg::BeamPolarity::AutoDetect:
      default:
        p.forced = false;
        p.blockedLevel = p.idleLevel;  // dark = blocked
        break;
    }

    // Response time: the slowest of a few tries, with dark pauses between.
    uint32_t slowest = 0;
    bool seen = true;
    for (uint8_t i = 0; i < 5; ++i) {
      const uint32_t r = pulse::measureResponse(
          b, p.blockedLevel, cfg::EMIT_SETTLE_MAX_US + cfg::EMIT_CONFIRM_US);
      if (r == pulse::kNever) {
        seen = false;
        break;
      }
      if (r > slowest) slowest = r;
      delay(4);
    }
    p.lightSeen = seen;
    p.responseUs = seen ? slowest : 0;
    s_sampler[b].reset(seen ? slowest : duck::BeamSampler::kNever);
    s_strayRun[b] = 0;
    s_cleanRun[b] = 0;
    s_stray[b] = false;
    s_holdRun = 0;
    s_emittersSuspect = false;
    s_lightRun[b] = 0;
    s_lastLevel[b] = seen ? (uint8_t)!p.blockedLevel : p.blockedLevel;
  }
}

}  // namespace

void powerOn() {
  pinMode(cfg::PIN_VEXT_CTRL, OUTPUT);
  digitalWrite(cfg::PIN_VEXT_CTRL, cfg::VEXT_ON_LEVEL);
}

bool begin() {
  if (cfg::POWER_SAVE) {
    pulse::begin();
    return true;
  }
  // Emitters wired to the two GPIO pins: light them for good. (Emitters
  // wired to Vext by mistake are lit anyway.)
  pulse::emittersSteady(true);
  return edgecap::begin(kPins[kDrop], kPins[kShelf]);
}

void service(uint32_t nowMs) {
  if (!cfg::POWER_SAVE) return;

  duck::BeamSampler::Look plan[kCount];
  for (uint8_t b = 0; b < kCount; ++b) plan[b] = s_sampler[b].plan();

  // Emitters on one pin are lit together anyway, so both receivers are
  // read at the same moment, after the longer of the two lead times. That
  // way a wire that loses contact can never be seen by one look and not by
  // the other.
  uint32_t lead[kCount] = {plan[kDrop].settleUs, plan[kShelf].settleUs};
  if (kSharedEmitter) {
    const uint32_t common = lead[kDrop] > lead[kShelf] ? lead[kDrop] : lead[kShelf];
    lead[kDrop] = common;
    lead[kShelf] = common;
  }

  pulse::Look looks[kCount];
  for (uint8_t b = 0; b < kCount; ++b) {
    looks[b].wanted = true;
    looks[b].settleUs = lead[b];
    looks[b].giveUpUs = lead[b] + (plan[b].giveUpUs - plan[b].settleUs);
    looks[b].darkLevel = s_polarity[b].blockedLevel;
    looks[b].levelBefore = s_polarity[b].blockedLevel;
    looks[b].seenAfterUs = pulse::kNever;
    looks[b].level = s_polarity[b].blockedLevel;
  }
  pulse::window(looks);

  bool light[kCount];
  uint32_t seen[kCount];
  for (uint8_t b = 0; b < kCount; ++b) {
    s_lastLevel[b] = looks[b].level;
    light[b] = looks[b].seenAfterUs != pulse::kNever;
    // For the sampler, light at the first read is "in time" by its own
    // lead time, whatever lead the look actually used.
    if (!light[b]) seen[b] = duck::BeamSampler::kNever;
    else if (looks[b].seenAfterUs == lead[b]) seen[b] = plan[b].settleUs;
    else seen[b] = looks[b].seenAfterUs;

    if (looks[b].levelBefore != s_polarity[b].blockedLevel) {
      s_cleanRun[b] = 0;
      if (s_strayRun[b] < kStrayCycles && ++s_strayRun[b] == kStrayCycles) s_stray[b] = true;
    } else {
      s_strayRun[b] = 0;
      if (s_cleanRun[b] < kStrayClearCycles && ++s_cleanRun[b] == kStrayClearCycles) {
        s_stray[b] = false;
      }
    }
  }

  // Both beams clear until now, and both dark in this one look?
  const bool bothClear = !s_sampler[kDrop].blocked() && !s_sampler[kShelf].blocked();
  if (kSharedEmitter && bothClear && !light[kDrop] && !light[kShelf]) {
    if (s_holdRun == 0) s_holdStartMs = nowMs;
    if (++s_holdRun < kHoldCycles) return;      // wait and see
    s_holdRun = 0;
    s_emittersSuspect = true;
    for (uint8_t b = 0; b < kCount; ++b) {
      s_sampler[b].report(duck::BeamSampler::kNever);
      pushChange(b, true, nowMs, /*uncounted=*/true);
    }
    return;
  }
  if (s_holdRun != 0) {
    // The wait is over. Both beams seeing light again at once was the
    // wire. One of them only: it was a duck and a hand at the same moment
    // after all, and both are reported from when they began (the beam that
    // is clear again is reported blocked first; "clear" follows below).
    s_holdRun = 0;
    if (light[kDrop] != light[kShelf]) {
      for (uint8_t b = 0; b < kCount; ++b) {
        s_sampler[b].report(duck::BeamSampler::kNever);
        pushChange(b, true, s_holdStartMs);
      }
    }
  }

  for (uint8_t b = 0; b < kCount; ++b) {
    if (light[b]) s_emittersSuspect = false;
    if (s_sampler[b].report(seen[b])) pushChange(b, s_sampler[b].blocked(), nowMs);

    // A beam that was blocked when the box was switched on could not be
    // measured then. Do it once it has shown light steadily for a second.
    if (s_polarity[b].lightSeen) continue;
    if (!light[b]) {
      s_lightRun[b] = 0;
      continue;
    }
    if (++s_lightRun[b] < kMeasureAfterCycles) continue;
    s_lightRun[b] = 0;                          // if it fails: again in a second
    uint32_t slowest = 0;
    bool seenAll = true;
    for (uint8_t i = 0; i < 3 && seenAll; ++i) {
      delay(2);
      const uint32_t r = pulse::measureResponse(
          b, s_polarity[b].blockedLevel, cfg::EMIT_SETTLE_MAX_US + cfg::EMIT_CONFIRM_US);
      if (r == pulse::kNever) seenAll = false;
      else if (r > slowest) slowest = r;
    }
    if (seenAll) {
      s_polarity[b].lightSeen = true;
      s_polarity[b].responseUs = slowest;
      s_sampler[b].reset(slowest);              // clear, with the measured lead
    }
  }
}

void detectPolarity() {
  if (cfg::POWER_SAVE) {
    detectPolarityStrobed();
    discardPending();
    return;
  }

  // Sample both pins once a millisecond and take the majority as "idle".
  const uint32_t samples = cfg::BEAM_POLARITY_SAMPLE_MS;
  uint32_t highCount[kCount] = {0, 0};
  for (uint32_t i = 0; i < samples; ++i) {
    for (uint8_t b = 0; b < kCount; ++b) highCount[b] += edgecap::readLevel(b);
    delay(1);
  }

  for (uint8_t b = 0; b < kCount; ++b) {
    Polarity& p = s_polarity[b];
    const bool idleHigh = highCount[b] * 2 >= samples;
    const uint32_t agree = idleHigh ? highCount[b] : samples - highCount[b];
    p.idleLevel = idleHigh ? 1 : 0;
    p.agreePct = (uint8_t)(agree * 100 / samples);

    switch (configured(b)) {
      case cfg::BeamPolarity::BlockedIsLow:
        p.forced = true;
        p.unstable = false;
        p.blockedLevel = 0;
        break;
      case cfg::BeamPolarity::BlockedIsHigh:
        p.forced = true;
        p.unstable = false;
        p.blockedLevel = 1;
        break;
      case cfg::BeamPolarity::AutoDetect:
      default:
        p.forced = false;
        p.unstable = p.agreePct < cfg::BEAM_POLARITY_MIN_AGREE_PCT;
        p.blockedLevel = idleHigh ? 0 : 1;  // blocked = the opposite of idle
        break;
    }
  }
  discardPending();
}

const Polarity& polarity(uint8_t beam) { return s_polarity[beam < kCount ? beam : 0]; }

bool polarityDoubtful() {
  const Polarity& d = s_polarity[kDrop];
  const Polarity& s = s_polarity[kShelf];
  if (d.unstable || s.unstable) return true;
  if (cfg::POWER_SAVE) {
    // A level fixed in config.h that is not what the dark receiver shows.
    if (d.forced && d.blockedLevel != d.idleLevel) return true;
    if (s.forced && s.blockedLevel != s.idleLevel) return true;
    // No answer from a receiver when its emitter was lit: something is in
    // the beam, or it is misaligned, or the emitter is not on its pin (an
    // emitter left on Vext is lit all the time, which turns "dark" and
    // "lit" upside down). Worth a look in every case.
    if (!d.lightSeen || !s.lightSeen) return true;
  }
  // Both sensors are the same model, so they must agree. Only meaningful
  // when both were measured; a forced value is taken as the truth.
  if (!d.forced && !s.forced && d.blockedLevel != s.blockedLevel) return true;
  // One forced, one measured: the measured one should match the forced one.
  if (d.forced != s.forced && d.blockedLevel != s.blockedLevel) return true;
  return false;
}

uint8_t rawLevel(uint8_t beam) {
  if (cfg::POWER_SAVE) return s_lastLevel[beam < kCount ? beam : 0];  // as last looked at
  return edgecap::readLevel(beam);
}

bool isBlocked(uint8_t beam) {
  if (cfg::POWER_SAVE) return s_sampler[beam < kCount ? beam : 0].blocked();
  return edgecap::readLevel(beam) == s_polarity[beam].blockedLevel;
}

bool next(Change& out, uint32_t nowMs) {
  if (cfg::POWER_SAVE) {
    if (s_queueCount == 0) return false;
    out = s_queue[s_queueHead];
    s_queueHead = (uint8_t)((s_queueHead + 1) % kQueueSize);
    --s_queueCount;
    return true;
  }

  // Normal path: edges recorded by the interrupt, with their true times.
  edgecap::Edge e;
  if (edgecap::pop(e)) {
    out.beam = e.beam;
    out.blocked = (e.level == s_polarity[e.beam].blockedLevel);
    out.ms = e.ms;
    out.uncounted = false;
    return true;
  }

  // Safety net: the buffer is empty, so each pin should be at the level of
  // the last edge handed out. If it stays different, an edge went missing
  // (buffer overflow, or something this code did not foresee); report the
  // level the pin really has. Timing is lost, the state is not.
  for (uint8_t b = 0; b < kCount; ++b) {
    const uint8_t raw = edgecap::readLevel(b);
    if (raw == edgecap::deliveredLevel(b)) {
      s_mismatch[b] = false;
      continue;
    }
    // Without the interrupt (it failed to install) this polling is all
    // there is, so do not wait.
    const uint32_t patience = edgecap::started() ? cfg::BEAM_RESYNC_MS : 0;
    if (!s_mismatch[b]) {
      s_mismatch[b] = true;
      s_mismatchSince[b] = nowMs;
    }
    if ((uint32_t)(nowMs - s_mismatchSince[b]) < patience) continue;

    s_mismatch[b] = false;
    edgecap::resync(b, raw);
    ++s_resyncs;
    out.beam = b;
    out.blocked = (raw == s_polarity[b].blockedLevel);
    out.ms = nowMs;
    out.uncounted = false;
    return true;
  }
  return false;
}

void discardPending() {
  if (cfg::POWER_SAVE) {
    s_queueCount = 0;
    s_queueHead = 0;
    return;
  }
  edgecap::Edge e;
  while (edgecap::pop(e)) {
  }
  for (uint8_t b = 0; b < kCount; ++b) s_mismatch[b] = false;
}

uint32_t resyncCount() { return s_resyncs; }
uint32_t overflowCount() {
  return cfg::POWER_SAVE ? s_queueOverflows : edgecap::overflowCount();
}
bool captureIsFlashSafe() { return edgecap::flashSafe(); }
uint32_t lateCount(uint8_t beam) { return s_sampler[beam < kCount ? beam : 0].lateCount(); }
uint32_t raiseCount(uint8_t beam) { return s_sampler[beam < kCount ? beam : 0].raiseCount(); }
uint32_t settleUs(uint8_t beam) { return s_sampler[beam < kCount ? beam : 0].settleUs(); }
bool strayLight(uint8_t beam) { return s_stray[beam < kCount ? beam : 0]; }
bool emittersSuspect() { return s_emittersSuspect; }

}  // namespace beams
