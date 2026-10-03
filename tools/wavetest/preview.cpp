// Host preview of the waveform-test pages (PBM files). From the repo root:
//   c++ -std=c++17 -Isrc -Iinclude tools/wavetest/preview.cpp src/Font.cpp -o /tmp/wavetest_preview
//   /tmp/wavetest_preview /tmp/wavetest
#include <cstdio>

#include "wavetest/WaveTestPage.h"

static bool writePbm(const char* path, const uint8_t* buf, int w, int h) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P4\n%d %d\n", w, h);
  for (int i = 0; i < w / 8 * h; i++) fputc(static_cast<uint8_t>(~buf[i]), f);  // PBM 1 = black
  fclose(f);
  return true;
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  constexpr int W = 400, H = 300;
  static uint8_t buf[W / 8 * H];
  const font::Canvas c{buf, W, H};
  char path[512];
  for (int which = 0; which < 2; which++) {
    wavetest::renderPage(c, which, "DU 12 frames  -  change 3 of 6");
    snprintf(path, sizeof(path), "%s/page%c.pbm", dir, 'A' + which);
    if (!writePbm(path, buf, W, H)) return 1;
  }
  wavetest::Result results[wavetest::SETTING_COUNT] = {};
  for (int i = 0; i < wavetest::SETTING_COUNT; i++) {
    results[i].count = wavetest::CHANGES_PER_SETTING;
    for (int k = 0; k < results[i].count; k++) results[i].visibleMs[k] = 40 + wavetest::FRAME_SETTINGS[i] * 23 + k;
  }
  wavetest::renderSummary(c, results);
  snprintf(path, sizeof(path), "%s/summary.pbm", dir);
  if (!writePbm(path, buf, W, H)) return 1;
  puts("ok");
  return 0;
}
