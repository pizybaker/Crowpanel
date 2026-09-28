#include "Library.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <strings.h>

#include <Preferences.h>

#include "Epub.h"

extern const char BOOK_TXT_START[] asm("_binary_src_book_txt_start");
extern const char BOOK_TXT_END[] asm("_binary_src_book_txt_end");

namespace {

constexpr int PIN_SD_POWER = 42;
constexpr int PIN_SD_SCK = 39;
constexpr int PIN_SD_MISO = 13;
constexpr int PIN_SD_MOSI = 40;
constexpr int PIN_SD_CS = 10;
constexpr uint32_t SD_HZ = 20000000;

SPIClass sdSpi(HSPI);
Preferences titles;

enum class Kind { None, Txt, Epub };

Kind kindOf(const char* name) {
  size_t n = strlen(name);
  if (n > 4 && strcasecmp(name + n - 4, ".txt") == 0) return Kind::Txt;
  if (n > 5 && strcasecmp(name + n - 5, ".epub") == 0) return Kind::Epub;
  return Kind::None;
}

void titleFromFileName(char* out, size_t cap, const char* fileName) {
  const char* dot = strrchr(fileName, '.');
  size_t n = dot ? static_cast<size_t>(dot - fileName) : strlen(fileName);
  if (n >= cap) n = cap - 1;
  for (size_t i = 0; i < n; i++) out[i] = fileName[i] == '_' ? ' ' : fileName[i];
  out[n] = '\0';
}

bool sdReadAt(void* ctx, uint32_t offset, void* dst, size_t n) {
  File& f = *static_cast<File*>(ctx);
  return f.seek(offset) && f.read(static_cast<uint8_t*>(dst), n) == n;
}

// EPUB titles come from the OPF; cached in NVS by path+size so a rescan
// doesn't re-open every book.
void epubTitleCached(BookEntry& e, File& f) {
  uint32_t h = 2166136261u;
  for (const char* p = e.path; *p; p++) h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
  h ^= e.size;
  char key[12];
  snprintf(key, sizeof(key), "t%08lx", static_cast<unsigned long>(h));
  if (titles.isKey(key) && titles.getString(key, e.title, sizeof(e.title)) > 0) return;
  ByteSource src{&f, e.size, sdReadAt};
  char t[sizeof(e.title)];
  if (epubTitle(src, t, sizeof(t))) {
    snprintf(e.title, sizeof(e.title), "%s", t);
    titles.putString(key, e.title);
  }
}

void scanDir(const char* dirPath, BookEntry* out, int& count, int max) {
  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f && count < max; f = dir.openNextFile()) {
    const char* name = f.name();
    const Kind kind = f.isDirectory() || name[0] == '.' ? Kind::None : kindOf(name);
    if (kind == Kind::None || f.size() == 0) continue;
    BookEntry& e = out[count++];
    snprintf(e.path, sizeof(e.path), "%s", f.path());
    titleFromFileName(e.title, sizeof(e.title), name);
    e.size = f.size();
    e.builtIn = false;
    e.epub = kind == Kind::Epub;
    if (e.epub) epubTitleCached(e, f);
  }
}

int byTitle(const void* a, const void* b) {
  return strcasecmp(static_cast<const BookEntry*>(a)->title, static_cast<const BookEntry*>(b)->title);
}

}  // namespace

bool libraryMountSd() {
  pinMode(PIN_SD_POWER, OUTPUT);
  digitalWrite(PIN_SD_POWER, HIGH);
  delay(10);
  SD.end();
  sdSpi.end();
  sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, sdSpi, SD_HZ)) {
    Serial.println("[sd] mount failed");
    return false;
  }
  Serial.printf("[sd] mounted, %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
  return true;
}

int libraryScan(BookEntry* out, int max, bool sdOk) {
  int count = 0;
  if (sdOk) {
    titles.begin("titles", false);
    scanDir("/", out, count, max - 1);
    scanDir("/books", out, count, max - 1);
    qsort(out, count, sizeof(BookEntry), byTitle);
    titles.end();
  }
  BookEntry& b = out[count++];
  b.path[0] = '\0';
  snprintf(b.title, sizeof(b.title), "The Gray Man (built-in)");
  b.size = builtinBookLen();
  b.builtIn = true;
  b.epub = false;
  Serial.printf("[lib] %d books\n", count);
  return count;
}

const char* builtinBookText() { return BOOK_TXT_START; }

size_t builtinBookLen() {
  size_t n = BOOK_TXT_END - BOOK_TXT_START;
  return (n > 0 && BOOK_TXT_START[n - 1] == '\0') ? n - 1 : n;
}
