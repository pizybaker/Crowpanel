#include "Epub.h"

#include <uzlib.h>

#include <strings.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "BigAlloc.h"
#include "HtmlText.h"
#include "Toc.h"

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

// Like forEachTag but also reports end tags: fn(localName, n, closing, lt, gt).
template <typename Fn>
void forEachTagAll(const char* p, const char* end, Fn fn) {
  while (p < end) {
    const char* lt = static_cast<const char*>(memchr(p, '<', end - p));
    if (!lt || lt + 1 >= end) return;
    const char* gt = static_cast<const char*>(memchr(lt, '>', end - lt));
    if (!gt) return;
    const char* name = lt + 1;
    const bool closing = *name == '/';
    if (closing) name++;
    if (*name != '!' && *name != '?') {
      const char* ne = name;
      while (ne < gt && *ne != ' ' && *ne != '\t' && *ne != '\n' && *ne != '\r' && *ne != '/') ne++;
      for (const char* c = name; c < ne; c++) {
        if (*c == ':') name = c + 1;
      }
      if (!fn(name, static_cast<size_t>(ne - name), closing, lt, gt)) return;
    }
    p = gt + 1;
  }
}

struct RawToc {
  const Entry* doc;  // spine document the entry points into
  char frag[64];     // "#fragment" target, empty for the document start
  uint8_t depth;
  char title[sizeof(TocEntry::title)];
  uint32_t offset;
};

// Label markup (entities, nested spans) -> plain title, via the chapter converter.
void labelToTitle(const char* s, const char* e, char* title, size_t cap) {
  char buf[256];
  const size_t n = static_cast<size_t>(e - s) < sizeof(buf) - 16 ? e - s : sizeof(buf) - 16;
  HtmlToText conv(buf, sizeof(buf) - 1);
  conv.addChapter(s, n);
  toc::copyTitle(title, cap, buf, conv.length());
}

void addRaw(const Zip& zip, const Span& dir, const Span& href, int depth, const char* title, RawToc* out,
            size_t& count) {
  if (count >= MAX_TOC_ENTRIES || !href.n || !title[0]) return;
  char path[320];
  const size_t pl = resolveHref(dir, href, path, sizeof(path));
  RawToc& r = out[count];
  r.doc = zip.find(path, pl);
  if (!r.doc) return;
  r.frag[0] = '\0';
  if (const char* hash = static_cast<const char*>(memchr(href.p, '#', href.n))) {
    const size_t fl = href.p + href.n - hash - 1;
    if (fl < sizeof(r.frag)) {
      memcpy(r.frag, hash + 1, fl);
      r.frag[fl] = '\0';
    }
  }
  r.depth = static_cast<uint8_t>(depth < 0 ? 0 : depth > 6 ? 6 : depth);
  snprintf(r.title, sizeof(r.title), "%s", title);
  r.offset = UINT32_MAX;
  count++;
}

// EPUB 2: <navPoint><navLabel><text>..</text></navLabel><content src=".."/>..</navPoint>
void parseNcx(const Zip& zip, const Span& dir, const char* doc, size_t len, RawToc* out, size_t& count) {
  int depth = -1;
  char label[sizeof(RawToc::title)] = "";
  forEachTagAll(doc, doc + len, [&](const char* name, size_t n, bool closing, const char* lt, const char* gt) {
    if (nameIs(name, n, "navPoint")) {
      depth += closing ? -1 : 1;
      if (!closing) label[0] = '\0';
    } else if (!closing && nameIs(name, n, "text") && depth >= 0) {
      const char* e = static_cast<const char*>(memchr(gt, '<', doc + len - gt));
      if (e) labelToTitle(gt + 1, e, label, sizeof(label));
    } else if (!closing && nameIs(name, n, "content") && depth >= 0) {
      addRaw(zip, dir, attr(lt, gt, "src"), depth, label, out, count);
    }
    return true;
  });
}

