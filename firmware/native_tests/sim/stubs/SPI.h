// Stand-in for SPI.h (PC simulation only). The other end of the bus is a
// pretend SX1276 in sim_board.cpp.
#pragma once

#include <cstdint>

#define SPI_MODE0 0

struct SPISettings {
  SPISettings(uint32_t, uint8_t, uint8_t) {}
};

class SimSPI {
 public:
  void begin(int sck, int miso, int mosi, int ss);
  void end();
  void beginTransaction(const SPISettings&);
  void endTransaction();
  uint8_t transfer(uint8_t out);
};
extern SimSPI SPI;
