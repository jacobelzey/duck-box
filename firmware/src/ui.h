// Screen layouts for the 128x64 OLED.
//
// This file only draws: it is handed text and numbers and lays them out as
// large as they will fit. What to show, and when, is decided in main.cpp.
// It depends on U8g2 and nothing else, so the same code is also built into
// the PC simulation (native_tests/sim), which renders every screen to an
// image and checks that nothing is clipped.
#pragma once

#include <stdint.h>

#include <U8g2lib.h>

namespace ui {

// One row of the beam test screen.
struct BeamRow {
  bool blocked;          // interpreted state
  uint8_t rawLevel;      // electrical level right now, 0 or 1
  uint8_t blockedLevel;  // which level means "blocked", 0 or 1
  bool forced;           // polarity fixed in config.h (not auto-detected)
  bool unstable;         // auto-detection saw a mix of levels
  uint32_t breaks;       // breaks seen since the test started
};

struct BeamTestView {
  BeamRow drop;
  BeamRow shelf;
  uint8_t ledNumber;     // 1..4, the LED lit right now
  uint8_t ledPin;        // its GPIO number
};

class Screen {
 public:
  explicit Screen(U8G2& display) : d_(display) {}

  // Burn-in protection: everything drawn afterwards is moved by this many
  // pixels. Layouts keep a 2 pixel margin, so +/-2 never clips.
  void setShift(int8_t dx, int8_t dy) {
    dx_ = dx;
    dy_ = dy;
  }

  // Each of these clears the frame buffer and draws one complete screen
  // into it. The caller sends the buffer to the display.

  // One or two lines of text, each as large as it will fit. line2 may be
  // nullptr or empty.
  void message(const char* line1, const char* line2);

  // The same with a progress bar along the bottom (percent 0..100).
  void messageWithBar(const char* line1, const char* line2, uint8_t percent);

  // A small caption with a very large number under it.
  void tally(const char* label, uint32_t value);

  // Sensor alignment screen (small text; for the owner, not passers-by).
  void beamTest(const BeamTestView& view);

 private:
  void drawLines(const char* line1, const char* line2, int areaHeight);
  void drawCentered(const uint8_t* font, const char* text, int baseline);

  U8G2& d_;
  int8_t dx_ = 0;
  int8_t dy_ = 0;
};

}  // namespace ui
