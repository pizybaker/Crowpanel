#pragma once

#include <cstddef>

// Converts EPUB XHTML chapters into book text (UTF-8 + markup.h markers),
// appending to a caller-owned buffer. Output for a document is never longer
// than its input plus 8 bytes, so capacity = sum(inputs) + 8 * chapters is safe;
// anything beyond capacity is dropped rather than overflowing.
class HtmlToText {
 public:
  HtmlToText(char* out, size_t capacity) : out(out), cap(capacity) {}

  // Appends one XHTML document; every document after the first starts on a new page.
  void addChapter(const char* html, size_t len);
  size_t length() const { return pos; }

 private:
  void put(char c) {
    if (pos < cap) out[pos++] = c;
  }
  void paragraphBreak();
  void lineBreak();
  void visible(const char* bytes, size_t n);
  void codepoint(unsigned long cp);
  const char* tag(const char* p, const char* end);
  const char* entity(const char* p, const char* end);

  char* out;
  size_t cap;
  size_t pos = 0;
  bool atParaStart = true;
  bool pendingSpace = false;
  bool curBold = false, curItalic = false;
  int boldDepth = 0, italicDepth = 0, skipDepth = 0;
  bool inHeading = false;
};
