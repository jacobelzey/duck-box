// The hardware side of POWER_SAVE: one short "window" per cycle in which
// the candle LEDs get their flash of light and the beam emitters are lit
// just long enough for the receivers to be read.
//
// Nothing here uses a hardware timer or PWM unit. Every pin is switched by
// the main loop itself, at times measured with micros(). That is what lets
// the processor sleep for the rest of the cycle: in light sleep the PWM
// unit stops, but a pin that was set LOW (or HIGH) simply stays that way.
#pragma once

#include <stdint.h>

namespace pulse {

constexpr uint8_t kBeams = 2;       // 0 = drop, 1 = shelf (as in beams.h)
constexpr uint32_t kNever = 0xFFFFFFFFu;

// Emitter and LED pins as outputs, everything dark. Receiver pins as
// inputs with pull-up.
void begin();

// ---- candle LEDs ---------------------------------------------------------

// How long LED `led` (0..3) is lit in each window, in microseconds.
// 0 = dark; anything close to a whole cycle = lit all the time.
void setLedOnUs(uint8_t led, uint32_t onUs);
// All LEDs dark, now.
void ledsOff();

// ---- beams ---------------------------------------------------------------

struct Look {
  // in
  bool wanted;         // false = this beam is not looked at this window
  uint32_t settleUs;   // read the receiver this long after lighting up
  uint32_t giveUpUs;   // if dark then, keep looking until this long
  uint8_t darkLevel;   // receiver level that means "no light"
  // out
  uint8_t levelBefore;   // receiver level just before the emitter was lit
  uint32_t seenAfterUs;  // light seen: exactly settleUs if it was there at
                         // the first read, the later time if it appeared
                         // while waiting; kNever if it never did
  uint8_t level;         // receiver level at the last read
};

// Run one window: first light the wanted emitters and read the receivers
// (with the candle dark, so that its light cannot be mistaken for a beam),
// then flash the LEDs, switching each thing off at its own moment. Returns
// when the last pin has been switched: a millisecond or so with a dim
// candle, up to one cycle when the LEDs are bright.
void window(Look looks[kBeams]);

// Light one emitter and time how long its receiver takes to leave
// `darkLevel`. Returns kNever if it has not after `timeoutUs`.
uint32_t measureResponse(uint8_t beam, uint8_t darkLevel, uint32_t timeoutUs);

// Receiver level right now (emitter state as it is).
uint8_t readLevel(uint8_t beam);

// Both emitters lit / dark until further notice (POWER_SAVE off).
void emittersSteady(bool on);

// ---- book-keeping for the POWER report -------------------------------------

// Microseconds each emitter / all LEDs together were lit since the last
// call (LEDs: sum over the four, so a whole cycle with all on = 4 cycles).
uint32_t takeEmitterUs(uint8_t beam);
uint32_t takeLedUs();

}  // namespace pulse
