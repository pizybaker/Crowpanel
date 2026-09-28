#include "HtmlText.h"

#include <cstdlib>
#include <cstring>

#include "Markup.h"

namespace {

enum class Kind { Other, Skip, Block, Heading, Break, Bold, Italic, Cell };

struct TagKind {
  const char* name;
  Kind kind;
};

constexpr TagKind TAGS[] = {
    {"head", Kind::Skip},      {"script", Kind::Skip},    {"style", Kind::Skip},      {"svg", Kind::Skip},
    {"p", Kind::Block},        {"div", Kind::Block},      {"li", Kind::Block},        {"ul", Kind::Block},
    {"ol", Kind::Block},       {"blockquote", Kind::Block}, {"section", Kind::Block}, {"article", Kind::Block},
    {"header", Kind::Block},   {"footer", Kind::Block},   {"aside", Kind::Block},     {"nav", Kind::Block},
    {"table", Kind::Block},    {"tr", Kind::Block},       {"dt", Kind::Block},        {"dd", Kind::Block},
    {"dl", Kind::Block},       {"pre", Kind::Block},      {"figure", Kind::Block},    {"figcaption", Kind::Block},
    {"hr", Kind::Block},       {"body", Kind::Block},     {"center", Kind::Block},    {"h1", Kind::Heading},
    {"h2", Kind::Heading},     {"h3", Kind::Heading},     {"h4", Kind::Heading},      {"h5", Kind::Heading},
    {"h6", Kind::Heading},     {"br", Kind::Break},       {"b", Kind::Bold},          {"strong", Kind::Bold},
    {"i", Kind::Italic},       {"em", Kind::Italic},      {"cite", Kind::Italic},     {"dfn", Kind::Italic},
    {"var", Kind::Italic},     {"td", Kind::Cell},        {"th", Kind::Cell},
};

struct Entity {
  const char* name;
  unsigned short cp;
};

constexpr Entity ENTITIES[] = {
    {"amp", '&'},       {"lt", '<'},        {"gt", '>'},        {"quot", '"'},      {"apos", '\''},
    {"nbsp", 0xA0},     {"shy", 0xAD},      {"mdash", 0x2014},  {"ndash", 0x2013},  {"hellip", 0x2026},
    {"lsquo", 0x2018},  {"rsquo", 0x2019},  {"ldquo", 0x201C},  {"rdquo", 0x201D},  {"sbquo", 0x201A},
    {"bdquo", 0x201E},  {"laquo", 0xAB},    {"raquo", 0xBB},    {"copy", 0xA9},     {"reg", 0xAE},
    {"trade", 0x2122},  {"deg", 0xB0},      {"middot", 0xB7},   {"bull", 0x2022},   {"times", 0xD7},
    {"eacute", 0xE9},   {"egrave", 0xE8},   {"agrave", 0xE0},   {"ccedil", 0xE7},   {"uuml", 0xFC},
    {"ouml", 0xF6},     {"auml", 0xE4},     {"szlig", 0xDF},    {"thinsp", 0x2009}, {"ensp", 0x2002},
    {"emsp", 0x2003},   {"zwj", 0x200D},    {"zwnj", 0x200C},   {"prime", 0x2032},  {"Prime", 0x2033},
};

bool isSpace(char c) { return static_cast<unsigned char>(c) <= ' '; }

// Value of attribute `name` within [p, end) (the attribute part of a tag).
bool attrValue(const char* p, const char* end, const char* name, const char*& v, size_t& n) {
  const size_t nl = strlen(name);
  for (const char* q = p; q + nl < end; q++) {
    if (!isSpace(q[0]) || memcmp(q + 1, name, nl) != 0) continue;
    const char* e = q + 1 + nl;
    while (e < end && isSpace(*e)) e++;
    if (e >= end || *e != '=') continue;
    e++;
    while (e < end && isSpace(*e)) e++;
    if (e >= end || (*e != '"' && *e != '\'')) continue;
    const char quote = *e++;
    const char* close = static_cast<const char*>(memchr(e, quote, end - e));
    if (!close) return false;
    v = e;
    n = close - e;
    return true;
  }
  return false;
}

Kind lookupTag(const char* name, size_t n) {
  for (const auto& t : TAGS) {
    if (strlen(t.name) == n && strncmp(t.name, name, n) == 0) return t.kind;
  }
  return Kind::Other;
}

}  // namespace