// EPUB 3: <nav epub:type="toc"><ol><li><a href="..">Title</a><ol>..</ol></li></ol></nav>
void parseNav(const Zip& zip, const Span& dir, const char* doc, size_t len, RawToc* out, size_t& count) {
  const char* end = doc + len;
  const char* navStart = nullptr;
  const char* firstNav = nullptr;
  forEachTag(doc, end, [&](const char* name, size_t n, const char* lt, const char* gt) {
    if (!nameIs(name, n, "nav")) return true;
    if (!firstNav) firstNav = gt + 1;
    Span type = attr(lt, gt, "epub:type");
    for (size_t i = 0; i + 3 <= type.n; i++) {
      if (memcmp(type.p + i, "toc", 3) == 0) {
        navStart = gt + 1;
        return false;
      }
    }
    return true;
  });
  if (!navStart) navStart = firstNav;
  if (!navStart) return;

  int level = 0;
  const char* aHref = nullptr;
  Span href;
  const char* aText = nullptr;
  forEachTagAll(navStart, end, [&](const char* name, size_t n, bool closing, const char* lt, const char* gt) {
    if (nameIs(name, n, "nav") && closing) return false;
    if (nameIs(name, n, "ol")) level += closing ? -1 : 1;
    if (nameIs(name, n, "a")) {
      if (!closing) {
        href = attr(lt, gt, "href");
        aHref = href.p;
        aText = gt + 1;
      } else if (aHref && aText) {
        char title[sizeof(RawToc::title)];
        labelToTitle(aText, lt, title, sizeof(title));
        addRaw(zip, dir, href, level - 1, title, out, count);
        aHref = aText = nullptr;
      }
    }
    return true;
  });
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
    Span id, href, mediaType, properties;
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
    items[itemCount++] = {attr(lt, gt, "id"), attr(lt, gt, "href"), attr(lt, gt, "media-type"),
                          attr(lt, gt, "properties")};
    return true;
  });
  if (!items) {
    bigFree(opf);
    return *err = "out of memory", false;
  }

  // Table of contents: EPUB 3 nav document, else the EPUB 2 NCX.
  size_t rawCount = 0;
  auto* raw = static_cast<RawToc*>(bigAlloc(sizeof(RawToc) * MAX_TOC_ENTRIES));
  if (raw) {
    const Item* nav = nullptr;
    const Item* ncx = nullptr;
    for (size_t i = 0; i < itemCount; i++) {
      const Span& pr = items[i].properties;
      for (size_t k = 0; k + 3 <= pr.n && !nav; k++) {
        if (memcmp(pr.p + k, "nav", 3) == 0 && (k == 0 || pr.p[k - 1] == ' ') && (k + 3 == pr.n || pr.p[k + 3] == ' '))
          nav = &items[i];
      }
      const Span& mt = items[i].mediaType;
      if (!ncx && mt.n == 24 && memcmp(mt.p, "application/x-dtbncx+xml", 24) == 0) ncx = &items[i];
    }
    for (const Item* doc : {nav, ncx}) {
      if (!doc || rawCount) continue;
      char path[320];
      const size_t pl = resolveHref(dir, doc->href, path, sizeof(path));
      const Entry* e = zip.find(path, pl);
      size_t len = 0;
      char* body = e ? zip.read(*e, len) : nullptr;
      if (!body) continue;
      const char* slash = strrchr(path, '/');
      const Span docDir{path, slash ? static_cast<size_t>(slash - path) : 0};
      if (doc == nav) parseNav(zip, docDir, body, len, raw, rawCount);
      else parseNcx(zip, docDir, body, len, raw, rawCount);
      bigFree(body);
    }
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
    bigFree(raw);
    bigFree(items);
    bigFree(opf);
    return *err = "EPUB has no readable chapters", false;
  }
  if (capacity > MAX_TEXT_BYTES) capacity = MAX_TEXT_BYTES;

  out.text = static_cast<char*>(bigAlloc(capacity + 1));
  if (!out.text) {
    bigFree(raw);
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
    HtmlToText::Anchor anchors[32];
    size_t anchorCount = 0;
    for (size_t i = 0; i < rawCount && anchorCount < 32; i++) {
      if (raw[i].doc == e && raw[i].frag[0]) anchors[anchorCount++] = {raw[i].frag, strlen(raw[i].frag), &raw[i].offset};
    }
    const uint32_t start = conv.addChapter(html, len, anchors, anchorCount);
    for (size_t i = 0; i < rawCount; i++) {
      if (raw[i].doc == e && raw[i].offset == UINT32_MAX) raw[i].offset = start;  // no fragment, or not found
    }
    bigFree(html);
    return true;
  });
  bigFree(items);
  bigFree(opf);

  // Keep entries that landed in the spine, in reading order.
  if (raw && rawCount) {
    out.toc = static_cast<TocEntry*>(bigAlloc(sizeof(TocEntry) * rawCount));
    for (size_t i = 0; out.toc && i < rawCount; i++) {
      if (raw[i].offset == UINT32_MAX) continue;
      TocEntry& t = out.toc[out.tocCount++];
      t.offset = raw[i].offset;
      t.depth = raw[i].depth;
      memcpy(t.title, raw[i].title, sizeof(t.title));
    }
  }
  bigFree(raw);

  out.len = conv.length();
  out.text[out.len] = '\0';
  if (out.len == 0) {
    bigFree(out.text);
    bigFree(out.toc);
    out.text = nullptr;
    out.toc = nullptr;
    out.tocCount = 0;
    return *err = readFailed ? "could not decompress chapters" : "EPUB contains no text", false;
  }
  return true;
}
