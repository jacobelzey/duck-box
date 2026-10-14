// A pretend Heltec board for running the whole firmware on a PC.
//
// The real src/*.cpp files are compiled unchanged against the small stand-in
// headers in stubs/. Everything they would do to hardware lands here:
// a simulated clock, pin levels, PWM duties, an NVS store, the SX1276
// registers, the serial log and the picture sent to the OLED.
//
// This is a test aid, not an emulator: it models only what the firmware
// uses, and timing only where it matters (delay(), I2C traffic to the OLED
// and flash writes take simulated time, during which beam interrupts keep
// firing - which is exactly the situation the firmware must cope with).
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sim {

constexpr int kPins = 40;

struct Board {
  // --- time ---
  uint64_t nowUs = 0;

  // --- pins ---
  bool isOutput[kPins] = {};
  uint8_t outputLevel[kPins] = {};   // what the firmware drives
  uint8_t externalLevel[kPins];      // what the outside world applies to inputs
  void (*isr[kPins])(void*) = {};
  void* isrArg[kPins] = {};
  bool isrServiceInstalled = false;

  // --- beam sensors -----------------------------------------------------
  // A receiver sees light while its emitter pin is driven HIGH and nothing
  // is in the beam, `beamResponseUs` after both became true. Its output
  // then shows the opposite of `beamDarkLevel`. (index 0 = drop, 1 = shelf)
  bool beamBlocked[2] = {false, false};
  uint8_t beamDarkLevel[2] = {0, 0};        // output level without light
  uint32_t beamResponseUs[2] = {250, 250};  // light on -> output changes
  uint32_t beamFallUs[2] = {400, 400};      // light off -> output changes back
  bool beamStrayLight[2] = {false, false};  // e.g. sunlight on the receiver
  // Emitters wired wrongly: to Vext, lit whenever Vext is on,
  // whatever the emitter pin does.
  bool emittersOnVext = false;
  // The wire from the emitter pin to the emitters has come off.
  bool emitterWireOpen = false;
  bool beamPath[2] = {false, false};        // emitter lit and beam free
  uint64_t beamPathSinceUs[2] = {0, 0};
  uint64_t beamLightUntilUs[2] = {0, 0};    // output still "lit" until then
  uint8_t beamDelivered[2] = {0, 0};        // level last signalled by interrupt

  // --- how long things take on the real chip ------------------------------
  uint32_t microsCostUs = 1;                // each micros() / pin read
  uint64_t nvsStallNextWriteUs = 0;         // extra time for the next flash write
                                            // (a page erase), then reset to 0

  // --- how long output pins have been HIGH (LED and emitter duty) ---------
  uint64_t highTotalUs[kPins] = {};
  uint64_t roseAtUs[kPins] = {};
  std::deque<std::pair<uint64_t, uint64_t>> highSpans[kPins];  // recent, finished

  // --- light sleep ---
  uint64_t sleepTimerUs = 0;
  uint64_t sleptUs = 0;
  uint32_t sleeps = 0;

  // --- PWM (LEDC) ---
  int channelPin[16];
  uint32_t channelDuty[16] = {};
  uint8_t channelBits[16] = {};

  // --- OLED, decoded from the I2C traffic ---
  bool displayOn = false;
  int contrast = -1;
  uint8_t screen[64][128] = {};      // last picture sent, 1 = lit
  uint32_t framesSent = 0;           // pictures started at the top left corner
  uint32_t stripsSent = 0;           // rows of 8 pixels started at the left edge

  // --- SX1276 ---
  uint8_t loraOpMode = 0x09;         // after reset: FSK, standby
  uint32_t loraResets = 0;

  // --- flash (NVS) ---
  std::map<std::string, uint32_t> nvs;
  uint32_t nvsWrites = 0;
  std::string nvsFile;               // survives "power cycles" between phases

  // --- serial ---
  std::string serial;
  bool echoSerial = false;

  uint32_t cpuMhz = 240;

  Board();
};

Board& board();

// Advance the simulated clock, delivering scheduled pin changes (and their
// interrupts) at their exact times on the way.
void advanceUs(uint64_t us);

// Schedule the outside world changing an input pin, `afterMs` from now.
// (Not for the beam receivers: use scheduleBlocked.)
void scheduleLevel(double afterMs, int pin, uint8_t level);

// Schedule something entering (true) or leaving (false) a beam.
void scheduleBlocked(double afterMs, int beam, bool blocked);
// Put something into / take it out of a beam right now without the beam
// interrupt noticing (to test the polling safety net).
void blockSilently(int beam, bool blocked);
// Call after changing emitterWireOpen, emittersOnVext or beamStrayLight
// while the firmware is running.
void beamsChanged();

// Duty of the LED on `pin`, 0..1: the PWM setting if a PWM channel drives
// the pin, otherwise the share of the last 30 ms the pin was HIGH.
double ledDuty(int pin);
// Total time an output pin has been HIGH since power-up, microseconds.
uint64_t highUs(int pin);
// OLED: pictures started (data written at the top left corner).
// Screen content is taken from the bytes actually sent.

// Lines of serial output that contain `text`.
int countLines(const std::string& text);
std::string lastLineWith(const std::string& text);

// Save the last picture sent to the OLED as a 4x enlarged .bmp file.
void saveScreen(const std::string& path);
// Number of lit pixels in the last picture.
int litPixels();

void loadNvs();
void storeNvs();

}  // namespace sim