void HtmlToText::paragraphBreak() {
  if (!atParaStart) {
    put('\n');
    put('\n');
  }
  atParaStart = true;
  pendingSpace = false;
}

void HtmlToText::lineBreak() {
  if (!atParaStart) put('\n');
  pendingSpace = false;
}

void HtmlToText::visible(const char* bytes, size_t n) {
  if (skipDepth > 0) return;
  if (pendingSpace && !atParaStart) put(' ');
  pendingSpace = false;
  const bool wantBold = boldDepth > 0 || inHeading;
  const bool wantItalic = italicDepth > 0;
  if (wantBold != curBold) put(wantBold ? markup::BOLD_ON : markup::BOLD_OFF);
  if (wantItalic != curItalic) put(wantItalic ? markup::ITALIC_ON : markup::ITALIC_OFF);
  curBold = wantBold;
  curItalic = wantItalic;
  for (size_t i = 0; i < n; i++) put(bytes[i]);
  atParaStart = false;
}

void HtmlToText::codepoint(unsigned long cp) {
  if (cp == 0xAD || cp == 0x200B || cp == 0xFEFF || cp == 0x200C || cp == 0x200D) return;
  if (cp == 0xA0 || cp < 0x20) {
    if (!atParaStart) pendingSpace = true;
    return;
  }
  char b[4];
  size_t n;
  if (cp < 0x80) {
    b[0] = static_cast<char>(cp);
    n = 1;
  } else if (cp < 0x800) {
    b[0] = static_cast<char>(0xC0 | (cp >> 6));
    b[1] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 2;
  } else if (cp < 0x10000) {
    b[0] = static_cast<char>(0xE0 | (cp >> 12));
    b[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    b[2] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 3;
  } else {
    b[0] = static_cast<char>(0xF0 | (cp >> 18));
    b[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    b[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    b[3] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 4;
  }
  visible(b, n);
}

// p points at '&'. Returns the position after the entity (or after '&' if unknown).
const char* HtmlToText::entity(const char* p, const char* end) {
  const char* semi = p + 1;
  while (semi < end && semi - p <= 10 && *semi != ';' && !isSpace(*semi) && *semi != '&' && *semi != '<') semi++;
  if (semi >= end || *semi != ';') {
    visible("&", 1);
    return p + 1;
  }
  const char* name = p + 1;
  size_t n = semi - name;
  if (n > 1 && name[0] == '#') {
    char num[12] = {};
    memcpy(num, name + 1, n - 1 < sizeof(num) - 1 ? n - 1 : sizeof(num) - 1);
    unsigned long cp = (num[0] == 'x' || num[0] == 'X') ? strtoul(num + 1, nullptr, 16) : strtoul(num, nullptr, 10);
    if (cp > 0 && cp <= 0x10FFFF) codepoint(cp);
    return semi + 1;
  }
  for (const auto& e : ENTITIES) {
    if (strlen(e.name) == n && strncmp(e.name, name, n) == 0) {
      codepoint(e.cp);
      return semi + 1;
    }
  }
  visible(p, semi + 1 - p);
  return semi + 1;
}

// p points at '<'. Returns the position after the tag (or construct).
const char* HtmlToText::tag(const char* p, const char* end) {
  auto skipPast = [&](const char* from, const char* marker) {
    const size_t m = strlen(marker);
    for (const char* q = from; q + m <= end; q++) {
      if (memcmp(q, marker, m) == 0) return q + m;
    }
    return end;
  };
  if (end - p >= 4 && memcmp(p, "<!--", 4) == 0) return skipPast(p + 4, "-->");
  if (end - p >= 9 && memcmp(p, "<![CDATA[", 9) == 0) return skipPast(p + 9, "]]>");
  if (end - p >= 2 && (p[1] == '!' || p[1] == '?')) return skipPast(p, ">");

  const char* q = p + 1;
  const bool closing = q < end && *q == '/';
  if (closing) q++;
  const char* nameStart = q;
  while (q < end && !isSpace(*q) && *q != '>' && *q != '/') q++;
  const char* nameEnd = q;
  for (const char* c = nameStart; c < nameEnd; c++) {
    if (*c == ':') nameStart = c + 1;  // drop namespace prefixes like xhtml:p
  }
  char lower[12];
  size_t n = nameEnd - nameStart;
  if (n >= sizeof(lower)) n = 0;
  for (size_t i = 0; i < n; i++) {
    char c = nameStart[i];
    lower[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
  }

  char quote = 0;
  bool selfClosing = false;
  while (q < end) {
    char c = *q++;
    if (quote) {
      if (c == quote) quote = 0;
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '>') {
      selfClosing = q - 2 >= p && q[-2] == '/';
      break;
    }
  }

  // Anchor targets (id="..." or <a name="...">) resolve to the position after
  // this tag's own effect, i.e. where its content starts.
  const Anchor* hit = nullptr;
  if (anchorCount && !closing) {
    const char* v;
    size_t vn;
    if (attrValue(nameEnd, q, "id", v, vn) || attrValue(nameEnd, q, "name", v, vn)) {
      for (size_t i = 0; i < anchorCount && !hit; i++) {
        if (anchors[i].idLen == vn && memcmp(anchors[i].id, v, vn) == 0) hit = &anchors[i];
      }
    }
  }
  struct RecordOnExit {
    const Anchor* a;
    const size_t& pos;
    ~RecordOnExit() {
      if (a && *a->offset == UINT32_MAX) *a->offset = static_cast<uint32_t>(pos);
    }
  } record{hit, pos};

  const Kind kind = n ? lookupTag(lower, n) : Kind::Other;
  if (kind == Kind::Skip) {
    if (closing) {
      if (skipDepth > 0) skipDepth--;
    } else if (!selfClosing) {
      skipDepth++;
    }
    return q;
  }
  if (skipDepth > 0) return q;

  switch (kind) {
    case Kind::Block:
      paragraphBreak();
      break;
    case Kind::Heading:
      paragraphBreak();
      if (!closing) {
        put(markup::HEADING);
        inHeading = true;
      } else {
        inHeading = false;
      }
      break;
    case Kind::Break:
      lineBreak();
      break;
    case Kind::Bold:
      if (!selfClosing) boldDepth += closing ? (boldDepth > 0 ? -1 : 0) : 1;
      break;
    case Kind::Italic:
      if (!selfClosing) italicDepth += closing ? (italicDepth > 0 ? -1 : 0) : 1;
      break;
    case Kind::Cell:
      if (!atParaStart) pendingSpace = true;
      break;
    default:
      break;
  }
  return q;
}

uint32_t HtmlToText::addChapter(const char* html, size_t len, const Anchor* chapterAnchors, size_t count) {
  if (pos > 0) {
    paragraphBreak();
    put(markup::PAGE_BREAK);
  }
  const auto start = static_cast<uint32_t>(pos);
  anchors = chapterAnchors;
  anchorCount = count;
  atParaStart = true;
  pendingSpace = false;
  boldDepth = italicDepth = skipDepth = 0;
  inHeading = false;

  const char* p = html;
  const char* end = html + len;
  while (p < end) {
    const char c = *p;
    if (c == '<') {
      p = tag(p, end);
    } else if (c == '&') {
      p = entity(p, end);
    } else if (isSpace(c)) {
      if (!atParaStart && skipDepth == 0) pendingSpace = true;
      p++;
    } else {
      const auto u = static_cast<unsigned char>(c);
      size_t n = u < 0x80 ? 1 : (u >> 5) == 0x6 ? 2 : (u >> 4) == 0xE ? 3 : (u >> 3) == 0x1E ? 4 : 1;
      if (p + n > end) n = end - p;
      if (n == 2 && u == 0xC2 && static_cast<unsigned char>(p[1]) == 0xAD) {
        p += 2;  // soft hyphen
        continue;
      }
      if (n == 2 && u == 0xC2 && static_cast<unsigned char>(p[1]) == 0xA0) {
        if (!atParaStart && skipDepth == 0) pendingSpace = true;  // no-break space
        p += 2;
        continue;
      }
      visible(p, n);
      p += n;
    }
  }
  paragraphBreak();
  anchors = nullptr;
  anchorCount = 0;
  return start;
}
