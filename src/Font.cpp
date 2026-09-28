#include "Font.h"

#include <algorithm>

namespace font {

namespace {

constexpr uint32_t REPLACEMENT = 0xFFFD;

uint8_t kernClass(const uint16_t* cps, const uint8_t* ids, uint16_t count, uint32_t cp) {
  if (!cps || cp > 0xFFFF) return 0;
  const uint16_t* end = cps + count;
  const uint16_t* it = std::lower_bound(cps, end, static_cast<uint16_t>(cp));
  return (it != end && *it == cp) ? ids[it - cps] : 0;
}

// 4.4 fixed-point kerning adjustment; sparse CSR layout as in EpdFont::getKerning.
int kerning(const EpdFontData* f, uint32_t left, uint32_t right) {
  if (!f->kernRowOffsets) return 0;
  uint8_t lc = kernClass(f->kernLeftCodepoints, f->kernLeftClassIds, f->kernLeftEntryCount, left);
  if (!lc) return 0;
  uint8_t rc = kernClass(f->kernRightCodepoints, f->kernRightClassIds, f->kernRightEntryCount, right);
  if (!rc) return 0;
  const uint8_t target = rc - 1;
  for (uint16_t i = f->kernRowOffsets[lc - 1]; i < f->kernRowOffsets[lc]; i++) {
    if (f->kernSparseCols[i] == target) return f->kernSparseValues[i];
    if (f->kernSparseCols[i] > target) break;
  }
  return 0;
}

void setPixel(const Canvas& c, int x, int y, bool black) {
  if (x < 0 || y < 0 || x >= c.width || y >= c.height) return;
  uint8_t& b = c.buf[y * (c.width / 8) + x / 8];
  const uint8_t mask = 0x80 >> (x & 7);
  if (black) b &= ~mask;
  else b |= mask;
}

// Walks the glyphs of s, calling fn(glyph, penX) with differential rounding:
// each (previous advance + kern) step is snapped to whole pixels as one unit,
// so identical pairs always get identical spacing.
template <typename Fn>
int walk(const EpdFontData* f, const char* s, size_t len, Fn fn) {
  const char* p = s;
  const char* end = s + len;
  int penX = 0;
  int32_t pendingFp = 0;
  uint32_t prev = 0;
  while (p < end) {
    uint32_t cp = nextCodepoint(p, end);
    const EpdGlyph* g = glyph(f, cp);
    if (!g) continue;
    if (prev) penX += fp4::toPixel(pendingFp + kerning(f, prev, cp));
    fn(g, penX);
    pendingFp = g->advanceX;
    prev = cp;
  }
  return prev ? penX + fp4::toPixel(pendingFp) : 0;
}

}  // namespace

uint32_t nextCodepoint(const char*& p, const char* end) {
  const auto* u = reinterpret_cast<const uint8_t*>(p);
  uint8_t c = u[0];
  int n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
  if (n == 0 || p + n > end) {
    p++;
    return REPLACEMENT;
  }
  uint32_t cp = n == 1 ? c : (c & (0x7F >> n));
  for (int i = 1; i < n; i++) {
    if ((u[i] & 0xC0) != 0x80) {
      p++;
      return REPLACEMENT;
    }
    cp = (cp << 6) | (u[i] & 0x3F);
  }
  p += n;
  return cp;
}

const EpdGlyph* glyph(const EpdFontData* f, uint32_t cp) {
  const EpdUnicodeInterval* begin = f->intervals;
  const EpdUnicodeInterval* end = begin + f->intervalCount;
  auto it = std::upper_bound(begin, end, cp, [](uint32_t v, const EpdUnicodeInterval& iv) { return v < iv.first; });
  if (it != begin && cp <= (it - 1)->last) return &f->glyph[(it - 1)->offset + (cp - (it - 1)->first)];
  if (cp != REPLACEMENT) return glyph(f, REPLACEMENT);
  return nullptr;
}

int textWidth(const EpdFontData* f, const char* s, size_t len) {
  return walk(f, s, len, [](const EpdGlyph*, int) {});
}

int drawText(const Canvas& c, const EpdFontData* f, int x, int baselineY, const char* s, size_t len, bool black) {
  return walk(f, s, len, [&](const EpdGlyph* g, int penX) {
    const uint8_t* bits = f->bitmap + g->dataOffset;
    const int x0 = x + penX + g->left;
    const int y0 = baselineY - g->top;
    const int n = g->width * g->height;
    for (int i = 0; i < n; i++) {
      if (bits[i >> 3] & (0x80 >> (i & 7))) setPixel(c, x0 + i % g->width, y0 + i / g->width, black);
    }
  });
}

}  // namespace font
