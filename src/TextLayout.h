#pragma once

// Page layout for book text (UTF-8 + Markup.h markers) with proportional
// fonts: greedy word wrap, justified lines, first-line indents, centered bold
// headings. Pure C++ so tools/layout_test.cpp can run it on the host.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Font.h"
#include "Markup.h"

namespace text {

// TXT input: drops '\r' and control bytes (which would collide with markers),
// turns tabs into spaces. Returns the new length.
inline size_t prepareTxt(char* buf, size_t len) {
  size_t out = 0;
  for (size_t i = 0; i < len; i++) {
    const char c = buf[i];
    if (c == '\t') buf[out++] = ' ';
    else if (c == '\n' || static_cast<unsigned char>(c) >= 0x20) buf[out++] = c;
  }
  return out;
}

// Project Gutenberg-style files hard-wrap paragraphs at ~70 columns with blank
// lines between paragraphs; reflow those (single '\n' -> ' ') so text fills
// the line. Files that already put one paragraph per line are left alone.
inline void reflowIfHardWrapped(char* buf, size_t len) {
  size_t nonEmpty = 0, wrappedLen = 0, blank = 0, lineStart = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i == len || buf[i] == '\n') {
      size_t l = i - lineStart;
      if (l == 0) blank++;
      else {
        nonEmpty++;
        if (l >= 40 && l <= 80) wrappedLen++;
      }
      lineStart = i + 1;
    }
  }
  if (blank == 0 || nonEmpty < 10 || wrappedLen * 10 < nonEmpty * 7) return;
  for (size_t i = 0; i < len; i++) {
    if (buf[i] != '\n') continue;
    bool prevNl = i > 0 && buf[i - 1] == '\n';
    bool nextNl = i + 1 < len && buf[i + 1] == '\n';
    if (!prevNl && !nextNl) buf[i] = ' ';
  }
}

struct Fonts {
  const EpdFontData* regular;
  const EpdFontData* bold;
  const EpdFontData* italic;
  const EpdFontData* boldItalic;
  const EpdFontData* pick(uint8_t style) const;
};

struct Geometry {
  int width;       // text column width in pixels
  int height;      // text area height in pixels
  int lineHeight;  // baseline-to-baseline
  int ascent;      // top of line box to baseline
  int descent;     // baseline to lowest descender (positive)
  int indent;      // first-line indent
  int paraGap;     // extra space after a paragraph
  int headingGap;  // extra space before/after a heading
};

// Layout state carried from one page to the next.
enum : uint8_t { S_BOLD = 1, S_ITALIC = 2, S_MID_PARA = 4, S_NO_INDENT = 8, S_HEADING = 16 };

struct PageStart {
  uint32_t offset;
  uint8_t state;
};

// Lays out one page. draw(font, x, baselineY, text, len) is called per run of
// same-style text (pass a no-op to only measure). Returns where the next page
// starts; offset == len means the book ends on this page.
template <typename DrawFn>
PageStart layoutPage(const char* t, size_t len, PageStart start, const Fonts& fonts, const Geometry& g, DrawFn draw);

// Implementation --------------------------------------------------------------

inline const EpdFontData* Fonts::pick(uint8_t style) const {
  const bool b = style & S_BOLD, i = style & S_ITALIC;
  return b && i ? boldItalic : b ? bold : i ? italic : regular;
}

namespace detail {

constexpr int MAX_RUNS = 96;
constexpr int MAX_WORDS = 64;

struct Run {
  uint32_t off;
  uint16_t len;
  uint8_t style;
  int16_t x;  // relative to its word
};

struct Word {
  uint8_t firstRun, runCount;
  int16_t width;
};

inline bool isVisibleStart(char c) { return c != ' ' && c != '\n' && c != markup::PAGE_BREAK && !markup::isMarker(c); }

inline void applyMarker(char c, uint8_t& state) {
  if (c == markup::BOLD_ON) state |= S_BOLD;
  else if (c == markup::BOLD_OFF) state &= ~S_BOLD;
  else if (c == markup::ITALIC_ON) state |= S_ITALIC;
  else if (c == markup::ITALIC_OFF) state &= ~S_ITALIC;
}

}  // namespace detail

