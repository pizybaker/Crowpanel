#pragma once

// Pages for the DU waveform comparison: two different book pages (so every
// line changes on each switch and ghosts show), plus a results summary.
// Pure C++ so tools/wavetest/preview.cpp renders the exact same pages.

#include <cstdio>
#include <cstring>

#include "../Font.h"
#include "../fonts/merri11_bold.h"
#include "../fonts/merri11_italic.h"
#include "../fonts/merri11_regular.h"
#include "../fonts/merri7_bold.h"
#include "../fonts/merri7_regular.h"

namespace wavetest {

constexpr int MARGIN_X = 12;
constexpr int BAR_H = 22;
constexpr int LINE_H = 28;  // the reader's book line height

constexpr uint8_t FRAME_SETTINGS[] = {20, 15, 12, 10, 8};
constexpr int SETTING_COUNT = sizeof(FRAME_SETTINGS);
constexpr int CHANGES_PER_SETTING = 6;

constexpr const char* PAGE_TEXT[2] = {
    "It is a truth universally acknowledged, that a single man in possession of a good fortune, must be in "
    "want of a wife. However little known the feelings or views of such a man may be on his first entering "
    "a neighbourhood, this truth is so well fixed in the minds of the surrounding families.",
    "Call me Ishmael. Some years ago, never mind how long precisely, having little or no money in my purse, "
    "and nothing particular to interest me on shore, I thought I would sail about a little and see the "
    "watery part of the world. It is a way I have of driving off the spleen.",
};
constexpr const char* PAGE_STYLED[2] = {"Bold heading text", "Italic emphasis here"};
constexpr const char* PAGE_SMALL[2] = {"Small print 7 pt: 0123456789 abcdefghij",
                                       "Fine print 7 pt: ABCDEFGHIJ klmnopqrst"};

inline void fillRect(const font::Canvas& c, int x0, int y0, int x1, int y1, bool black) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > c.width) x1 = c.width;
  if (y1 > c.height) y1 = c.height;
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      uint8_t& b = c.buf[y * (c.width / 8) + x / 8];
      const uint8_t mask = 0x80 >> (x & 7);
      b = black ? (b & ~mask) : (b | mask);
    }
  }
}

inline int text(const font::Canvas& c, const EpdFontData* f, int x, int baseline, const char* s, bool black = true) {
  return font::drawText(c, f, x, baseline, s, strlen(s), black);
}

// Greedy word wrap at the reader's column width, stopping before maxBaseline.
inline void paragraph(const font::Canvas& c, const EpdFontData* f, int baseline, int maxBaseline, const char* s) {
  const int maxW = c.width - 2 * MARGIN_X;
  const char* p = s;
  while (*p && baseline <= maxBaseline) {
    const char* lineEnd = p;
    const char* scan = p;
    while (*scan) {
      const char* wordEnd = scan;
      while (*wordEnd && *wordEnd != ' ') wordEnd++;
      if (font::textWidth(f, p, wordEnd - p) > maxW && lineEnd != p) break;
      lineEnd = wordEnd;
      scan = *wordEnd ? wordEnd + 1 : wordEnd;
    }
    font::drawText(c, f, MARGIN_X, baseline, p, lineEnd - p, true);
    baseline += LINE_H;
    p = *lineEnd ? lineEnd + 1 : lineEnd;
  }
}

// One test page. `which` alternates 0/1; the label names the setting and change.
inline void renderPage(const font::Canvas& c, int which, const char* label) {
  memset(c.buf, 0xFF, static_cast<size_t>(c.width / 8) * c.height);
  // Solid black bar: shows how deep the blacks get.
  fillRect(c, 0, 0, c.width, BAR_H, true);
  text(c, &merri7_bold, MARGIN_X, 15, label, false);

  const int hairlineTop = c.height - 36;
  text(c, which ? &merri11_italic : &merri11_bold, MARGIN_X, BAR_H + 26, PAGE_STYLED[which]);
  paragraph(c, &merri11_regular, BAR_H + 26 + LINE_H, hairlineTop - 10, PAGE_TEXT[which]);

  // Hairlines: 1 px rules show whether faint strokes survive.
  for (int i = 0; i < 3; i++) fillRect(c, MARGIN_X, hairlineTop + i * 4, c.width - MARGIN_X, hairlineTop + i * 4 + 1, true);
  text(c, &merri7_regular, MARGIN_X, c.height - 8, PAGE_SMALL[which]);
}

struct Result {
  uint32_t visibleMs[CHANGES_PER_SETTING];  // upload + power-on + waveform
  int count;
};

inline void renderSummary(const font::Canvas& c, const Result* results) {
  memset(c.buf, 0xFF, static_cast<size_t>(c.width / 8) * c.height);
  fillRect(c, 0, 0, c.width, BAR_H + 8, true);
  text(c, &merri11_bold, MARGIN_X, 22, "DU waveform results", false);
  int y = BAR_H + 8 + 30;
  text(c, &merri7_bold, MARGIN_X, y, "Frames");
  text(c, &merri7_bold, 90, y, "Visible refresh, avg (min-max) of 6");
  y += 28;
  for (int i = 0; i < SETTING_COUNT; i++) {
    const Result& r = results[i];
    char a[8], b[48];
    snprintf(a, sizeof(a), "%u", FRAME_SETTINGS[i]);
    if (r.count == 0) {
      snprintf(b, sizeof(b), "not run");
    } else {
      uint32_t sum = 0, lo = r.visibleMs[0], hi = r.visibleMs[0];
      for (int k = 0; k < r.count; k++) {
        sum += r.visibleMs[k];
        if (r.visibleMs[k] < lo) lo = r.visibleMs[k];
        if (r.visibleMs[k] > hi) hi = r.visibleMs[k];
      }
      snprintf(b, sizeof(b), "%lu ms  (%lu-%lu)", static_cast<unsigned long>(sum / r.count),
               static_cast<unsigned long>(lo), static_cast<unsigned long>(hi));
    }
    text(c, &merri11_regular, MARGIN_X, y, a);
    text(c, &merri11_regular, 90, y, b);
    y += LINE_H + 4;
  }
  text(c, &merri7_regular, MARGIN_X, c.height - 8, "Rotate down to run the whole test again");
}

}  // namespace wavetest
