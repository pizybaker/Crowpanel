// Host-side check of TextLayout.h: every non-whitespace byte appears on
// exactly one page, in order, and no line exceeds the column budget.
//   c++ -std=c++17 -Ifirmware_reader/src firmware_reader/tools/layout_test.cpp -o /tmp/layout_test
//   /tmp/layout_test firmware_reader/src/book.txt
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "TextLayout.h"

static std::string squash(const std::string& s) {
  std::string o;
  for (char c : s)
    if (c != ' ' && c != '\n') o += c;
  return o;
}

static int check(std::string text, int cols, int lines, bool verbose) {
  size_t n = text::sanitizeToAscii(text.data(), text.size());
  text.resize(n);
  text::reflowIfHardWrapped(text.data(), text.size());
  for (char c : text)
    if (c != '\n' && (c < 0x20 || c > 0x7E)) return fprintf(stderr, "non-printable 0x%02x survived\n", (uint8_t)c), 1;

  std::string emitted;
  size_t pos = 0;
  int pages = 0, maxLen = 0;
  while (pos < text.size()) {
    size_t next = text::layoutPage(text.data(), text.size(), pos, cols, lines, [&](int, const char* s, int len) {
      if (len > maxLen) maxLen = len;
      if ((int)strlen(s) != len) fprintf(stderr, "line not NUL-terminated at len\n");
      emitted.append(s, len);
      emitted += '\n';
      if (verbose && pages == 1) printf("|%-*s|\n", cols, s);
    });
    if (next <= pos) {
      bool onlyWs = true;
      for (size_t k = pos; k < text.size(); k++) onlyWs &= (text[k] == ' ' || text[k] == '\n');
      if (onlyWs) break;
      return fprintf(stderr, "no progress at %zu\n", pos), 1;
    }
    pos = next;
    pages++;
  }
  bool same = squash(emitted) == squash(text);
  printf("cols=%d lines=%d -> %d pages, longest line %d, content %s\n", cols, lines, pages, maxLen,
         same ? "OK" : "MISMATCH");
  return (same && maxLen <= cols) ? 0 : 1;
}

int main(int argc, char** argv) {
  int fails = 0;
  if (argc > 1) {
    std::ifstream in(argv[1], std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    fails += check(ss.str(), 47, 14, true);
    fails += check(ss.str(), 30, 9, false);
  }
  std::string wrapped;
  for (int p = 0; p < 30; p++) {
    for (int l = 0; l < 5; l++) wrapped += "It was a dark and stormy night; the rain fell in torrents,\n";
    wrapped += "\n";
  }
  fails += check(wrapped, 47, 14, false);
  fails += check("Supercalifragilisticexpialidocious-antidisestablishmentarianism-pneumonoultramicroscopic ok", 20, 3, false);
  fails += check("caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D \xE2\x80\x94 dash\xE2\x80\xA6 bad\xFF byte", 47, 14, false);
  printf(fails ? "FAILED\n" : "ALL PASSED\n");
  return fails ? 1 : 0;
}
