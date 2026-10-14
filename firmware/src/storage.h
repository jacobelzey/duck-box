// Values that survive power loss (ESP32 NVS, through Preferences).
#pragma once

#include <stdint.h>

#include "duck_logic.h"

namespace storage {

// Open the store. Returns false if flash storage is unavailable; the box
// then still works, it just forgets everything at power-off.
bool begin();

duck::Counters loadCounters();
void saveCounters(const duck::Counters& counters);

// Running time, in minutes, kept as two records:
//   "current"  - how long the run in progress has lasted. Written every
//                cfg::RUNTIME_SAVE_MS, so it is 0 until the first save.
//   "previous" - the length of the last run that got as far as a save.
// At start-up a non-zero "current" belongs to the run that just ended and
// is moved to "previous". A run that dies before its first save therefore
// leaves "previous" untouched, which is what keeps the reading intact when
// flat batteries make the box restart over and over.
uint32_t loadCurrentRunMinutes();
void saveCurrentRunMinutes(uint32_t minutes);
uint32_t loadPreviousRunMinutes();
void savePreviousRunMinutes(uint32_t minutes);

}  // namespace storage
