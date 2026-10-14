// The pretend board behind the stand-in headers in stubs/. See sim_board.h.
#include "sim_board.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <U8g2lib.h>

#include "config.h"
#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

namespace sim {

namespace {

struct SimEvent {
  enum Kind { PinLevel, BeamBlocked, BeamRecheck } kind;
  int index;       // pin, or beam
  uint8_t value;   // level, or blocked
};
std::multimap<uint64_t, SimEvent> g_events;

// Pins of the SX1276 that the pretend radio watches (same as config.h; the
// simulation checks behaviour, the unit tests check the numbers).
constexpr int kLoraResetPin = 14;

const int kEmitPin[2] = {cfg::PIN_EMIT_DROP, cfg::PIN_EMIT_SHELF};
const int kRxPin[2] = {cfg::PIN_BEAM_DROP, cfg::PIN_BEAM_SHELF};

int beamOfRxPin(int pin) {
  for (int b = 0; b < 2; ++b) {
    if (pin == kRxPin[b]) return b;
  }
  return -1;
}

bool isEmitPin(int pin) { return pin == kEmitPin[0] || pin == kEmitPin[1]; }

bool emitterLit(int beam) {
  const Board& b = board();
  if (b.emitterWireOpen) return false;
  if (b.emittersOnVext) {
    return b.isOutput[cfg::PIN_VEXT_CTRL] && b.outputLevel[cfg::PIN_VEXT_CTRL] == cfg::VEXT_ON_LEVEL;
  }
  const int pin = kEmitPin[beam];
  return b.isOutput[pin] && b.outputLevel[pin];
}

// Output level of a receiver right now.
uint8_t rxLevel(int beam) {
  const Board& b = board();
  const bool light = b.beamStrayLight[beam] ||
                     (b.beamPath[beam] &&
                      b.nowUs - b.beamPathSinceUs[beam] >= b.beamResponseUs[beam]) ||
                     b.nowUs < b.beamLightUntilUs[beam];
  return light ? (uint8_t)!b.beamDarkLevel[beam] : b.beamDarkLevel[beam];
}

// Tell the firmware's interrupt handler (if any) about a receiver change.
void deliverRx(int beam) {
  Board& b = board();
  const uint8_t level = rxLevel(beam);
  if (level == b.beamDelivered[beam]) return;
  b.beamDelivered[beam] = level;
  const int pin = kRxPin[beam];
  if (b.isr[pin] != nullptr) b.isr[pin](b.isrArg[pin]);
}

// The emitter was switched or something moved in the beam.
void refreshBeam(int beam) {
  Board& b = board();
  const bool path = emitterLit(beam) && !b.beamBlocked[beam];
  if (path && !b.beamPath[beam]) {
    b.beamPathSinceUs[beam] = b.nowUs;
    g_events.insert({b.nowUs + b.beamResponseUs[beam],
                     SimEvent{SimEvent::BeamRecheck, beam, 0}});
  } else if (!path && b.beamPath[beam]) {
    // The light is gone; a receiver that had answered takes a moment to
    // let go again.
    if (b.nowUs - b.beamPathSinceUs[beam] >= b.beamResponseUs[beam]) {
      b.beamLightUntilUs[beam] = b.nowUs + b.beamFallUs[beam];
      g_events.insert({b.beamLightUntilUs[beam], SimEvent{SimEvent::BeamRecheck, beam, 0}});
    }
  }
  b.beamPath[beam] = path;
  deliverRx(beam);
}

void refreshBeams() {
  refreshBeam(0);
  refreshBeam(1);
}

// Book-keeping of how long an output pin is HIGH.
void noteOutput(int pin, bool high) {
  Board& b = board();
  const bool was = b.roseAtUs[pin] != 0;
  if (high && !was) {
    b.roseAtUs[pin] = b.nowUs + 1;                 // +1 so that 0 means "low"
  } else if (!high && was) {
    const uint64_t rose = b.roseAtUs[pin] - 1;
    b.highTotalUs[pin] += b.nowUs - rose;
    b.highSpans[pin].push_back({rose, b.nowUs});
    while (b.highSpans[pin].size() > 64) b.highSpans[pin].pop_front();
    b.roseAtUs[pin] = 0;
  }
}

uint8_t inputLevel(int pin) {
  const Board& b = board();
  if (b.isOutput[pin]) return b.outputLevel[pin];
  const int beam = beamOfRxPin(pin);
  return beam >= 0 ? rxLevel(beam) : b.externalLevel[pin];
}

}  // namespace

Board::Board() {
  for (int i = 0; i < kPins; ++i) externalLevel[i] = 1;  // pull-ups: idle high
  for (int i = 0; i < 16; ++i) channelPin[i] = -1;
}

Board& board() {
  static Board b;
  return b;
}

void advanceUs(uint64_t us) {
  Board& b = board();
  const uint64_t target = b.nowUs + us;
  while (!g_events.empty() && g_events.begin()->first <= target) {
    const uint64_t when = g_events.begin()->first;
    const SimEvent ev = g_events.begin()->second;
    g_events.erase(g_events.begin());
    if (when > b.nowUs) b.nowUs = when;
    switch (ev.kind) {
      case SimEvent::PinLevel: {
        const uint8_t old = b.externalLevel[ev.index];
        b.externalLevel[ev.index] = ev.value;
        // An any-edge GPIO interrupt, delivered at the moment of the edge.
        if (ev.value != old && b.isr[ev.index] != nullptr) b.isr[ev.index](b.isrArg[ev.index]);
        break;
      }
      case SimEvent::BeamBlocked:
        b.beamBlocked[ev.index] = ev.value != 0;
        refreshBeam(ev.index);
        break;
      case SimEvent::BeamRecheck:
        deliverRx(ev.index);
        break;
    }
  }
  b.nowUs = target;
}

void scheduleLevel(double afterMs, int pin, uint8_t level) {
  const uint64_t when = board().nowUs + (uint64_t)(afterMs * 1000.0);
  g_events.insert({when, SimEvent{SimEvent::PinLevel, pin, level}});
}

void scheduleBlocked(double afterMs, int beam, bool blocked) {
  const uint64_t when = board().nowUs + (uint64_t)(afterMs * 1000.0);
  g_events.insert({when, SimEvent{SimEvent::BeamBlocked, beam, (uint8_t)blocked}});
}

void blockSilently(int beam, bool blocked) {
  Board& b = board();
  b.beamBlocked[beam] = blocked;
  const bool path = emitterLit(beam) && !blocked;
  if (path && !b.beamPath[beam]) b.beamPathSinceUs[beam] = b.nowUs;
  b.beamPath[beam] = path;
  b.beamLightUntilUs[beam] = 0;
  b.beamDelivered[beam] = path ? (uint8_t)!b.beamDarkLevel[beam] : b.beamDarkLevel[beam];
}

void beamsChanged() { refreshBeams(); }

uint64_t highUs(int pin) {
  const Board& b = board();
  uint64_t total = b.highTotalUs[pin];
  if (b.roseAtUs[pin] != 0) total += b.nowUs - (b.roseAtUs[pin] - 1);
  return total;
}

double ledDuty(int pin) {
  const Board& b = board();
  for (int ch = 0; ch < 16; ++ch) {
    if (b.channelPin[ch] != pin) continue;
    const double full = (double)((1u << b.channelBits[ch]) - 1u);
    const double d = (double)b.channelDuty[ch] / full;
    return d > 1.0 ? 1.0 : d;
  }
  // No PWM channel: a plain output flashed by the firmware. Share of the
  // last 30 ms (three cycles) it was HIGH.
  const uint64_t window = 30000;
  const uint64_t from = b.nowUs > window ? b.nowUs - window : 0;
  uint64_t high = 0;
  for (const auto& span : b.highSpans[pin]) {
    if (span.second <= from) continue;
    high += span.second - (span.first > from ? span.first : from);
  }
  if (b.roseAtUs[pin] != 0) {
    const uint64_t rose = b.roseAtUs[pin] - 1;
    high += b.nowUs - (rose > from ? rose : from);
  }
  return (double)high / (double)(b.nowUs - from ? b.nowUs - from : 1);
}

int countLines(const std::string& text) {
  std::istringstream in(board().serial);
  std::string line;
  int n = 0;
  while (std::getline(in, line)) n += (line.find(text) != std::string::npos);
  return n;
}

std::string lastLineWith(const std::string& text) {
  std::istringstream in(board().serial);
  std::string line, found;
  while (std::getline(in, line)) {
    if (line.find(text) != std::string::npos) found = line;
  }
  return found;
}

int litPixels() {
  int n = 0;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 128; ++x) n += board().screen[y][x];
  }
  return n;
}

