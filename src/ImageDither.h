#pragma once

// Greyscale image -> 1bpp screen frame: scale to cover the screen (centered
// crop, box-filtered), then Floyd-Steinberg dither. Pure C++ for host tests.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "BigAlloc.h"

namespace image {

// frame: outW*outH/8 bytes, MSB-first, bit 1 = white. Returns false on OOM.
inline bool coverAndDither(const uint8_t* gray, int w, int h, uint8_t* frame, int outW, int outH) {
  auto* err = static_cast<int16_t*>(bigAlloc(sizeof(int16_t) * outW * outH));
  if (!err) return false;

  // Cover: the image scaled by s fills the screen; crop the overflow equally.
  // Fixed point 16.16 source coordinates.
  const int64_t sx = (static_cast<int64_t>(w) << 16) / outW;
  const int64_t sy = (static_cast<int64_t>(h) << 16) / outH;
  const int64_t step = sx < sy ? sx : sy;  // source pixels per screen pixel
  const int64_t x0 = ((static_cast<int64_t>(w) << 16) - step * outW) / 2;
  const int64_t y0 = ((static_cast<int64_t>(h) << 16) - step * outH) / 2;

  for (int y = 0; y < outH; y++) {
    int ya = static_cast<int>((y0 + step * y) >> 16);
    int yb = static_cast<int>((y0 + step * (y + 1)) >> 16);
    if (yb <= ya) yb = ya + 1;
    if (yb > h) yb = h;
    for (int x = 0; x < outW; x++) {
      int xa = static_cast<int>((x0 + step * x) >> 16);
      int xb = static_cast<int>((x0 + step * (x + 1)) >> 16);
      if (xb <= xa) xb = xa + 1;
      if (xb > w) xb = w;
      uint32_t sum = 0, n = 0;
      for (int yy = ya; yy < yb; yy++) {
        for (int xx = xa; xx < xb; xx++) sum += gray[yy * w + xx];
        n += xb - xa;
      }
      err[y * outW + x] = static_cast<int16_t>(n ? sum / n : 255);
    }
  }

  memset(frame, 0, outW * outH / 8);
  for (int y = 0; y < outH; y++) {
    const bool ltr = (y & 1) == 0;  // serpentine scan avoids directional streaks
    for (int i = 0; i < outW; i++) {
      const int x = ltr ? i : outW - 1 - i;
      const int dir = ltr ? 1 : -1;
      const int v = err[y * outW + x];
      const bool white = v >= 128;
      if (white) frame[y * (outW / 8) + x / 8] |= 0x80 >> (x & 7);
      const int e = v - (white ? 255 : 0);
      auto spread = [&](int dx, int dy, int num) {
        const int nx = x + dx, ny = y + dy;
        if (nx >= 0 && nx < outW && ny < outH) err[ny * outW + nx] += static_cast<int16_t>(e * num / 16);
      };
      spread(dir, 0, 7);
      spread(-dir, 1, 3);
      spread(0, 1, 5);
      spread(dir, 1, 1);
    }
  }
  bigFree(err);
  return true;
}

}  // namespace image
