// Copies the counting thresholds from config.h into the structure the
// counting logic takes. Kept in a header of its own (plain C++) so that the
// firmware and the native tests use exactly the same mapping.
#pragma once

#include "button_decoder.h"
#include "candle_flicker.h"
#include "config.h"
#include "duck_logic.h"

inline duck::LogicConfig makeLogicConfig() {
  duck::LogicConfig c;
  c.dropDebounceMs    = cfg::DROP_DEBOUNCE_MS;
  c.dropLockoutMs     = cfg::DROP_LOCKOUT_MS;
  c.fullAfterMs       = cfg::DROP_FULL_AFTER_MS;
  c.fullClearMs       = cfg::DROP_FULL_CLEAR_MS;
  c.shelfMinBlockMs   = cfg::SHELF_MIN_BLOCK_MS;
  c.shelfLockoutMs    = cfg::SHELF_LOCKOUT_MS;
  c.shelfStuckMs      = cfg::SHELF_STUCK_MS;
  c.shelfStuckClearMs = cfg::SHELF_STUCK_CLEAR_MS;
  c.tradeWindowMs     = cfg::TRADE_WINDOW_MS;
  return c;
}

inline duck::ButtonConfig makeButtonConfig() {
  duck::ButtonConfig c;
  c.debounceMs  = cfg::BUTTON_DEBOUNCE_MS;
  c.shortMaxMs  = cfg::BUTTON_SHORT_MAX_MS;
  c.doubleGapMs = cfg::BUTTON_DOUBLE_GAP_MS;
  c.holdMs      = cfg::BUTTON_HOLD_RESET_MS;
  return c;
}

inline duck::CandleConfig makeCandleConfig() {
  duck::CandleConfig c;
  c.average    = cfg::LED_AVERAGE_BRIGHTNESS;
  c.depth      = cfg::LED_FLICKER_DEPTH;
  c.flarePeak  = cfg::LED_FLARE_PEAK;
  c.flareDecay = cfg::LED_FLARE_DECAY;
  return c;
}
