#pragma once

// One font-comparison page: the font's name in the title bar, then the same
// sample line at every generated size. Pure C++ so tools/fonttest/preview.cpp
// renders the exact pages the firmware shows.

#include <cstdio>
#include <cstring>

#include "../Font.h"
#include "../fonts/sans11_bold.h"
#include "../fonts/sans7_regular.h"
#include "FontCatalog.h"

namespace fonttest {

constexpr int MARGIN_X = 12;
constexpr int TITLE_BAR_H = 30;
constexpr int LABEL_W = 44;  // "14 pt" column
constexpr int FOOTER_BASELINE_FROM_BOTTOM = 6;
constexpr int FOOTER_H = 22;

constexpr const char* SAMPLE = "The quick brown fox";

inline void fillRect(const font::Canvas& c, int x0, int y0, int x1, int y1, bool black) {
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      uint8_t& b = c.buf[y * (c.width / 8) + x / 8];
      const uint8_t mask = 0x80 >> (x & 7);
      b = black ? (b & ~mask) : (b | mask);
    }
  }
}

inline void text(const font::Canvas& c, const EpdFontData* f, int x, int baseline, const char* s, bool black = true) {
  font::drawText(c, f, x, baseline, s, strlen(s), black);
}

inline void renderPage(const font::Canvas& c, int index) {
  const TestFont& tf = TEST_FONTS[index];
  memset(c.buf, 0xFF, static_cast<size_t>(c.width / 8) * c.height);

  fillRect(c, 0, 0, c.width, TITLE_BAR_H, true);
  text(c, &sans11_bold, MARGIN_X, 23, tf.name, false);
  char counter[12];
  snprintf(counter, sizeof(counter), "%d / %d", index + 1, TEST_FONT_COUNT);
  const int cw = font::textWidth(&sans7_regular, counter, strlen(counter));
  text(c, &sans7_regular, c.width - MARGIN_X - cw, 20, counter, false);

  // Spread the rows evenly between the title bar and the footer rule.
  int rowsH = 0;
  for (int i = 0; i < FONT_SIZE_COUNT; i++) rowsH += tf.sizes[i]->advanceY;
  const int gap = (c.height - FOOTER_H - TITLE_BAR_H - rowsH) / (FONT_SIZE_COUNT + 1);
  int top = TITLE_BAR_H + gap;
  for (int i = 0; i < FONT_SIZE_COUNT; i++) {
    const EpdFontData* f = tf.sizes[i];
    const int baseline = top + f->ascender;
    char label[8];
    snprintf(label, sizeof(label), "%d pt", FONT_SIZES_PT[i]);
    text(c, &sans7_regular, MARGIN_X, baseline, label);
    text(c, f, MARGIN_X + LABEL_W, baseline, SAMPLE);
    top += f->advanceY + gap;
  }

  fillRect(c, 0, c.height - FOOTER_H, c.width, c.height - FOOTER_H + 1, true);
  text(c, &sans7_regular, MARGIN_X, c.height - FOOTER_BASELINE_FROM_BOTTOM,
       "Rotate down: next font    Rotate up: previous font");
}

}  // namespace fonttest