// 24-bit BMP, each OLED pixel drawn as a 4x4 block, with a dark border so
// that it is obvious how close the picture comes to the edge of the glass.
void saveScreen(const std::string& path) {
  const int scale = 4, border = 8;
  const int w = 128 * scale + 2 * border, h = 64 * scale + 2 * border;
  const int rowBytes = (w * 3 + 3) & ~3;
  const uint32_t dataSize = (uint32_t)(rowBytes * h);
  unsigned char header[54] = {'B', 'M'};
  auto put32 = [&](int at, uint32_t v) {
    for (int i = 0; i < 4; ++i) header[at + i] = (unsigned char)(v >> (8 * i));
  };
  put32(2, 54 + dataSize);
  put32(10, 54);
  put32(14, 40);
  put32(18, (uint32_t)w);
  put32(22, (uint32_t)h);
  header[26] = 1;
  header[28] = 24;
  put32(34, dataSize);

  std::ofstream out(path, std::ios::binary);
  out.write((const char*)header, sizeof(header));
  std::string row((size_t)rowBytes, '\0');
  for (int y = h - 1; y >= 0; --y) {  // BMP rows run bottom to top
    for (int x = 0; x < w; ++x) {
      const int sx = (x - border) / scale, sy = (y - border) / scale;
      const bool inside = x >= border && y >= border && sx < 128 && sy < 64;
      unsigned char r = 40, g = 40, bl = 48;         // border
      if (inside) {
        const bool lit = board().displayOn && board().screen[sy][sx];
        r = lit ? 255 : 0;                           // OLED: white on black
        g = lit ? 255 : 0;
        bl = lit ? 255 : 0;
      }
      row[(size_t)x * 3 + 0] = (char)bl;
      row[(size_t)x * 3 + 1] = (char)g;
      row[(size_t)x * 3 + 2] = (char)r;
    }
    out.write(row.data(), rowBytes);
  }
}

