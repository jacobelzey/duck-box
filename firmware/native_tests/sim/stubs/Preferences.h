// Stand-in for Preferences.h (PC simulation only): a map in memory that is
// also written to a file so that it survives simulated power cycles.
#pragma once

#include <cstddef>
#include <cstdint>

class Preferences {
 public:
  bool begin(const char* name, bool readOnly = false);
  bool isKey(const char* key);
  uint32_t getUInt(const char* key, uint32_t defaultValue = 0);
  size_t putUInt(const char* key, uint32_t value);
};
