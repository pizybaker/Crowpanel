#include "Library.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <strings.h>

#include "TextLayout.h"

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

bool isTxt(const char* name) {
  size_t n = strlen(name);
  return n > 4 && strcasecmp(name + n - 4, ".txt") == 0;
}

void makeTitle(char* out, size_t cap, const char* fileName) {
  size_t n = strlen(fileName) - 4;  // drop ".txt"
  if (n >= cap) n = cap - 1;
  memcpy(out, fileName, n);
  n = text::sanitizeToAscii(out, n);
  for (size_t i = 0; i < n; i++)
    if (out[i] == '_' || out[i] == '\n') out[i] = ' ';
  out[n] = '\0';
}

void scanDir(const char* dirPath, BookEntry* out, int& count, int max) {
  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f && count < max; f = dir.openNextFile()) {
    const char* name = f.name();
    if (f.isDirectory() || name[0] == '.' || !isTxt(name)) continue;
    BookEntry& e = out[count];
    snprintf(e.path, sizeof(e.path), "%s", f.path());
    makeTitle(e.title, sizeof(e.title), name);
    e.size = f.size();
    e.builtIn = false;
    if (e.size > 0) count++;
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
    scanDir("/", out, count, max - 1);
    scanDir("/books", out, count, max - 1);
    qsort(out, count, sizeof(BookEntry), byTitle);
  }
  BookEntry& b = out[count++];
  b.path[0] = '\0';
  snprintf(b.title, sizeof(b.title), "The Gray Man (built-in)");
  b.size = builtinBookLen();
  b.builtIn = true;
  Serial.printf("[lib] %d books\n", count);
  return count;
}

const char* builtinBookText() { return BOOK_TXT_START; }

size_t builtinBookLen() {
  size_t n = BOOK_TXT_END - BOOK_TXT_START;
  return (n > 0 && BOOK_TXT_START[n - 1] == '\0') ? n - 1 : n;
}
