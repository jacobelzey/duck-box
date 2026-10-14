// Everything to do with radios, which this box does not use:
// LoRa chip to sleep, Bluetooth confirmed idle. There is no WiFi function
// here on purpose: nothing in this firmware refers to the WiFi driver, so
// the linker leaves it out of the program altogether (README, "Power").
#pragma once

#include <stdint.h>

namespace radios {

struct LoraSleepResult {
  bool chipFound;     // version register answered 0x12 (SX1276)
  bool asleep;        // mode bits read back as 000 (Sleep)
  uint8_t version;    // RegVersion as read
  uint8_t opMode;     // RegOpMode as read back after the write
};

// Reset the SX1276 and put it into Sleep mode, then park its SPI pins.
LoraSleepResult loraSleep();

// True if the Bluetooth controller is idle, i.e. was never started. (The
// Arduino core hands the controller's memory back at start-up when a
// sketch does not use Bluetooth, after which it cannot be started at all,
// so this is a check, not a switch.)
bool bluetoothIsIdle();

}  // namespace radios
