#include "Book.h"

#include <Arduino.h>
#include <SD.h>

#include <algorithm>

#include "BigAlloc.h"
#include "Epub.h"

namespace {

constexpr size_t MAX_TXT_BYTES = 6 * 1024 * 1024;  // leaves ~2 MB of the 8 MB PSRAM
constexpr size_t READ_CHUNK = 16 * 1024;
constexpr const char* CACHE_DIR = "/.reader";
constexpr uint32_t CACHE_VERSION = 1;  // bump when HtmlToText output changes

bool readAll(File& f, char* dst, size_t n) {
  size_t got = 0;
  while (got < n) {
    int r = f.read(reinterpret_cast<uint8_t*>(dst) + got, std::min(READ_CHUNK, n - got));
    if (r <= 0) return false;
    got += r;
  }
  return true;
}

bool sdReadAt(void* ctx, uint32_t offset, void* dst, size_t n) {
  File& f = *static_cast<File*>(ctx);
  return f.seek(offset) && readAll(f, static_cast<char*>(dst), n);
}

void cachePath(const BookEntry& e, char* out, size_t cap) {
  uint32_t h = 2166136261u;
  for (const char* p = e.path; *p; p++) h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
  h ^= e.size;
  snprintf(out, cap, "%s/%08lx_v%lu.txt", CACHE_DIR, static_cast<unsigned long>(h),
           static_cast<unsigned long>(CACHE_VERSION));
}

}  // namespace

bool bookNeedsExtraction(const BookEntry& entry) {
  if (!entry.epub) return false;
  char path[64];
  cachePath(entry, path, sizeof(path));
  return !SD.exists(path);
}

bool Book::loadTxt(const BookEntry& e) {
  size_t raw = e.builtIn ? builtinBookLen() : e.size;
  if (raw == 0) return err = "empty file", false;
  if (raw > MAX_TXT_BYTES) return err = "file too large (max 6 MB)", false;
  buf = static_cast<char*>(bigAlloc(raw + 1));
  if (!buf) return err = "out of memory", false;
  if (e.builtIn) {
    memcpy(buf, builtinBookText(), raw);
  } else {
    File f = SD.open(e.path, FILE_READ);
    if (!f || !readAll(f, buf, raw)) return err = "SD read error", false;
  }
  len = text::prepareTxt(buf, raw);
  text::reflowIfHardWrapped(buf, len);
  return true;
}

bool Book::loadEpub(const BookEntry& e) {
  char cache[64];
  cachePath(e, cache, sizeof(cache));
  if (File c = SD.open(cache, FILE_READ)) {
    len = c.size();
    buf = static_cast<char*>(bigAlloc(len + 1));
    if (buf && readAll(c, buf, len)) {
      Serial.printf("[book] cache hit %s\n", cache);
      return true;
    }
    bigFree(buf);
    buf = nullptr;
    Serial.printf("[book] cache unreadable, re-extracting\n");
  }

  File f = SD.open(e.path, FILE_READ);
  if (!f) return err = "cannot open file", false;
  ByteSource src{&f, static_cast<uint32_t>(f.size()), sdReadAt};
  EpubText out;
  if (!epubExtract(src, out, &err)) return false;
  buf = out.text;
  len = out.len;

  SD.mkdir(CACHE_DIR);
  if (File c = SD.open(cache, FILE_WRITE)) {
    const bool ok = c.write(reinterpret_cast<const uint8_t*>(buf), len) == len;
    c.close();  // must close before a possible remove
    if (!ok) SD.remove(cache);
    Serial.printf("[book] cache %s %s\n", ok ? "written" : "write failed", cache);
  }
  return true;
}

void Book::paginate() {
  starts.clear();
  starts.reserve(len / 1200 + 16);  // ~1.4 KB of text per page at this layout
  auto noop = [](const EpdFontData*, int, int, const char*, size_t) {};
  text::PageStart ps{0, 0};
  while (ps.offset < len) {
    text::PageStart next = text::layoutPage(buf, len, ps, *fonts, geom, noop);
    if (next.offset <= ps.offset) break;
    starts.push_back(ps);
    ps = next;
  }
  if (starts.empty()) starts.push_back({0, 0});
}

bool Book::open(const BookEntry& e, const text::Fonts& f, const text::Geometry& g) {
  close();
  fonts = &f;
  geom = g;
  const uint32_t t0 = millis();
  if (!(e.epub ? loadEpub(e) : loadTxt(e))) {
    close();
    return false;
  }
  buf[len] = '\0';
  const uint32_t t1 = millis();
  paginate();
  Serial.printf("[book] '%s': %u bytes, load %lu ms, %d pages in %lu ms\n", e.title, static_cast<unsigned>(len),
                t1 - t0, pageCount(), millis() - t1);
  return true;
}

void Book::close() {
  bigFree(buf);
  buf = nullptr;
  len = 0;
  starts.clear();
  starts.shrink_to_fit();
}

int Book::pageForOffset(uint32_t offset) const {
  auto it = std::upper_bound(starts.begin(), starts.end(), offset,
                             [](uint32_t off, const text::PageStart& s) { return off < s.offset; });
  return it == starts.begin() ? 0 : static_cast<int>(it - starts.begin()) - 1;
}
