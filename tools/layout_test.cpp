// Host check of TextLayout.h: across a whole book every visible byte is drawn
// exactly once, in order, and nothing is drawn outside the text column.
// Build/run (from repo root), with an extracted EPUB text or a .txt:
//   c++ -std=c++17 -Ifirmware_reader/src -Ilib/EpdFont firmware_reader/tools/layout_test.cpp \
//       firmware_reader/src/Font.cpp -o /tmp/layout_test && /tmp/layout_test book.txt
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "TextLayout.h"
#include "fonts/serif9_bold.h"
#include "fonts/serif9_italic.h"
#include "fonts/serif9_regular.h"

static std::string visibleOnly(const char* s, size_t n) {
  std::string o;
  for (size_t i = 0; i < n; i++)
    if (s[i] != ' ' && s[i] != '\n' && !markup::isMarker(s[i])) o += s[i];
  return o;
}

static int check(std::string text, const char* label, bool isTxt) {
  if (isTxt) {
    text.resize(text::prepareTxt(text.data(), text.size()));
    text::reflowIfHardWrapped(text.data(), text.size());
  }
  const text::Fonts fonts{&serif9_regular, &serif9_bold, &serif9_italic, &serif9_bold};
  const text::Geometry g{376, 270, 25, 20, 6, 18, 4, 10};

  std::string emitted;
  int pages = 0, overflow = 0, maxLines = 0;
  text::PageStart ps{0, 0};
  while (ps.offset < text.size()) {
    int lines = 0, lastY = -1;
    auto next = text::layoutPage(text.data(), text.size(), ps, fonts, g,
                                 [&](const EpdFontData* f, int x, int y, const char* s, size_t n) {
                                   emitted += visibleOnly(s, n);
                                   int w = font::textWidth(f, s, n);
                                   if (x < 0 || x + w > g.width + 1) overflow++;
                                   if (y != lastY) lines++, lastY = y;
                                   if (y + g.descent > g.height) overflow++;
                                 });
    if (next.offset <= ps.offset) return fprintf(stderr, "%s: no progress at %u\n", label, ps.offset), 1;
    maxLines = lines > maxLines ? lines : maxLines;
    ps = next;
    pages++;
  }
  bool same = emitted == visibleOnly(text.data(), text.size());
  printf("%s: %d pages, up to %d lines/page, %d out-of-bounds draws, content %s\n", label, pages, maxLines, overflow,
         same ? "OK" : "MISMATCH");
  return same && overflow == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
  int fails = 0;
  for (int i = 1; i < argc; i++) {
    std::ifstream in(argv[i], std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string name = argv[i];
    fails += check(ss.str(), argv[i], name.size() > 4 && name.substr(name.size() - 4) == ".txt");
  }
  std::string wrapped;
  for (int p = 0; p < 30; p++) {
    for (int l = 0; l < 5; l++) wrapped += "It was a dark and stormy night; the rain fell in torrents,\n";
    wrapped += "\n";
  }
  fails += check(wrapped, "hard-wrapped", true);
  fails += check("\x05\x01" "Chapter One\x02\n\nA \x03word\x04, then \x01" "bold\x02.\n\n\x0C\x05Two\n\nEnd",
                 "markup", false);
  printf(fails ? "FAILED\n" : "ALL PASSED\n");
  return fails ? 1 : 0;
}
