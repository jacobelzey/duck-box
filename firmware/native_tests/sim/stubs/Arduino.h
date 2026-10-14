// Stand-in for the Arduino core, for the PC simulation only (see
// ../sim_board.h). Declares just what this firmware uses.
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09
#define MSBFIRST 1

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t level);
int digitalRead(uint8_t pin);
unsigned long millis();
unsigned long micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);

uint32_t ledcSetup(uint8_t channel, uint32_t freq, uint8_t bits);
void ledcAttachPin(uint8_t pin, uint8_t channel);
void ledcWrite(uint8_t channel, uint32_t duty);

bool setCpuFrequencyMhz(uint32_t mhz);
uint32_t getCpuFrequencyMhz();

class SimSerial {
 public:
  void begin(unsigned long baud);
  void print(const char* s);
  void print(char c);
  void println(const char* s);
  void println();
  void printf(const char* format, ...) __attribute__((format(printf, 2, 3)));
};
extern SimSerial Serial;

void setup();
void loop();