void loadNvs() {
  Board& b = board();
  if (b.nvsFile.empty()) return;
  std::ifstream in(b.nvsFile);
  std::string key;
  uint32_t value;
  b.nvs.clear();
  while (in >> key >> value) b.nvs[key] = value;
}

void storeNvs() {
  const Board& b = board();
  if (b.nvsFile.empty()) return;
  std::ofstream out(b.nvsFile, std::ios::trunc);
  for (const auto& kv : b.nvs) out << kv.first << ' ' << kv.second << '\n';
}

}  // namespace sim

using sim::board;

// ============================================================ Arduino core

void pinMode(uint8_t pin, uint8_t mode) {
  if (pin >= sim::kPins) return;
  sim::Board& b = board();
  b.isOutput[pin] = (mode == OUTPUT);
  sim::noteOutput(pin, b.isOutput[pin] && b.outputLevel[pin]);
  if (sim::isEmitPin(pin) || pin == cfg::PIN_VEXT_CTRL) sim::refreshBeams();
}

void digitalWrite(uint8_t pin, uint8_t level) {
  if (pin >= sim::kPins) return;
  sim::Board& b = board();
  if (pin == sim::kLoraResetPin && b.isOutput[pin] && level == LOW) {
    ++b.loraResets;
    b.loraOpMode = 0x09;  // datasheet reset value: FSK mode, standby
  }
  b.outputLevel[pin] = level ? 1 : 0;
  sim::noteOutput(pin, b.isOutput[pin] && b.outputLevel[pin]);
  if (sim::isEmitPin(pin) || pin == cfg::PIN_VEXT_CTRL) sim::refreshBeams();
}

int digitalRead(uint8_t pin) {
  if (pin >= sim::kPins) return 0;
  sim::advanceUs(board().microsCostUs);        // reading a pin takes a moment
  return sim::inputLevel(pin);
}

unsigned long millis() { return (unsigned long)(uint32_t)(board().nowUs / 1000u); }
// Asking the time takes time: on the chip, two calls to micros() never
// return the same value, and code that assumes they might is wrong.
unsigned long micros() {
  sim::advanceUs(board().microsCostUs);
  return (unsigned long)(uint32_t)board().nowUs;
}
void delay(uint32_t ms) { sim::advanceUs((uint64_t)ms * 1000u); }
void delayMicroseconds(uint32_t us) { sim::advanceUs(us); }

