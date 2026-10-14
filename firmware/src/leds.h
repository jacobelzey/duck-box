// The four candle LEDs behind the duck emblem.
//
// POWER_SAVE off: four hardware PWM outputs. POWER_SAVE on: plain outputs
// that pulse.* flashes once per cycle for the wanted share of the cycle
// (the PWM unit stops while the processor sleeps; a plain pin does not).
#pragma once

#include <stdint.h>

namespace leds {

// Set up the four outputs, all LEDs off.
void begin();

// Set one LED (0..3) to a duty cycle of 0..1.
void set(uint8_t led, float duty);

void allOff();

}  // namespace leds