template <typename DrawFn>
PageStart layoutPage(const char* t, size_t len, PageStart start, const Fonts& fonts, const Geometry& g, DrawFn draw) {
  using namespace detail;
  size_t pos = start.offset;
  uint8_t state = start.state;
  int y = 0;
  bool pageHasText = false;

  Run runs[MAX_RUNS];
  Word words[MAX_WORDS];

  while (pos < len) {
    // Paragraph start: skip separators, pick up the heading flag.
    if (!(state & S_MID_PARA)) {
      while (pos < len && (t[pos] == '\n' || t[pos] == ' ' || markup::isMarker(t[pos]) || t[pos] == markup::PAGE_BREAK)) {
        if (t[pos] == markup::PAGE_BREAK) {
          state = (state & (S_BOLD | S_ITALIC)) | S_NO_INDENT;
          if (pageHasText) return {static_cast<uint32_t>(pos + 1), state};
        } else if (t[pos] == markup::HEADING) {
          state |= S_HEADING;
        } else {
          applyMarker(t[pos], state);
        }
        pos++;
      }
      if (pos >= len) break;
      if ((state & S_HEADING) && pageHasText) y += g.headingGap;
    }

    // Build one line.
    const size_t lineStart = pos;
    const uint8_t lineState = state;
    const bool heading = state & S_HEADING;
    const bool firstLine = !(state & S_MID_PARA);
    const int indent = (firstLine && !heading && !(state & S_NO_INDENT)) ? g.indent : 0;
    const int avail = g.width - indent;
    int nRuns = 0, nWords = 0, lineW = 0;
    bool endsParagraph = false, hardBreak = false;
    const int spaceW = font::textWidth(fonts.regular, " ", 1);

    while (pos < len) {
      const char c = t[pos];
      if (c == ' ') {
        pos++;
        continue;
      }
      if (c == '\n' || c == markup::PAGE_BREAK) {
        if (c == '\n' && !(pos + 1 < len && t[pos + 1] == '\n')) {
          hardBreak = true;
          pos++;
        } else {
          endsParagraph = true;
          if (c == '\n') pos += 2;  // PAGE_BREAK is handled at the next paragraph start
        }
        break;
      }
      if (markup::isMarker(c)) {
        applyMarker(c, state);
        pos++;
        continue;
      }

      // One word, possibly spanning several style runs.
      const size_t wordStart = pos;
      const uint8_t wordState = state;
      const int runBase = nRuns;
      int wordW = 0;
      while (pos < len && t[pos] != ' ' && t[pos] != '\n' && t[pos] != markup::PAGE_BREAK) {
        if (markup::isMarker(t[pos])) {
          applyMarker(t[pos], state);
          pos++;
          continue;
        }
        const size_t runStart = pos;
        while (pos < len && isVisibleStart(t[pos])) pos++;
        if (nRuns < MAX_RUNS) {
          const uint8_t st = state | (heading ? S_BOLD : 0);
          const int w = font::textWidth(fonts.pick(st), t + runStart, pos - runStart);
          runs[nRuns++] = {static_cast<uint32_t>(runStart), static_cast<uint16_t>(pos - runStart), st,
                           static_cast<int16_t>(wordW)};
          wordW += w;
        }
      }
      const int needed = (nWords ? spaceW : 0) + wordW;
      if (nWords > 0 && (lineW + needed > avail || nWords == MAX_WORDS || nRuns == MAX_RUNS)) {
        pos = wordStart;  // doesn't fit: next line starts with this word
        state = wordState;
        nRuns = runBase;
        break;
      }
      words[nWords++] = {static_cast<uint8_t>(runBase), static_cast<uint8_t>(nRuns - runBase),
                         static_cast<int16_t>(wordW)};
      lineW += needed;
    }
    if (pos >= len) endsParagraph = true;

    if (nWords == 0) {
      state = endsParagraph ? (state & (S_BOLD | S_ITALIC)) : (state | S_MID_PARA);
      continue;  // blank line or empty paragraph: nothing to draw
    }
    if (y + g.ascent + g.descent > g.height) return {static_cast<uint32_t>(lineStart), lineState};

    // Justify full lines; the last line of a paragraph, hard breaks and headings stay natural.
    const bool justify = !endsParagraph && !hardBreak && !heading && nWords > 1;
    const int gaps = nWords - 1;
    const int slack = avail - lineW;
    int x = heading ? (g.width - lineW) / 2 : indent;
    for (int w = 0; w < nWords; w++) {
      for (int r = 0; r < words[w].runCount; r++) {
        const Run& run = runs[words[w].firstRun + r];
        draw(fonts.pick(run.style), x + run.x, y + g.ascent, t + run.off, run.len);
      }
      x += words[w].width + spaceW;
      if (justify) x += slack / gaps + (w < slack % gaps ? 1 : 0);
    }
    y += g.lineHeight;
    pageHasText = true;

    if (endsParagraph) {
      y += heading ? g.headingGap : g.paraGap;
      state &= S_BOLD | S_ITALIC;
      if (heading) state |= S_NO_INDENT;
    } else {
      state = (state & ~S_NO_INDENT) | S_MID_PARA;
      if (heading) state |= S_HEADING;
    }
  }
  return {static_cast<uint32_t>(len), state};
}

}  // namespace text