uint32_t ledcSetup(uint8_t channel, uint32_t freq, uint8_t bits) {
  if (channel < 16) board().channelBits[channel] = bits;
  return freq;
}
void ledcAttachPin(uint8_t pin, uint8_t channel) {
  if (channel < 16) board().channelPin[channel] = pin;
}
void ledcWrite(uint8_t channel, uint32_t duty) {
  if (channel < 16) board().channelDuty[channel] = duty;
}

bool setCpuFrequencyMhz(uint32_t mhz) {
  board().cpuMhz = mhz;
  return true;
}
uint32_t getCpuFrequencyMhz() { return board().cpuMhz; }

SimSerial Serial;

static void serialWrite(const char* s) {
  board().serial += s;
  if (board().echoSerial) std::fputs(s, stdout);
}
void SimSerial::begin(unsigned long) {}
void SimSerial::print(const char* s) { serialWrite(s); }
void SimSerial::print(char c) {
  const char s[2] = {c, '\0'};
  serialWrite(s);
}
void SimSerial::println(const char* s) {
  serialWrite(s);
  serialWrite("\n");
}
void SimSerial::println() { serialWrite("\n"); }
void SimSerial::printf(const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  serialWrite(buffer);
}

// ================================================================== ESP-IDF

uint32_t esp_random() { return 0xC0FFEE11u; }
esp_bt_controller_status_t esp_bt_controller_get_status() { return ESP_BT_CONTROLLER_STATUS_IDLE; }
int64_t esp_timer_get_time() {
  sim::advanceUs(board().microsCostUs);
  return (int64_t)board().nowUs;
}

uint32_t sim_reg_read(int reg) {
  uint32_t value = 0;
  const int first = (reg == GPIO_IN1_REG) ? 32 : 0;
  for (int bit = 0; bit < 32 && first + bit < sim::kPins; ++bit) {
    const int pin = first + bit;
    if (sim::inputLevel(pin)) value |= (1u << bit);
  }
  return value;
}

esp_err_t gpio_install_isr_service(int) {
  if (board().isrServiceInstalled) return ESP_ERR_INVALID_STATE;
  board().isrServiceInstalled = true;
  return ESP_OK;
}
esp_err_t gpio_set_intr_type(gpio_num_t, gpio_int_type_t) { return ESP_OK; }
esp_err_t gpio_isr_handler_add(gpio_num_t pin, gpio_isr_t handler, void* arg) {
  board().isr[pin] = handler;
  board().isrArg[pin] = arg;
  return ESP_OK;
}
esp_err_t gpio_isr_handler_remove(gpio_num_t pin) {
  board().isr[pin] = nullptr;
  return ESP_OK;
}
esp_err_t gpio_set_drive_capability(gpio_num_t, gpio_drive_cap_t) { return ESP_OK; }
esp_err_t gpio_sleep_sel_dis(gpio_num_t) { return ESP_OK; }

// Light sleep, with the figures of ESP-IDF 4.4 on an ESP32: requests under
// about 2.3 ms are refused; otherwise the call returns about the requested
// time later, of which the last 1.2 ms or so the chip is already awake
// again (waiting for its crystal, restoring clocks). Pins keep their
// levels, exactly as on the chip.
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us) {
  board().sleepTimerUs = time_in_us;
  return ESP_OK;
}
esp_err_t esp_light_sleep_start() {
  sim::Board& b = board();
  if (b.sleepTimerUs < 2300) return ESP_ERR_SLEEP_REJECT;
  b.sleptUs += b.sleepTimerUs - 1200;
  ++b.sleeps;
  sim::advanceUs(b.sleepTimerUs + 300);
  return ESP_OK;
}
esp_err_t esp_sleep_pd_config(esp_sleep_pd_domain_t, esp_sleep_pd_option_t) { return ESP_OK; }

// ============================================================== Preferences

bool Preferences::begin(const char*, bool) {
  sim::loadNvs();
  return true;
}
bool Preferences::isKey(const char* key) { return board().nvs.count(key) != 0; }
uint32_t Preferences::getUInt(const char* key, uint32_t defaultValue) {
  const auto it = board().nvs.find(key);
  return it == board().nvs.end() ? defaultValue : it->second;
}
size_t Preferences::putUInt(const char* key, uint32_t value) {
  board().nvs[key] = value;
  ++board().nvsWrites;
  sim::storeNvs();
  sim::advanceUs(3000);  // a flash write holds the main loop up for a while
  if (board().nvsStallNextWriteUs != 0) {  // and now and then a whole page is erased
    sim::advanceUs(board().nvsStallNextWriteUs);
    board().nvsStallNextWriteUs = 0;
  }
  return sizeof(uint32_t);
}

