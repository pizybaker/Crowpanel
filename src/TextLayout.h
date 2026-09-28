#pragma once

// Text cleanup + word-wrap pagination for the vendor's monospace bitmap font.
// Pure C++ (no Arduino) so tools/layout_test.cpp can exercise it on the host.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace text {

// The vendor font only has glyphs for 0x20..0x7E; anything else would index
// past the end of the font tables. Rewrites `buf` in place to printable ASCII
// plus '\n' (common UTF-8 punctuation mapped to ASCII look-alikes) and returns
// the new length. Output is never longer than input.
inline size_t sanitizeToAscii(char* buf, size_t len) {
  static constexpr char LATIN1[] =  // U+00C0..U+00FF
      "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPs"
      "aaaaaaaceeeeiiiidnooooo/ouuuuypy";
  size_t out = 0;
  size_t i = 0;
  auto u8 = [&](size_t k) { return static_cast<uint8_t>(buf[k]); };
  while (i < len) {
    uint8_t c = u8(i);
    if (c < 0x80) {
      i++;
      if (c == '\n' || (c >= 0x20 && c < 0x7F)) buf[out++] = static_cast<char>(c);
      else if (c == '\t') buf[out++] = ' ';
      continue;
    }
    int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 0;
    bool valid = n > 0 && i + n <= len;
    uint32_t cp = valid ? (c & (0x7F >> n)) : 0;
    for (int k = 1; valid && k < n; k++) {
      if ((u8(i + k) & 0xC0) != 0x80) valid = false;
      cp = (cp << 6) | (u8(i + k) & 0x3F);
    }
    if (!valid) {
      buf[out++] = '?';
      i++;
      continue;
    }
    i += n;
    switch (cp) {
      case 0x00A0: buf[out++] = ' '; break;
      case 0x00AD: case 0xFEFF: case 0x200B: break;
      case 0x2018: case 0x2019: case 0x201A: case 0x2032: buf[out++] = '\''; break;
      case 0x201C: case 0x201D: case 0x201E: case 0x2033: buf[out++] = '"'; break;
      case 0x2010: case 0x2011: case 0x2012: case 0x2013: buf[out++] = '-'; break;
      case 0x2014: case 0x2015: buf[out++] = '-'; buf[out++] = '-'; break;
      case 0x2026: buf[out++] = '.'; buf[out++] = '.'; buf[out++] = '.'; break;
      default:
        buf[out++] = (cp >= 0xC0 && cp <= 0xFF) ? LATIN1[cp - 0xC0] : '?';
        break;
    }
  }
  return out;
}

// Project Gutenberg-style files hard-wrap paragraphs at ~70 columns with blank
// lines between paragraphs. Reflowing those (single '\n' -> ' ') lets text fill
// the screen width instead of leaving ragged half-lines. Files that already put
// one paragraph per line are left alone.
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

static constexpr int MAX_COLS = 64;

// Lays out one page starting at byte `start`. Calls drawLine(lineIdx, text, n)
// for every emitted line (n == 0 for a blank paragraph gap) and returns the
// byte offset where the next page begins. Every non-whitespace byte in
// [start, return) is emitted exactly once; words wider than a line are split.
template <typename DrawLineFn>
size_t layoutPage(const char* t, size_t len, size_t start, int cols, int lines, DrawLineFn drawLine) {
  if (cols > MAX_COLS) cols = MAX_COLS;
  size_t pos = start;
  while (pos < len && (t[pos] == '\n' || t[pos] == ' ')) pos++;

  char lineBuf[MAX_COLS + 1];
  int lineLen = 0;
  int line = 0;
  auto flush = [&]() {
    lineBuf[lineLen] = '\0';
    drawLine(line, lineBuf, lineLen);
    lineLen = 0;
    line++;
  };

  while (pos < len && line < lines) {
    char c = t[pos];
    if (c == '\n') {
      size_t runStart = pos;
      while (pos < len && t[pos] == '\n') pos++;
      bool paragraph = (pos - runStart) > 1;
      if (lineLen > 0 || !paragraph) flush();
      // No gap at the top of a page, and none as the last line (it would waste it).
      if (paragraph && line > 0 && line < lines - 1) flush();
      continue;
    }
    if (c == ' ') {
      pos++;
      continue;
    }
    size_t wordStart = pos;
    while (pos < len && t[pos] != ' ' && t[pos] != '\n') pos++;
    int wordLen = static_cast<int>(pos - wordStart);

    int need = (lineLen > 0 ? 1 : 0) + wordLen;
    if (lineLen + need > cols) {
      if (wordLen > cols) {
        // Split an over-long token across lines instead of dropping it.
        if (lineLen > 0 && cols - lineLen - 1 < 4) {
          flush();
          pos = wordStart;
          continue;
        }
        if (lineLen > 0) lineBuf[lineLen++] = ' ';
        int take = cols - lineLen;
        memcpy(lineBuf + lineLen, t + wordStart, take);
        lineLen += take;
        pos = wordStart + take;
        flush();
        continue;
      }
      flush();
      if (line >= lines) {
        pos = wordStart;
        break;
      }
    }
    if (lineLen > 0) lineBuf[lineLen++] = ' ';
    memcpy(lineBuf + lineLen, t + wordStart, wordLen);
    lineLen += wordLen;
  }
  if (lineLen > 0) flush();
  return pos;
}

}  // namespace text
