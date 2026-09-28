#include "Epub.h"

#include <uzlib.h>

#include <strings.h>

#include <cstdlib>
#include <cstring>

#include "BigAlloc.h"
#include "HtmlText.h"

namespace {

constexpr uint32_t MAX_ENTRY_BYTES = 8u * 1024 * 1024;
constexpr size_t MAX_TEXT_BYTES = 5u * 1024 * 1024;

// ZIP fields are little-endian and unaligned: assemble bytes, never cast.
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

struct Entry {
  const char* name;
  uint16_t nameLen;
  uint16_t method;
  uint32_t csize;
  uint32_t usize;
  uint32_t localOffset;
};

class Zip {
 public:
  explicit Zip(const ByteSource& s) : src(s) {}
  ~Zip() {
    bigFree(cd);
    bigFree(entries);
  }

  bool open() {
    // End-of-central-directory record: last 22 bytes plus up to 64 KB comment.
    uint32_t tailLen = src.size < 65557 ? src.size : 65557;
    if (tailLen < 22) return false;
    auto* tail = static_cast<uint8_t*>(bigAlloc(tailLen));
    if (!tail) return false;
    bool ok = src.readAt(src.ctx, src.size - tailLen, tail, tailLen);
    uint32_t cdSize = 0, cdOffset = 0;
    for (int32_t i = static_cast<int32_t>(tailLen) - 22; ok && i >= 0; i--) {
      if (le32(tail + i) == 0x06054b50) {
        count = le16(tail + i + 10);
        cdSize = le32(tail + i + 12);
        cdOffset = le32(tail + i + 16);
        break;
      }
    }
    bigFree(tail);
    if (!ok || count == 0 || cdOffset + cdSize > src.size) return false;

    cd = static_cast<uint8_t*>(bigAlloc(cdSize));
    entries = static_cast<Entry*>(bigAlloc(sizeof(Entry) * count));
    if (!cd || !entries || !src.readAt(src.ctx, cdOffset, cd, cdSize)) return false;

    uint32_t p = 0;
    for (uint16_t i = 0; i < count; i++) {
      if (p + 46 > cdSize || le32(cd + p) != 0x02014b50) return false;
      Entry& e = entries[i];
      e.method = le16(cd + p + 10);
      e.csize = le32(cd + p + 20);
      e.usize = le32(cd + p + 24);
      e.nameLen = le16(cd + p + 28);
      e.localOffset = le32(cd + p + 42);
      e.name = reinterpret_cast<const char*>(cd + p + 46);
      p += 46 + e.nameLen + le16(cd + p + 30) + le16(cd + p + 32);
      if (p > cdSize) return false;
    }
    return true;
  }

  const Entry* find(const char* name, size_t n) const {
    for (uint16_t i = 0; i < count; i++) {
      if (entries[i].nameLen == n && memcmp(entries[i].name, name, n) == 0) return &entries[i];
    }
    for (uint16_t i = 0; i < count; i++) {  // some EPUBs disagree on case
      if (entries[i].nameLen == n && strncasecmp(entries[i].name, name, n) == 0) return &entries[i];
    }
    return nullptr;
  }

  // Returns a NUL-terminated bigAlloc'd copy of the entry's contents.
  char* read(const Entry& e, size_t& len) const {
    if (e.usize > MAX_ENTRY_BYTES || (e.method != 0 && e.method != 8)) return nullptr;
    uint8_t hdr[30];
    if (!src.readAt(src.ctx, e.localOffset, hdr, sizeof(hdr)) || le32(hdr) != 0x04034b50) return nullptr;
    const uint32_t dataOffset = e.localOffset + 30 + le16(hdr + 26) + le16(hdr + 28);
    if (dataOffset + e.csize > src.size) return nullptr;

    auto* out = static_cast<char*>(bigAlloc(e.usize + 1));
    if (!out) return nullptr;
    bool ok;
    if (e.method == 0) {
      ok = e.csize == e.usize && src.readAt(src.ctx, dataOffset, out, e.usize);
    } else {
      auto* comp = static_cast<uint8_t*>(bigAlloc(e.csize ? e.csize : 1));
      ok = comp && src.readAt(src.ctx, dataOffset, comp, e.csize);
      if (ok && e.usize > 0) {
        uzlib_uncomp d{};
        uzlib_uncompress_init(&d, nullptr, 0);  // whole output in memory serves as the window
        d.source = comp;
        d.source_limit = comp + e.csize;
        d.source_read_cb = nullptr;
        d.dest_start = d.dest = reinterpret_cast<uint8_t*>(out);
        d.dest_limit = d.dest + e.usize;
        int res = uzlib_uncompress(&d);
        ok = (res == TINF_DONE || res == TINF_OK) && d.dest == d.dest_limit;
      }
      bigFree(comp);
    }
    if (!ok) {
      bigFree(out);
      return nullptr;
    }
    out[e.usize] = '\0';
    len = e.usize;
    return out;
  }

