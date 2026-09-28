#include "Book.h"

#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>

#include <algorithm>

namespace {
constexpr size_t MAX_BOOK_BYTES = 6 * 1024 * 1024;  // leaves ~2 MB of the 8 MB PSRAM
constexpr size_t READ_CHUNK = 16 * 1024;
}  // namespace

bool Book::open(const BookEntry& entry, int c, int l) {
  close();
  cols = c;
  lines = l;
  uint32_t t0 = millis();

  size_t raw = entry.builtIn ? builtinBookLen() : entry.size;
  if (raw == 0) return err = "empty file", false;
  if (raw > MAX_BOOK_BYTES) return err = "file too large (max 6 MB)", false;

  // Whole book in PSRAM: page turns then never touch the SD card.
  buf = static_cast<char*>(heap_caps_malloc(raw + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    Serial.printf("[book] OOM: %u bytes\n", static_cast<unsigned>(raw + 1));
    return err = "out of memory", false;
  }

  size_t got = 0;
  if (entry.builtIn) {
    memcpy(buf, builtinBookText(), raw);
    got = raw;
  } else {
    File f = SD.open(entry.path, FILE_READ);
    if (!f) {
      close();
      return err = "cannot open file", false;
    }
    while (got < raw) {
      int r = f.read(reinterpret_cast<uint8_t*>(buf) + got, std::min(READ_CHUNK, raw - got));
      if (r <= 0) break;
      got += r;
    }
    if (got != raw) {
      Serial.printf("[book] short read %u/%u\n", static_cast<unsigned>(got), static_cast<unsigned>(raw));
      close();
      return err = "SD read error", false;
    }
  }

  len = text::sanitizeToAscii(buf, got);
  text::reflowIfHardWrapped(buf, len);
  buf[len] = '\0';

  starts.clear();
  starts.reserve(len / (cols * lines / 2) + 8);
  auto noop = [](int, const char*, int) {};
  size_t pos = 0;
  for (;;) {
    size_t next = text::layoutPage(buf, len, pos, cols, lines, noop);
    if (next <= pos && pos != 0) break;
    starts.push_back(static_cast<uint32_t>(pos));
    while (next < len && (buf[next] == ' ' || buf[next] == '\n')) next++;
    if (next >= len) break;
    pos = next;
  }

  Serial.printf("[book] '%s': %u bytes -> %d pages in %lu ms\n", entry.title, static_cast<unsigned>(len), pageCount(),
                millis() - t0);
  return true;
}

void Book::close() {
  if (buf) heap_caps_free(buf);
  buf = nullptr;
  len = 0;
  starts.clear();
  starts.shrink_to_fit();
}

int Book::pageForOffset(uint32_t offset) const {
  auto it = std::upper_bound(starts.begin(), starts.end(), offset);
  return it == starts.begin() ? 0 : static_cast<int>(it - starts.begin()) - 1;
}
