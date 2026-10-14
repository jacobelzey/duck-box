#include "radios.h"

#include <Arduino.h>
#include <SPI.h>

#include "config.h"
#include "esp_bt.h"

namespace radios {

namespace {

// SX1276 registers (Semtech SX1276/77/78/79 datasheet, register tables).
constexpr uint8_t kRegOpMode = 0x01;   // bits 2..0 = mode, 000 = Sleep
constexpr uint8_t kRegVersion = 0x42;  // reads 0x12 on an SX1276
constexpr uint8_t kModeMask = 0x07;
constexpr uint8_t kWriteFlag = 0x80;   // first SPI byte: bit 7 = 1 for write

uint8_t loraTransfer(uint8_t address, uint8_t value) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(cfg::PIN_LORA_SS, LOW);
  SPI.transfer(address);
  const uint8_t reply = SPI.transfer(value);
  digitalWrite(cfg::PIN_LORA_SS, HIGH);
  SPI.endTransaction();
  return reply;
}

uint8_t loraRead(uint8_t reg) { return loraTransfer(reg & 0x7F, 0x00); }
void loraWrite(uint8_t reg, uint8_t value) { loraTransfer(reg | kWriteFlag, value); }

}  // namespace

LoraSleepResult loraSleep() {
  LoraSleepResult r = {false, false, 0, 0};

  pinMode(cfg::PIN_LORA_SS, OUTPUT);
  digitalWrite(cfg::PIN_LORA_SS, HIGH);

  // Manual reset as the datasheet describes it: pull NRESET low for more
  // than 100 us, release it (high impedance), wait at least 5 ms.
  pinMode(cfg::PIN_LORA_RST, OUTPUT);
  digitalWrite(cfg::PIN_LORA_RST, LOW);
  delay(2);
  pinMode(cfg::PIN_LORA_RST, INPUT);
  delay(10);

  SPI.begin(cfg::PIN_LORA_SCK, cfg::PIN_LORA_MISO, cfg::PIN_LORA_MOSI, cfg::PIN_LORA_SS);

  r.version = loraRead(kRegVersion);
  r.chipFound = (r.version == 0x12);

  // After reset the chip is in Standby (about 1.6 mA). Clear only the mode
  // bits, which selects Sleep (about 0.2 uA), and leave the rest as it is.
  const uint8_t before = loraRead(kRegOpMode);
  loraWrite(kRegOpMode, (uint8_t)(before & ~kModeMask));
  delay(2);
  r.opMode = loraRead(kRegOpMode);
  r.asleep = r.chipFound && ((r.opMode & kModeMask) == 0);

  // Park the bus so that no input of either chip is left floating: chip
  // select high (deselected), clock and data low, and a pull-down on MISO,
  // which the SX1276 stops driving while it is deselected.
  SPI.end();
  pinMode(cfg::PIN_LORA_SS, OUTPUT);
  digitalWrite(cfg::PIN_LORA_SS, HIGH);
  pinMode(cfg::PIN_LORA_SCK, OUTPUT);
  digitalWrite(cfg::PIN_LORA_SCK, LOW);
  pinMode(cfg::PIN_LORA_MOSI, OUTPUT);
  digitalWrite(cfg::PIN_LORA_MOSI, LOW);
  pinMode(cfg::PIN_LORA_MISO, INPUT_PULLDOWN);
  // The DIO lines are outputs of the radio; leave them as plain inputs.
  pinMode(cfg::PIN_LORA_DIO0, INPUT);
  pinMode(cfg::PIN_LORA_DIO1, INPUT);
  pinMode(cfg::PIN_LORA_DIO2, INPUT);

  return r;
}

bool bluetoothIsIdle() {
  return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE;
}

}  // namespace radios