// ====================================================== SPI (pretend SX1276)

SimSPI SPI;
static int g_spiByte = 0;
static uint8_t g_spiAddress = 0;

void SimSPI::begin(int, int, int, int) {}
void SimSPI::end() {}
void SimSPI::beginTransaction(const SPISettings&) { g_spiByte = 0; }
void SimSPI::endTransaction() {}
uint8_t SimSPI::transfer(uint8_t out) {
  if (g_spiByte++ == 0) {
    g_spiAddress = out;
    return 0;
  }
  const uint8_t reg = g_spiAddress & 0x7F;
  if (g_spiAddress & 0x80) {  // write
    if (reg == 0x01) board().loraOpMode = out;
    return 0;
  }
  if (reg == 0x42) return 0x12;  // RegVersion of an SX1276
  if (reg == 0x01) return board().loraOpMode;
  return 0;
}

// ============================================================ U8g2 back end
//
// U8g2 itself is the real library; only its two hardware callbacks are
// replaced. The I2C one decodes just enough SSD1306 traffic to know whether
// the panel is on, its contrast and what picture it shows, and charges the
// simulated clock for the time the bytes would take on the wire.

extern "C" uint8_t u8x8_gpio_and_delay_arduino(u8x8_t*, uint8_t msg, uint8_t arg_int, void*) {
  if (msg == U8X8_MSG_DELAY_MILLI) sim::advanceUs((uint64_t)arg_int * 1000u);
  return 1;
}

extern "C" uint8_t u8x8_byte_arduino_hw_i2c(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
  static std::vector<uint8_t> transfer;
  sim::Board& b = board();

  switch (msg) {
    case U8X8_MSG_BYTE_START_TRANSFER:
      transfer.clear();
      break;

    case U8X8_MSG_BYTE_SEND: {
      const uint8_t* p = (const uint8_t*)arg_ptr;
      transfer.insert(transfer.end(), p, p + arg_int);
      break;
    }

    case U8X8_MSG_BYTE_END_TRANSFER: {
      // Address byte + payload, 9 clock periods each.
      const uint32_t clock = u8x8->bus_clock ? u8x8->bus_clock : 400000u;
      sim::advanceUs((uint64_t)(transfer.size() + 1) * 9u * 1000000u / clock + 120u);
      if (transfer.empty()) break;

      // Where the next data byte goes on the panel (SSD1306 page addressing).
      static int page = 0, column = 0;
      if (transfer[0] == 0x00) {  // command stream
        for (size_t i = 1; i < transfer.size(); ++i) {
          const uint8_t c = transfer[i];
          if (c == 0xAE) b.displayOn = false;
          else if (c == 0xAF) b.displayOn = true;
          else if (c == 0x81 && i + 1 < transfer.size()) b.contrast = transfer[++i];
          else if (c == 0x20 || c == 0x8D || c == 0xA8 || c == 0xD3 || c == 0xD5 ||
                   c == 0xD9 || c == 0xDA || c == 0xDB) ++i;          // one argument
          else if (c == 0x21 || c == 0x22) i += 2;                     // two arguments
          else if (c >= 0xB0 && c <= 0xB7) page = c & 0x07;
          else if (c >= 0x10 && c <= 0x1F) column = (column & 0x0F) | ((c & 0x0F) << 4);
          else if (c <= 0x0F) column = (column & 0xF0) | c;
        }
      } else if (transfer[0] == 0x40) {  // pixel data, one byte = 8 pixels of a column
        if (page == 0 && column == 0) ++b.framesSent;
        if (column == 0) ++b.stripsSent;
        for (size_t i = 1; i < transfer.size(); ++i) {
          if (column < 128) {
            for (int bit = 0; bit < 8; ++bit) {
              b.screen[page * 8 + bit][column] = (transfer[i] >> bit) & 1u;
            }
          }
          ++column;
        }
      }
      break;
    }

    default:
      break;
  }
  return 1;
}