 private:
  const ByteSource& src;
  uint8_t* cd = nullptr;
  Entry* entries = nullptr;
  uint16_t count = 0;
};

struct Span {
  const char* p = nullptr;
  size_t n = 0;
};

// Value of attribute `name` inside the tag [tag, tagEnd).
Span attr(const char* tag, const char* tagEnd, const char* name) {
  const size_t nl = strlen(name);
  for (const char* q = tag + 1; q + nl + 2 < tagEnd; q++) {
    if ((q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\n' || q[-1] == '\r') && memcmp(q, name, nl) == 0) {
      const char* v = q + nl;
      while (v < tagEnd && *v == ' ') v++;
      if (v >= tagEnd || *v != '=') continue;
      v++;
      while (v < tagEnd && *v == ' ') v++;
      if (v >= tagEnd || (*v != '"' && *v != '\'')) continue;
      const char quote = *v++;
      const char* e = v;
      while (e < tagEnd && *e != quote) e++;
      return {v, static_cast<size_t>(e - v)};
    }
  }
  return {};
}

// Calls fn(localName, nameLen, tagStart, tagEnd) for every start tag in [p, end).
template <typename Fn>
void forEachTag(const char* p, const char* end, Fn fn) {
  while (p < end) {
    const char* lt = static_cast<const char*>(memchr(p, '<', end - p));
    if (!lt || lt + 1 >= end) return;
    const char* gt = static_cast<const char*>(memchr(lt, '>', end - lt));
    if (!gt) return;
    const char* name = lt + 1;
    if (*name != '/' && *name != '!' && *name != '?') {
      const char* ne = name;
      while (ne < gt && *ne != ' ' && *ne != '\t' && *ne != '\n' && *ne != '\r' && *ne != '/') ne++;
      for (const char* c = name; c < ne; c++) {
        if (*c == ':') name = c + 1;
      }
      if (!fn(name, static_cast<size_t>(ne - name), lt, gt)) return;
    }
    p = gt + 1;
  }
}

bool nameIs(const char* name, size_t n, const char* want) { return n == strlen(want) && memcmp(name, want, n) == 0; }

// dir + "/" + href with %XX decoding, "#fragment" removed and "../" resolved.
size_t resolveHref(const Span& dir, const Span& href, char* out, size_t cap) {
  size_t o = 0;
  auto push = [&](char c) {
    if (o + 1 < cap) out[o++] = c;
  };
  for (size_t i = 0; i < dir.n; i++) push(dir.p[i]);
  if (dir.n) push('/');
  for (size_t i = 0; i < href.n && href.p[i] != '#'; i++) {
    char c = href.p[i];
    if (c == '%' && i + 2 < href.n) {
      char hex[3] = {href.p[i + 1], href.p[i + 2], 0};
      c = static_cast<char>(strtol(hex, nullptr, 16));
      i += 2;
    }
    push(c);
  }
  out[o] = '\0';
  for (char* up; (up = strstr(out, "/../")) != nullptr;) {
    char* segStart = up;
    while (segStart > out && segStart[-1] != '/') segStart--;
    memmove(segStart, up + 4, strlen(up + 4) + 1);
  }
  return strlen(out);
}

void copyTitle(const char* opf, size_t opfLen, char* title, size_t cap) {
  title[0] = '\0';
  forEachTag(opf, opf + opfLen, [&](const char* name, size_t n, const char*, const char* gt) {
    if (!nameIs(name, n, "title")) return true;
    const char* s = gt + 1;
    const char* e = static_cast<const char*>(memchr(s, '<', opf + opfLen - s));
    if (!e) return false;
    size_t o = 0;
    bool space = false;
    for (const char* q = s; q < e && o + 1 < cap; q++) {
      if (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t') {
        space = o > 0;
        continue;
      }
      if (space && o + 2 < cap) title[o++] = ' ';
      space = false;
      if (*q == '&' && strncmp(q, "&amp;", 5) == 0) {
        title[o++] = '&';
        q += 4;
      } else {
        title[o++] = *q;
      }
    }
    title[o] = '\0';
    return false;
  });
}

// Loads the OPF (package document) and returns it with its directory.
char* loadOpf(const Zip& zip, size_t& opfLen, char* opfPath, size_t pathCap, Span& dir) {
  const Entry* c = zip.find("META-INF/container.xml", 22);
  size_t cLen = 0;
  char* container = c ? zip.read(*c, cLen) : nullptr;
  if (!container) return nullptr;
  Span full;
  forEachTag(container, container + cLen, [&](const char* name, size_t n, const char* lt, const char* gt) {
    if (!nameIs(name, n, "rootfile")) return true;
    full = attr(lt, gt, "full-path");
    return full.n == 0;
  });
  size_t pl = full.n < pathCap - 1 ? full.n : pathCap - 1;
  memcpy(opfPath, full.p ? full.p : "", pl);
  opfPath[pl] = '\0';
  bigFree(container);
  if (pl == 0) return nullptr;

  const char* slash = strrchr(opfPath, '/');
  dir = {opfPath, slash ? static_cast<size_t>(slash - opfPath) : 0};
  const Entry* o = zip.find(opfPath, pl);
  return o ? zip.read(*o, opfLen) : nullptr;
}

}  // namespace

bool epubTitle(const ByteSource& src, char* title, size_t cap) {
  Zip zip(src);
  if (!zip.open()) return false;
  char opfPath[256];
  Span dir;
  size_t opfLen = 0;
  char* opf = loadOpf(zip, opfLen, opfPath, sizeof(opfPath), dir);
  if (!opf) return false;
  copyTitle(opf, opfLen, title, cap);
  bigFree(opf);
  return title[0] != '\0';
}

bool epubExtract(const ByteSource& src, EpubText& out, const char** err) {
  Zip zip(src);
  if (!zip.open()) return *err = "not a valid EPUB (zip)", false;
  char opfPath[256];
  Span dir;
  size_t opfLen = 0;
  char* opf = loadOpf(zip, opfLen, opfPath, sizeof(opfPath), dir);
  if (!opf) return *err = "EPUB package (OPF) not found", false;
  copyTitle(opf, opfLen, out.title, sizeof(out.title));

  // Spine order -> zip entries. Two passes: size the output, then convert.
  struct Item {
    Span id, href;
  };
  size_t itemCap = 64, itemCount = 0;
  auto* items = static_cast<Item*>(bigAlloc(sizeof(Item) * itemCap));
  const char* opfEnd = opf + opfLen;
  forEachTag(opf, opfEnd, [&](const char* name, size_t n, const char* lt, const char* gt) {
    if (!items || !nameIs(name, n, "item")) return true;
    if (itemCount == itemCap) {
      itemCap *= 2;
      items = static_cast<Item*>(bigRealloc(items, sizeof(Item) * itemCap));
      if (!items) return false;
    }
    items[itemCount++] = {attr(lt, gt, "id"), attr(lt, gt, "href")};
    return true;
  });
  if (!items) {
    bigFree(opf);
    return *err = "out of memory", false;
  }

  auto spineEntry = [&](const char* lt, const char* gt) -> const Entry* {
    Span idref = attr(lt, gt, "idref");
    Span linear = attr(lt, gt, "linear");
    if (!idref.n || (linear.n == 2 && memcmp(linear.p, "no", 2) == 0)) return nullptr;
    for (size_t i = 0; i < itemCount; i++) {
      if (items[i].id.n == idref.n && memcmp(items[i].id.p, idref.p, idref.n) == 0) {
        char path[320];
        size_t pl = resolveHref(dir, items[i].href, path, sizeof(path));
        return zip.find(path, pl);
      }
    }
    return nullptr;
  };

  size_t capacity = 16;
  size_t chapters = 0;
  forEachTag(opf, opfEnd, [&](const char* name, size_t n, const char* lt, const char* gt) {
    if (!nameIs(name, n, "itemref")) return true;
    if (const Entry* e = spineEntry(lt, gt)) {
      capacity += e->usize + 8;
      chapters++;
    }
    return true;
  });
  if (chapters == 0) {
    bigFree(items);
    bigFree(opf);
    return *err = "EPUB has no readable chapters", false;
  }
  if (capacity > MAX_TEXT_BYTES) capacity = MAX_TEXT_BYTES;

  out.text = static_cast<char*>(bigAlloc(capacity + 1));
  if (!out.text) {
    bigFree(items);
    bigFree(opf);
    return *err = "out of memory", false;
  }
  HtmlToText conv(out.text, capacity);
  bool readFailed = false;
  forEachTag(opf, opfEnd, [&](const char* name, size_t n, const char* lt, const char* gt) {
    if (!nameIs(name, n, "itemref")) return true;
    const Entry* e = spineEntry(lt, gt);
    if (!e) return true;
    size_t len = 0;
    char* html = zip.read(*e, len);
    if (!html) {
      readFailed = true;
      return true;  // skip a bad chapter rather than lose the book
    }
    conv.addChapter(html, len);
    bigFree(html);
    return true;
  });
  bigFree(items);
  bigFree(opf);

  out.len = conv.length();
  out.text[out.len] = '\0';
  if (out.len == 0) {
    bigFree(out.text);
    out.text = nullptr;
    return *err = readFailed ? "could not decompress chapters" : "EPUB contains no text", false;
  }
  return true;
}
