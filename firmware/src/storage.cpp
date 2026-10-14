#include "storage.h"

#include <Preferences.h>

namespace storage {

namespace {
Preferences s_prefs;
bool s_open = false;

// NVS keys are limited to 15 characters.
constexpr const char* kNamespace = "duckbox";
constexpr const char* kKeyDrops = "drops";
constexpr const char* kKeyReach = "reach";
constexpr const char* kKeyTrades = "trades";
constexpr const char* kKeyRunCurrent = "run_cur";
constexpr const char* kKeyRunPrevious = "run_prev";

// Write only when the value differs from what is stored, so that calling a
// save function with unchanged data costs no flash wear.
void putIfChanged(const char* key, uint32_t value) {
  if (!s_open) return;
  if (s_prefs.isKey(key) && s_prefs.getUInt(key, 0) == value) return;
  s_prefs.putUInt(key, value);
}

uint32_t get(const char* key) {
  // isKey() first: getUInt() on a missing key logs an error on some cores.
  if (!s_open || !s_prefs.isKey(key)) return 0;
  return s_prefs.getUInt(key, 0);
}
}  // namespace

bool begin() {
  s_open = s_prefs.begin(kNamespace, /*readOnly=*/false);
  return s_open;
}

duck::Counters loadCounters() {
  duck::Counters c;
  c.drops = get(kKeyDrops);
  c.reachIns = get(kKeyReach);
  c.trades = get(kKeyTrades);
  return c;
}

void saveCounters(const duck::Counters& counters) {
  putIfChanged(kKeyDrops, counters.drops);
  putIfChanged(kKeyReach, counters.reachIns);
  putIfChanged(kKeyTrades, counters.trades);
}

uint32_t loadCurrentRunMinutes() { return get(kKeyRunCurrent); }
void saveCurrentRunMinutes(uint32_t minutes) { putIfChanged(kKeyRunCurrent, minutes); }
uint32_t loadPreviousRunMinutes() { return get(kKeyRunPrevious); }
void savePreviousRunMinutes(uint32_t minutes) { putIfChanged(kKeyRunPrevious, minutes); }

}  // namespace storage
