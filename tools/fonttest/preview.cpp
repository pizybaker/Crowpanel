// Host preview of the font-comparison pages: writes one PBM per font, using
// the firmware's own renderer. Build/run from the repo root:
//   c++ -std=c++17 -Isrc -Iinclude tools/fonttest/preview.cpp src/Font.cpp -o /tmp/fonttest_preview
//   /tmp/fonttest_preview /tmp/fonttest
#include <cstdio>

#include "fonttest/FontTestPage.h"

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  constexpr int W = 400, H = 300;
  static uint8_t buf[W / 8 * H];
  const font::Canvas canvas{buf, W, H};
  for (int i = 0; i < TEST_FONT_COUNT; i++) {
    fonttest::renderPage(canvas, i);
    char path[512];
    snprintf(path, sizeof(path), "%s/page%d.pbm", dir, i + 1);
    FILE* f = fopen(path, "wb");
    if (!f) return 1;
    // PBM: 1 = black, the canvas uses 1 = white.
    fprintf(f, "P4\n%d %d\n", W, H);
    for (uint8_t b : buf) fputc(static_cast<uint8_t>(~b), f);
    fclose(f);
  }
  printf("%d pages -> %s\n", TEST_FONT_COUNT, dir);
  return 0;
}
