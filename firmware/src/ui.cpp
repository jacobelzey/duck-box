#include "ui.h"

#include <stdio.h>
#include <string.h>

namespace ui {

namespace {

constexpr int kScreenW = 128;
constexpr int kScreenH = 64;
constexpr int kMargin = 2;                       // room for the burn-in shift
constexpr int kUsableW = kScreenW - 2 * kMargin;
constexpr int kUsableH = kScreenH - 2 * kMargin;
constexpr int kLineGap = 6;
constexpr int kBarHeight = 6;
constexpr int kBarGap = 4;

// Bold Helvetica in every size that is useful on this screen, largest
// first. "_tr" fonts hold the printable ASCII characters only.
const uint8_t* const kTextFonts[] = {
    u8g2_font_helvB24_tr, u8g2_font_helvB18_tr, u8g2_font_helvB14_tr,
    u8g2_font_helvB12_tr, u8g2_font_helvB10_tr, u8g2_font_helvB08_tr,
};
constexpr int kTextFontCount = sizeof(kTextFonts) / sizeof(kTextFonts[0]);

// Tall, narrow digits for the tallies ("_tn" = digits and a few symbols).
const uint8_t* const kNumberFonts[] = {
    u8g2_font_logisoso38_tn, u8g2_font_logisoso32_tn, u8g2_font_logisoso26_tn,
    u8g2_font_logisoso22_tn, u8g2_font_logisoso18_tn,
};
constexpr int kNumberFontCount = sizeof(kNumberFonts) / sizeof(kNumberFonts[0]);

bool isEmpty(const char* s) { return s == nullptr || s[0] == '\0'; }

// Largest font from `fonts` in which `text` fits the given box. Falls back
// to the smallest font if nothing fits. Returns the height of its capital
// letters through *height.
const uint8_t* fitFont(U8G2& d, const uint8_t* const* fonts, int count,
                       const char* text, int maxWidth, int maxHeight, int* height) {
  for (int i = 0; i < count; ++i) {
    d.setFont(fonts[i]);
    const int h = d.getAscent();
    if (h <= maxHeight && (int)d.getStrWidth(text) <= maxWidth) {
      *height = h;
      return fonts[i];
    }
  }
  d.setFont(fonts[count - 1]);
  *height = d.getAscent();
  return fonts[count - 1];
}

}  // namespace

void Screen::drawCentered(const uint8_t* font, const char* text, int baseline) {
  d_.setFont(font);
  const int w = (int)d_.getStrWidth(text);
  int x = (kScreenW - w) / 2;
  if (x < 0) x = 0;
  d_.drawStr(x + dx_, baseline + dy_, text);
}

// Lay out one or two centred lines in the top `areaHeight` pixels of the
// usable area. For two lines, every pairing of font sizes is tried and the
// one with the most ink that still fits is used, so a short word next to a
// long one gets the bigger type.
void Screen::drawLines(const char* line1, const char* line2, int areaHeight) {
  d_.setFontMode(1);
  d_.setDrawColor(1);
  d_.setFontPosBaseline();

  if (isEmpty(line1)) {
    line1 = line2;
    line2 = nullptr;
  }
  if (isEmpty(line1)) return;

  if (isEmpty(line2)) {
    int h = 0;
    const uint8_t* font =
        fitFont(d_, kTextFonts, kTextFontCount, line1, kUsableW, areaHeight, &h);
    drawCentered(font, line1, kMargin + (areaHeight - h) / 2 + h);
    return;
  }

  int bestA = kTextFontCount - 1;
  int bestB = kTextFontCount - 1;
  int bestScore = -1;
  int heights[kTextFontCount];
  bool fitsA[kTextFontCount];
  bool fitsB[kTextFontCount];
  for (int i = 0; i < kTextFontCount; ++i) {
    d_.setFont(kTextFonts[i]);
    heights[i] = d_.getAscent();
    fitsA[i] = (int)d_.getStrWidth(line1) <= kUsableW;
    fitsB[i] = (int)d_.getStrWidth(line2) <= kUsableW;
  }
  for (int a = 0; a < kTextFontCount; ++a) {
    if (!fitsA[a]) continue;
    for (int b = 0; b < kTextFontCount; ++b) {
      if (!fitsB[b]) continue;
      if (heights[a] + kLineGap + heights[b] > areaHeight) continue;
      // Prefer large type overall, and among equals the more even pair.
      const int smaller = heights[a] < heights[b] ? heights[a] : heights[b];
      const int score = (heights[a] + heights[b]) * 4 + smaller;
      if (score > bestScore) {
        bestScore = score;
        bestA = a;
        bestB = b;
      }
    }
  }

  const int total = heights[bestA] + kLineGap + heights[bestB];
  const int top = kMargin + (areaHeight - total) / 2;
  const int baseline1 = top + heights[bestA];
  const int baseline2 = baseline1 + kLineGap + heights[bestB];
  drawCentered(kTextFonts[bestA], line1, baseline1);
  drawCentered(kTextFonts[bestB], line2, baseline2);
}

void Screen::message(const char* line1, const char* line2) {
  d_.clearBuffer();
  drawLines(line1, line2, kUsableH);
}

void Screen::messageWithBar(const char* line1, const char* line2, uint8_t percent) {
  d_.clearBuffer();
  drawLines(line1, line2, kUsableH - kBarHeight - kBarGap);

  if (percent > 100) percent = 100;
  const int x = kMargin + dx_;
  const int y = kScreenH - kMargin - kBarHeight + dy_;
  d_.setDrawColor(1);
  d_.drawFrame(x, y, kUsableW, kBarHeight);
  const int fill = (kUsableW - 4) * percent / 100;
  if (fill > 0) d_.drawBox(x + 2, y + 2, fill, kBarHeight - 4);
}

void Screen::tally(const char* label, uint32_t value) {
  d_.clearBuffer();
  d_.setFontMode(1);
  d_.setDrawColor(1);
  d_.setFontPosBaseline();

  char number[12];
  snprintf(number, sizeof(number), "%lu", (unsigned long)value);

  // Caption: at most 14 pixels tall, so the number keeps most of the screen.
  int labelH = 0;
  const uint8_t* labelFont =
      fitFont(d_, kTextFonts + 2, kTextFontCount - 2, label, kUsableW, 14, &labelH);
  int numberH = 0;
  const uint8_t* numberFont =
      fitFont(d_, kNumberFonts, kNumberFontCount, number, kUsableW,
              kUsableH - labelH - kLineGap, &numberH);

  const int total = labelH + kLineGap + numberH;
  const int top = kMargin + (kUsableH - total) / 2;
  drawCentered(labelFont, label, top + labelH);
  drawCentered(numberFont, number, top + labelH + kLineGap + numberH);
}

void Screen::beamTest(const BeamTestView& view) {
  d_.clearBuffer();
  d_.setFontMode(1);
  d_.setDrawColor(1);
  d_.setFontPosBaseline();
  d_.setFont(u8g2_font_6x12_tr);  // 21 characters per row, 12 pixel rows

  char row[24];

  snprintf(row, sizeof(row), "BEAM TEST  LED%u=IO%u", (unsigned)view.ledNumber,
           (unsigned)view.ledPin);
  d_.drawStr(0, 10, row);
  d_.drawHLine(0, 13, kScreenW);

  // Columns: name, state, IN = level on the pin now, BLK = level that
  // means "blocked" (* = fixed in config.h, ? = unsure), count of breaks.
  d_.drawStr(0, 25, "BEAM  STATE IN BLK  #");

  const BeamRow* rows[2] = {&view.drop, &view.shelf};
  const char* names[2] = {"DROP ", "SHELF"};
  for (int i = 0; i < 2; ++i) {
    const BeamRow& r = *rows[i];
    const int baseline = 37 + i * 12;
    const char mark = r.forced ? '*' : (r.unstable ? '?' : ' ');
    snprintf(row, sizeof(row), "%s %s %c  %c%c%4lu", names[i],
             r.blocked ? "BLOCK" : "clear", r.rawLevel ? 'H' : 'L',
             r.blockedLevel ? 'H' : 'L', mark, (unsigned long)(r.breaks % 10000));
    d_.drawStr(0, baseline, row);
    if (r.blocked) {
      // Invert the state word so a blocked beam is obvious at a glance.
      d_.setDrawColor(2);
      d_.drawBox(6 * 6 - 1, baseline - 10, 5 * 6 + 2, 12);
      d_.setDrawColor(1);
    }
  }

  d_.drawStr(0, 62, (view.drop.unstable || view.shelf.unstable)
                        ? "?=unsure: see README"
                        : "tap=exit hold=relearn");
}

}  // namespace ui
