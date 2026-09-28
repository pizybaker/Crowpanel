#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <strings.h>

#include "Markup.h"

// One table-of-contents entry: where it starts in the book text.
struct TocEntry {
  uint32_t offset;
  uint8_t depth;  // 0 = top level
  char title[59];
};

static constexpr size_t MAX_TOC_ENTRIES = 400;

namespace toc {

// First visible character at or after offset. Page starts never point past
// it, so a chapter that begins a page maps to that page, not the one before.
inline uint32_t toVisible(const char* t, size_t len, uint32_t offset) {
  while (offset < len && (t[offset] == '\n' || t[offset] == ' ' || markup::isMarker(t[offset]))) offset++;
  return offset;
}

// Copies UTF-8 text into dst (NUL-terminated), collapsing whitespace, dropping
// markers and truncating on a codepoint boundary.
inline void copyTitle(char* dst, size_t cap, const char* src, size_t n) {
  size_t o = 0;
  bool space = false;
  for (size_t i = 0; i < n; i++) {
    const char c = src[i];
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      space = o > 0;
      continue;
    }
    if (markup::isMarker(c)) continue;
    if (space) {
      if (o + 1 >= cap) break;
      dst[o++] = ' ';
      space = false;
    }
    if (o + 1 >= cap) break;
    dst[o++] = c;
  }
  // Truncation may have split the last codepoint: drop it if incomplete.
  size_t k = o;
  while (k > 0 && (static_cast<uint8_t>(dst[k - 1]) & 0xC0) == 0x80) k--;
  if (k > 0) {
    const uint8_t lead = static_cast<uint8_t>(dst[k - 1]);
    const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (o - (k - 1) < need) o = k - 1;
  }
  dst[o] = '\0';
}

// Fallback when a book has no usable TOC: EPUB headings (HEADING markers), or
// for plain text, short standalone paragraphs that look like chapter titles
// ("PROLOGUE", "Chapter 3", "PART TWO"). Returns the number of entries written.
inline size_t fromText(const char* t, size_t len, TocEntry* out, size_t max, bool plainText) {
  size_t n = 0;
  size_t i = 0;
  while (i < len && n < max) {
    // Paragraph = text between blank lines (or chapter/page breaks).
    while (i < len && (t[i] == '\n' || t[i] == markup::PAGE_BREAK)) i++;
    const size_t start = i;
    while (i < len && !(t[i] == '\n' && i + 1 < len && t[i + 1] == '\n') && t[i] != markup::PAGE_BREAK) i++;
    const size_t end = i;
    if (end <= start) continue;

    bool take = false;
    size_t textStart = start;
    while (textStart < end && markup::isMarker(t[textStart]) && t[textStart] != markup::HEADING) textStart++;
    if (!plainText) {
      take = textStart < end && t[textStart] == markup::HEADING;
    } else if (end - start <= 40 && memchr(t + start, '\n', end - start) == nullptr) {
      bool upper = true, letters = false;
      for (size_t k = start; k < end; k++) {
        const char c = t[k];
        if (c >= 'a' && c <= 'z') upper = false;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) letters = true;
      }
      static constexpr const char* WORDS[] = {"chapter ", "prologue", "epilogue", "part ", "book "};
      bool keyword = false;
      for (const char* w : WORDS) keyword |= strncasecmp(t + start, w, strlen(w)) == 0;
      take = letters && (upper || keyword);
    }
    if (take) {
      TocEntry& e = out[n++];
      e.offset = static_cast<uint32_t>(start);
      e.depth = 0;
      copyTitle(e.title, sizeof(e.title), t + start, end - start);
      if (!e.title[0]) n--;
    }
  }
  return n;
}

}  // namespace toc
