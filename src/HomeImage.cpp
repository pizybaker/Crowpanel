#include "HomeImage.h"

#include <JPEGDEC.h>

#include <new>

#include "BigAlloc.h"
#include "ImageDither.h"

namespace {

struct Target {
  uint8_t* gray;
  int w, h;
};

int onBlock(JPEGDRAW* d) {
  const auto* t = static_cast<const Target*>(d->pUser);
  const auto* px = reinterpret_cast<const uint8_t*>(d->pPixels);
  for (int r = 0; r < d->iHeight; r++) {
    const int y = d->y + r;
    if (y >= t->h) break;
    for (int c = 0; c < d->iWidth; c++) {
      const int x = d->x + c;
      if (x < t->w) t->gray[y * t->w + x] = px[r * d->iWidth + c];
    }
  }
  return 1;
}

}  // namespace

bool decodeJpegToFrame(uint8_t* jpg, size_t len, uint8_t* frame, int outW, int outH, const char** err) {
  // JPEGDEC keeps ~20 KB of state; only needed while decoding.
  auto* jpeg = new (std::nothrow) JPEGDEC;
  if (!jpeg) return *err = "out of memory", false;
  if (!jpeg->openRAM(jpg, static_cast<int>(len), onBlock)) {
    delete jpeg;
    return *err = "not a readable JPEG", false;
  }
  const int w = jpeg->getWidth(), h = jpeg->getHeight();

  // Decode at the smallest 1/2/4/8 reduction that still covers the screen.
  int scale = 8;
  while (scale > 1 && ((w + scale - 1) / scale < outW || (h + scale - 1) / scale < outH)) scale /= 2;
  const int option = scale == 8 ? JPEG_SCALE_EIGHTH : scale == 4 ? JPEG_SCALE_QUARTER : scale == 2 ? JPEG_SCALE_HALF : 0;
  Target t{nullptr, (w + scale - 1) / scale, (h + scale - 1) / scale};
  t.gray = static_cast<uint8_t*>(bigAlloc(static_cast<size_t>(t.w) * t.h));
  if (!t.gray) {
    jpeg->close();
    delete jpeg;
    return *err = "image too large", false;
  }
  memset(t.gray, 0xFF, static_cast<size_t>(t.w) * t.h);

  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
  jpeg->setUserPointer(&t);
  const bool decoded = jpeg->decode(0, 0, option) == 1;
  const bool progressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
  jpeg->close();
  delete jpeg;

  bool ok = decoded && image::coverAndDither(t.gray, t.w, t.h, frame, outW, outH);
  bigFree(t.gray);
  if (!ok) *err = !decoded ? (progressive ? "progressive JPEG not supported" : "JPEG decode failed") : "out of memory";
  return ok;
}
