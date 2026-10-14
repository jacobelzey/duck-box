// Duck Box - every pin and every tunable number lives in this file.
//
// Plain C++ (no Arduino includes) so the native tests can include it too.
// All names are inside namespace cfg, because the Heltec board package
// already defines global names such as LED, Vext, SS, MOSI and T0..T9.
#pragma once

#include <stdint.h>

namespace cfg {

constexpr const char* FIRMWARE_VERSION = "1.1.1";

// =========================================================================
// PINS
// =========================================================================

// ---- External wiring. FIXED: the wiring diagram is drawn to match. -------
constexpr uint8_t PIN_BEAM_DROP  = 32;  // drop-hole beam receiver output
constexpr uint8_t PIN_BEAM_SHELF = 33;  // pickup-shelf beam receiver output

// Candle LEDs behind the duck emblem: LED1, LED2, LED3, LED4.
// GPIO -> 150 ohm -> LED anode, LED cathode -> GND (pin HIGH = lit).
constexpr uint8_t LED_COUNT = 4;
constexpr uint8_t PIN_LED[LED_COUNT] = {17, 22, 23, 13};

// ---- On the Heltec WiFi LoRa 32 V2 board itself (not wired by you) -------
constexpr uint8_t PIN_OLED_SDA  = 4;
constexpr uint8_t PIN_OLED_SCL  = 15;
constexpr uint8_t PIN_OLED_RST  = 16;
// Vext is the switched 3.3 V rail. It feeds the two Vext header pins (beam
// sensors) AND the on-board OLED, so this firmware keeps it on all the time.
constexpr uint8_t PIN_VEXT_CTRL = 21;
constexpr uint8_t VEXT_ON_LEVEL = 0;    // LOW = Vext on
constexpr uint8_t PIN_BOARD_LED = 25;   // small white LED next to the USB socket
constexpr uint8_t PIN_BUTTON    = 0;    // PRG button, pressed = LOW

constexpr uint8_t PIN_LORA_SS   = 18;
constexpr uint8_t PIN_LORA_MOSI = 27;
constexpr uint8_t PIN_LORA_MISO = 19;
constexpr uint8_t PIN_LORA_SCK  = 5;
constexpr uint8_t PIN_LORA_RST  = 14;
constexpr uint8_t PIN_LORA_DIO0 = 26;
constexpr uint8_t PIN_LORA_DIO1 = 35;
constexpr uint8_t PIN_LORA_DIO2 = 34;

// =========================================================================
// POWER SAVING
// =========================================================================

// Master switch. true = the box runs in cycles of CYCLE_MS: once per cycle
// it lights the beam emitters for a moment, looks at the receivers, gives
// the candle LEDs their pulse of light, and then sleeps until the next
// cycle. false = no power saving (emitters lit all the
// time, beam interrupts, hardware PWM, no sleep, screen always on).
// The native tests build both; override with -DDUCK_POWER_SAVE=0 or 1.
#ifndef DUCK_POWER_SAVE
#define DUCK_POWER_SAVE 1
#endif
constexpr bool POWER_SAVE = (DUCK_POWER_SAVE != 0);

// Supply pin of the two beam EMITTERS: both emitter red wires together on
// this GPIO, both emitter black wires on GND. About 10 mA each while lit,
// 20 mA together, which one pin can supply. (With POWER_SAVE off the pin
// is simply held HIGH, so the same wiring works in both modes.)
//   GPIO12 is a boot-strap pin that must not be pulled HIGH at reset. An
//   emitter (LED + resistor to GND) can only pull it down, which is safe;
//   wiring it to 3V3 by mistake would stop the board from starting.
//   It sits outside the header positions covered by the cartridge ledges.
// The two names exist so that the emitters can be given a pin each; by
// default they are the same pin.
constexpr uint8_t PIN_EMIT_DROP  = 12;
constexpr uint8_t PIN_EMIT_SHELF = 12;

// Length of one cycle. Both beams are checked once per cycle, and the
// candle LEDs flash once per cycle (so 10 ms = 100 flashes a second, too
// fast to see). Shorter = quicker reactions and more battery.
constexpr uint32_t CYCLE_MS = 10;

// How long an emitter is lit before its receiver is read. Measured at
// start-up for the sensors actually fitted (twice the measured response
// plus a margin); this value is used when that measurement fails.
constexpr uint32_t EMIT_SETTLE_US     = 1500;
constexpr uint32_t EMIT_SETTLE_MIN_US = 300;
constexpr uint32_t EMIT_SETTLE_MAX_US = 3000;
// A clear beam is only declared blocked after its receiver has stayed
// dark for this long with the emitter lit (looked at every 0.1 ms). This
// is what keeps a passing shadow from being counted, like DROP_DEBOUNCE_MS
// does with POWER_SAVE off. Settle + confirm must fit into one cycle.
constexpr uint32_t EMIT_CONFIRM_US = 5000;
// While a beam is blocked, one look in this many cycles is such a long
// look (so that a receiver slower than expected is still noticed).
constexpr uint32_t EMIT_RECHECK_CYCLES = 50;
// The settle time is raised when the receiver answers late this many
// looks in a row, and tried shorter again after this many looks in time.
constexpr uint32_t EMIT_LATE_LOOKS_TO_RAISE = 3;
constexpr uint32_t EMIT_RELAX_AFTER_CYCLES  = 6000;

// Put the processor into light sleep for the rest of each cycle.
constexpr bool LIGHT_SLEEP = true;

// Screen while nothing is happening. After an event or a button press the
// screen stays on for SCREEN_STAY_ON_MS and rotates its pages as before.
// After that it shows one page for SCREEN_PEEK_ON_MS out of every
// SCREEN_PEEK_EVERY_MS and is dark in between.
//   SCREEN_PEEK_EVERY_MS = 0 : never goes dark  <- as shipped: the screen
//                              is always on (costs about 5 mA, an estimate)
//   SCREEN_PEEK_ON_MS = 0    : stays dark until the next event
// To get the dark screen with peeks back, set SCREEN_PEEK_EVERY_MS to 20000
// here (or build with -DDUCK_SCREEN_PEEK_EVERY_MS=20000; the simulation
// does that to keep testing it).
#ifndef DUCK_SCREEN_PEEK_EVERY_MS
#define DUCK_SCREEN_PEEK_EVERY_MS 0
#endif
constexpr uint32_t SCREEN_STAY_ON_MS    = 30000;
constexpr uint32_t SCREEN_PEEK_EVERY_MS = DUCK_SCREEN_PEEK_EVERY_MS;
constexpr uint32_t SCREEN_PEEK_ON_MS    = 4000;

// A "POWER ..." line on the serial log this often (0 = never): which share
// of the time the processor was awake, the emitters and LEDs were lit and
// the screen was on.
constexpr uint32_t POWER_REPORT_MS = 60000;

// =========================================================================
// BEAM SENSORS
// =========================================================================

// Which electrical level means "beam blocked".
//   AutoDetect    - measured at power-up. With POWER_SAVE on, the level
//                   is read while the emitter is dark, which is always
//                   right. With POWER_SAVE off BOTH BEAMS MUST BE CLEAR at
//                   that moment or the beam reads inverted until next boot.
//   BlockedIsLow  - output is LOW while something is in the beam.
//   BlockedIsHigh - output is HIGH while something is in the beam.
// Once the beam test screen has told you which one your sensors are, set
// it here so the box no longer depends on the beams being clear at boot.
enum class BeamPolarity : uint8_t { AutoDetect, BlockedIsLow, BlockedIsHigh };

constexpr BeamPolarity DROP_BEAM_POLARITY  = BeamPolarity::AutoDetect;
constexpr BeamPolarity SHELF_BEAM_POLARITY = BeamPolarity::AutoDetect;

// Time between switching Vext on and using what it powers (OLED, beams).
constexpr uint32_t VEXT_SETTLE_MS = 150;
// How long the idle level is sampled for auto-detection (1 sample per ms).
constexpr uint32_t BEAM_POLARITY_SAMPLE_MS = 300;
// Auto-detection is called "unstable" (warning on serial and on the beam
// test screen) if fewer than this percentage of samples agree.
constexpr uint8_t BEAM_POLARITY_MIN_AGREE_PCT = 95;
// Safety net behind the interrupt capture: if a pin has disagreed with the
// last recorded level for this long, the level is taken from the pin.
constexpr uint32_t BEAM_RESYNC_MS = 50;

// =========================================================================
// COUNTING (all times in milliseconds)
// =========================================================================

// --- Drop beam ---
// The beam must stay blocked this long before a break is believed. Must be
// longer than the sensor's 2 ms response and shorter than the quickest
// duck (a falling duck blocks the beam for roughly 20-60 ms).
constexpr uint32_t DROP_DEBOUNCE_MS = 5;
// After a break ends, new breaks are ignored until the beam has stayed
// clear this long. Makes a tumbling or bouncing duck one count.
constexpr uint32_t DROP_LOCKOUT_MS = 400;
// Beam blocked without interruption for this long = chamber FULL.
constexpr uint32_t DROP_FULL_AFTER_MS = 4000;
// FULL ends when the beam has stayed clear this long.
constexpr uint32_t DROP_FULL_CLEAR_MS = 2000;

// --- Shelf beam ---
// Breaks shorter than this are glitches, not a hand.
constexpr uint32_t SHELF_MIN_BLOCK_MS = 60;
// After a hand leaves, breaks starting within this time belong to the same
// visit and are not counted again.
constexpr uint32_t SHELF_LOCKOUT_MS = 1500;
// Beam blocked without interruption for this long = a duck is parked in it.
constexpr uint32_t SHELF_STUCK_MS = 15000;
// "Parked duck" ends when the beam has stayed clear this long.
constexpr uint32_t SHELF_STUCK_CLEAR_MS = 2000;

// --- Trades ---
// A drop and a reach-in at most this far apart, in either order = a trade.
constexpr uint32_t TRADE_WINDOW_MS = 60000;

// =========================================================================
// SCREEN
// =========================================================================

// true = rotate the picture 180 degrees (board mounted upside down).
constexpr bool DISPLAY_FLIP_180 = false;

// OLED contrast, 0..255. Lower = less burn-in and a little less current.
constexpr uint8_t OLED_CONTRAST_NORMAL = 110;
constexpr uint8_t OLED_CONTRAST_DIM    = 1;
// I2C speed for the OLED. One full redraw takes about 25 ms at 400 kHz.
// Try 100000 if the picture is ever garbled.
constexpr uint32_t OLED_I2C_HZ = 400000;
// How often the firmware checks whether the picture needs redrawing.
constexpr uint32_t UI_REFRESH_MS = 50;

// Idle screen: time each page stays up.
constexpr uint32_t IDLE_PAGE_MS = 4000;
// How long an event message ("AHOY! DUCK ABOARD") stays up.
constexpr uint32_t EVENT_BANNER_MS = 3000;
// How long short confirmations (mode change, tally reset) stay up.
constexpr uint32_t NOTICE_BANNER_MS = 1800;
// Burn-in protection: the whole picture moves to the next position of a
// small +/-2 pixel pattern this often.
constexpr uint32_t BURNIN_SHIFT_MS = 60000;

// Boot screens.
constexpr uint32_t SPLASH_MS   = 3000;  // also the window for entering beam test
constexpr uint32_t LAST_RUN_MS = 3000;  // "LAST RUN 12 h 30 min"
// PRG must be seen pressed for this long during the splash to enter beam test.
constexpr uint32_t TEST_ENTRY_HOLD_MS = 60;
// "CHECK BEAMS" warning after a doubtful polarity detection.
constexpr uint32_t BEAM_WARNING_MS = 6000;

// =========================================================================
// CANDLE LEDS
// =========================================================================

constexpr uint32_t LED_PWM_HZ   = 5000;
constexpr uint8_t  LED_PWM_BITS = 12;

// Mean LED duty cycle in Normal mode, 0..1. This is the main battery knob
// for the LEDs: each LED draws about 8 mA when fully on (3.3 V, 150 ohm,
// yellow), so 0.30 means roughly 10 mA for all four together. With
// POWER_SAVE on, the processor also has to stay awake while an LED is lit,
// so a brighter candle costs a little more than the LED current alone.
constexpr float LED_AVERAGE_BRIGHTNESS = 0.30f;
// Dim mode multiplies the brightness by this.
constexpr float LED_DIM_FACTOR = 0.35f;
// How lively the flame is: 0 = steady light, 0.8 = stormy.
constexpr float LED_FLICKER_DEPTH = 0.55f;
// Brightness reached by the short flare on a drop / reach-in / trade, 0..1.
constexpr float LED_FLARE_PEAK = 1.0f;
// Flicker update period. The flicker constants are tuned for 20 ms.
constexpr uint32_t LED_STEP_MS = 20;
// Flare fades by this factor every step (0.90 at 20 ms = about half a second).
constexpr float LED_FLARE_DECAY = 0.90f;

// =========================================================================
// BUTTON (PRG)
// =========================================================================

constexpr uint32_t BUTTON_DEBOUNCE_MS   = 30;
// A press shorter than this is a "press"; longer starts the reset countdown.
constexpr uint32_t BUTTON_SHORT_MAX_MS  = 700;
// Longest pause between the two presses of a double press. A single press
// is acted on only after this pause has passed.
constexpr uint32_t BUTTON_DOUBLE_GAP_MS = 400;
// Total hold time to reset all tallies.
constexpr uint32_t BUTTON_HOLD_RESET_MS = 5000;

// Length of the "restocking" pause started by a double press.
constexpr uint32_t RESTOCK_PAUSE_MS = 180000;

// Beam test mode: time each LED stays lit in the chase, and its brightness.
constexpr uint32_t TEST_LED_STEP_MS = 1000;
constexpr float    TEST_LED_BRIGHTNESS = 0.8f;
// Beam test mode ends by itself after this long, so that a box left in it
// by accident goes back to counting.
constexpr uint32_t BEAM_TEST_TIMEOUT_MS = 900000;  // 15 minutes

// =========================================================================
// SAVING TO FLASH
// =========================================================================

// Tallies are written this long after the LAST change (a burst of events
// is one write)...
constexpr uint32_t SAVE_QUIET_MS = 5000;
// ...but never later than this after the FIRST unsaved change.
constexpr uint32_t SAVE_MAX_WAIT_MS = 60000;
// Running time is written this often. Also the resolution of "LAST RUN".
constexpr uint32_t RUNTIME_SAVE_MS = 600000;  // 10 minutes

// =========================================================================
// POWER / MISC
// =========================================================================

// CPU clock. 80 MHz is the lowest speed that keeps the peripheral clock
// (PWM, I2C, serial) unchanged. Keep equal to board_build.f_cpu in
// platformio.ini.
constexpr uint32_t CPU_FREQ_MHZ = 80;
// Main loop idle time with POWER_SAVE off. Beam edges are timestamped in
// an interrupt there, so this does not affect counting accuracy.
constexpr uint32_t LOOP_DELAY_MS = 2;

constexpr uint32_t SERIAL_BAUD = 115200;

}  // namespace cfg
