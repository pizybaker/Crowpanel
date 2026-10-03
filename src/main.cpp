// Book reader firmware for the Elecrow CrowPanel 4.2" (green-sticker V1.2A).
//
// Boot -> Home screen: the chosen wallpaper (see Wallpaper.h), else /bg.jpg.
// MENU opens the library.
// Library: pick a .epub or .txt book from the SD card (or the built-in one),
// or the top row "Add books over Wi-Fi" to upload books from a phone.
// The selected row is inverted and a 5-key bar at the bottom highlights the
// last button pressed, with a status line saying what it did. EXIT goes home.
// Reader: rotary DOWN = next page, rotary UP = previous page, rotary press =
// contents (jump to a chapter), MENU = library, EXIT = home. Every page turn
// is a full refresh.

#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <qrcode.h>

#include "Book.h"
#include "Buttons.h"
#include "Font.h"
#include "HomeImage.h"
#include "Library.h"
#include "Transfer.h"
#include "Wallpaper.h"
#include "BigAlloc.h"
#include "fonts/sans11_bold.h"
#include "fonts/sans7_bold.h"
#include "fonts/sans7_regular.h"
#include "fonts/serif9_bold.h"
#include "fonts/serif9_italic.h"
#include "fonts/serif9_regular.h"
#include "vendor/EPD.h"

namespace {

constexpr int PIN_PWR_MAIN = 41;
constexpr int PIN_PWR_PANEL = 7;

constexpr int SCREEN_W = EPD_W;

const EpdFontData* const UI = &sans7_regular;
const EpdFontData* const UI_BOLD = &sans7_bold;
const EpdFontData* const UI_TITLE = &sans11_bold;
const EpdFontData* const BODY = &serif9_regular;
const text::Fonts BOOK_FONTS{&serif9_regular, &serif9_bold, &serif9_italic, &serif9_bold};

// Reader layout
constexpr int MARGIN_X = 12;
constexpr int BODY_TOP = 6;
constexpr int FOOTER_RULE_Y = 282;
constexpr int FOOTER_BASELINE = 295;
const text::Geometry BOOK_GEOMETRY{
    SCREEN_W - 2 * MARGIN_X,       // width
    FOOTER_RULE_Y - 4 - BODY_TOP,  // height
    25,                            // lineHeight
    21,                            // ascent (serif9 ascender)
    6,                             // descent
    18,                            // indent
    3,                             // paraGap
    10,                            // headingGap
};

// Library layout
constexpr int TITLE_BAR_H = 30;
constexpr int LIST_TOP = 34;
constexpr int ROW_H = 26;
constexpr int STATUS_RULE_Y = 244;
constexpr int STATUS_BASELINE = 259;
constexpr int BAR_TOP = 264;
constexpr int BAR_BOTTOM = 299;
constexpr int BAR_KEY_W = SCREEN_W / BUTTON_COUNT;
constexpr int VISIBLE_ROWS = (STATUS_RULE_Y - LIST_TOP) / ROW_H;

// Bar order matches Button enum order.
constexpr const char* LIBRARY_KEY_ACTIONS[BUTTON_COUNT] = {"Rescan", "Prev", "Open", "Next", "Home"};
constexpr const char* TOC_KEY_ACTIONS[BUTTON_COUNT] = {"Library", "Prev", "Go", "Next", "Back"};
constexpr const char* TRANSFER_KEY_ACTIONS[BUTTON_COUNT] = {"", "", "", "", "Done"};
constexpr int WIFI_ROW = 0;   // library row 0 is "Add books over Wi-Fi"; books follow
constexpr int FIRST_BOOK_ROW = 1;
constexpr int TOC_INDENT = 14;

uint8_t imageBuf[(EPD_W * EPD_H) / 8];
const font::Canvas canvas{imageBuf, EPD_W, EPD_H};

enum class Screen { Home, Library, Reader, Toc, Transfer };

constexpr size_t MAX_HOME_IMAGE_BYTES = 4 * 1024 * 1024;

struct Progress {
  uint32_t offset;
  uint8_t percent;
};

Preferences prefs;
BookEntry books[MAX_BOOKS];
int bookCount = 0;
int selected = FIRST_BOOK_ROW;  // library row; books[selected - FIRST_BOOK_ROW]
int scrollTop = 0;
bool sdOk = false;

Book book;
int openBook = -1;
int page = 0;
int tocSelected = 0;
int tocTop = 0;
int tocCurrent = -1;  // chapter containing the page the contents were opened from

Screen screen = Screen::Home;
transfer::Info wifi;

// Decoded home image, kept so returning home doesn't re-decode the JPEG.
uint8_t homeFrame[(EPD_W * EPD_H) / 8];
char homeImagePath[160] = "";  // file homeFrame was decoded from ("" = none)
uint32_t homeImageSize = 0;
const char* homeError = "";
int lastButton = -1;
char status[96] = "";
char readerNote[48] = "";

// ---------------------------------------------------------------- drawing

void clear() { memset(imageBuf, 0xFF, sizeof(imageBuf)); }

void fillRect(int x0, int y0, int x1, int y1, bool black) {
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      uint8_t& b = imageBuf[y * (EPD_W / 8) + x / 8];
      const uint8_t mask = 0x80 >> (x & 7);
      b = black ? (b & ~mask) : (b | mask);
    }
  }
}

void hRule(int y) { fillRect(0, y, SCREEN_W, y + 1, true); }

void frame(int x0, int y0, int x1, int y1) {
  fillRect(x0, y0, x1, y0 + 1, true);
  fillRect(x0, y1 - 1, x1, y1, true);
  fillRect(x0, y0, x0 + 1, y1, true);
  fillRect(x1 - 1, y0, x1, y1, true);
}

int textWidth(const EpdFontData* f, const char* s) { return font::textWidth(f, s, strlen(s)); }

void drawText(const EpdFontData* f, int x, int baseline, const char* s, bool inverted = false) {
  font::drawText(canvas, f, x, baseline, s, strlen(s), !inverted);
}

void drawCentered(const EpdFontData* f, int x0, int w, int baseline, const char* s, bool inverted = false) {
  drawText(f, x0 + (w - textWidth(f, s)) / 2, baseline, s, inverted);
}

void drawRight(const EpdFontData* f, int xRight, int baseline, const char* s, bool inverted = false) {
  drawText(f, xRight - textWidth(f, s), baseline, s, inverted);
}

// Copies `in` into `out`, shortened with an ellipsis to fit maxWidth pixels.
void fitText(const EpdFontData* f, char* out, size_t cap, const char* in, int maxWidth) {
  snprintf(out, cap, "%s", in);
  if (textWidth(f, out) <= maxWidth) return;
  size_t n = strlen(out);
  while (n > 0) {
    do n--;
    while (n > 0 && (static_cast<uint8_t>(out[n]) & 0xC0) == 0x80);  // UTF-8 boundary
    while (n > 0 && out[n - 1] == ' ') n--;
    if (n + 4 > cap) continue;
    memcpy(out + n, "\xE2\x80\xA6", 4);  // "…" + NUL
    if (textWidth(f, out) <= maxWidth) return;
  }
}

// Every screen change is the same full GC refresh. The 0x17 A5 update powers
// the panel's drivers down when it finishes but leaves the controller awake,
// so the panel stays out of deep sleep between refreshes and the 210 ms
// hardware reset only runs on the first refresh or after a BUSY timeout.
bool panelNeedsReset = true;

void present() {
  uint32_t t0 = millis();
  if (panelNeedsReset) {
    EPD_RESET();
    panelNeedsReset = false;
  }
  EPD_Init();
  if (!EPD_Display(imageBuf)) panelNeedsReset = true;
  Serial.printf("[epd] refresh %lu ms\n", millis() - t0);
}

// ---------------------------------------------------------------- progress

void progressKey(char* out, size_t cap, const BookEntry& e) {
  uint32_t h = 2166136261u;  // FNV-1a of the path (NVS keys max 15 chars)
  const char* p = e.builtIn ? "<builtin>" : e.path;
  for (; *p; p++) h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
  snprintf(out, cap, "b%08lx", static_cast<unsigned long>(h));
}

bool loadProgress(const BookEntry& e, Progress& out) {
  char key[16];
  progressKey(key, sizeof(key), e);
  return prefs.isKey(key) && prefs.getBytes(key, &out, sizeof(out)) == sizeof(out);
}

void saveProgress() {
  if (openBook < 0 || !book.isOpen()) return;
  const BookEntry& e = books[openBook];
  int count = book.pageCount();
  Progress p{book.pageOffset(page), static_cast<uint8_t>(count > 1 ? page * 100 / (count - 1) : 100)};
  char key[16];
  progressKey(key, sizeof(key), e);
  prefs.putBytes(key, &p, sizeof(p));
}

// ---------------------------------------------------------------- screens

// Title bar, scrolling rows, status line and 5-key bar shared by the library
// and contents screens. drawRow(index, y, selected) draws one row's content.
// Title bar, status line and 5-key bar (the last pressed key inverted).
void drawChrome(const char* title, const char* counter, const char* statusText, const char* const* actions) {
  fillRect(0, 0, SCREEN_W, TITLE_BAR_H, true);
  drawText(UI_TITLE, MARGIN_X, 23, title, true);
  if (counter[0]) drawRight(UI, SCREEN_W - MARGIN_X, 20, counter, true);

  hRule(STATUS_RULE_Y);
  char line[112];
  fitText(UI, line, sizeof(line), statusText, SCREEN_W - 2 * MARGIN_X);
  drawText(UI, MARGIN_X, STATUS_BASELINE, line);

  for (int k = 0; k < BUTTON_COUNT; k++) {
    const int x0 = k * BAR_KEY_W;
    const bool hit = k == lastButton;
    if (hit) fillRect(x0, BAR_TOP, x0 + BAR_KEY_W, BAR_BOTTOM + 1, true);
    frame(x0, BAR_TOP, x0 + BAR_KEY_W, BAR_BOTTOM + 1);
    drawCentered(UI_BOLD, x0, BAR_KEY_W, BAR_TOP + 15, buttonName(static_cast<Button>(k)), hit);
    drawCentered(UI, x0, BAR_KEY_W, BAR_TOP + 31, actions[k], hit);
  }
}

template <typename RowFn>
void drawListScreen(const char* title, int sel, int count, int top, const char* statusText,
                    const char* const* actions, RowFn drawRow) {
  clear();
  char counter[32];
  snprintf(counter, sizeof(counter), "%d / %d", sel + 1, count);
  drawChrome(title, counter, statusText, actions);

  for (int r = 0; r < VISIBLE_ROWS && top + r < count; r++) {
    const int y = LIST_TOP + r * ROW_H;
    const bool isSel = top + r == sel;
    if (isSel) fillRect(0, y, SCREEN_W, y + ROW_H - 2, true);
    drawRow(top + r, y, isSel);
  }
  if (count > VISIBLE_ROWS) {  // scrollbar thumb in the right margin
    const int trackH = VISIBLE_ROWS * ROW_H - 2;
    const int thumbH = trackH * VISIBLE_ROWS / count < 8 ? 8 : trackH * VISIBLE_ROWS / count;
    const int thumbY = LIST_TOP + (trackH - thumbH) * top / (count - VISIBLE_ROWS);
    fillRect(SCREEN_W - 4, thumbY, SCREEN_W - 1, thumbY + thumbH, true);
  }
}

// One list row: optional left marker, title (ellipsized), optional right label.
void drawRow(int y, bool sel, int indent, const char* marker, const char* title, const char* right) {
  const int rightW = right[0] ? textWidth(UI, right) + 10 : 0;
  char fitted[112];
  fitText(BODY, fitted, sizeof(fitted), title, SCREEN_W - 2 * MARGIN_X - 14 - indent - rightW);
  if (marker[0]) drawText(BODY, MARGIN_X + indent, y + 18, marker, sel);
  drawText(BODY, MARGIN_X + 14 + indent, y + 18, fitted, sel);
  if (right[0]) drawRight(UI, SCREEN_W - MARGIN_X, y + 17, right, sel);
}

void renderLibrary() {
  const int rows = bookCount + FIRST_BOOK_ROW;
  drawListScreen("Library", selected, rows, scrollTop, status, LIBRARY_KEY_ACTIONS, [](int row, int y, bool sel) {
    if (row == WIFI_ROW) {
      drawRow(y, sel, 0, "+", "Add books over Wi-Fi", "");
      return;
    }
    const BookEntry& b = books[row - FIRST_BOOK_ROW];
    char right[16] = "";
    Progress p;
    if (loadProgress(b, p)) snprintf(right, sizeof(right), "%u%%", p.percent);
    else if (b.epub) snprintf(right, sizeof(right), "EPUB");
    drawRow(y, sel, 0, sel ? "\xE2\x80\xBA" : "", b.title, right);  // "›"
  });
}

// QR code with its top-left at (x, y), modules scaled to fit maxSize pixels.
void drawQr(int x, int y, int maxSize, const char* text) {
  // ricmoo/QRCode does not check capacity (oversized input overruns its
  // buffers), so pick the version here. Byte-mode capacity at ECC_LOW:
  static constexpr uint8_t CAPACITY[] = {17, 32, 53, 78, 106, 134};
  const size_t len = strlen(text);
  for (uint8_t version = 1; version <= sizeof(CAPACITY); version++) {
    if (len > CAPACITY[version - 1]) continue;
    uint8_t data[qrcode_getBufferSize(sizeof(CAPACITY))];
    QRCode qr;
    if (qrcode_initText(&qr, data, version, ECC_LOW, text) != 0) return;
    const int px = maxSize / qr.size;
    const int size = qr.size * px;
    const int x0 = x + (maxSize - size) / 2;
    for (uint8_t cy = 0; cy < qr.size; cy++) {
      for (uint8_t cx = 0; cx < qr.size; cx++) {
        if (qrcode_getModule(&qr, cx, cy)) fillRect(x0 + cx * px, y + cy * px, x0 + (cx + 1) * px, y + (cy + 1) * px, true);
      }
    }
    return;
  }
}

void renderTransfer() {
  clear();
  drawChrome("Wi-Fi transfer", "", transfer::lastMessage(), TRANSFER_KEY_ACTIONS);

  char joinQr[80];
  snprintf(joinQr, sizeof(joinQr), "WIFI:T:WPA;S:%s;P:%s;;", wifi.ssid, wifi.password);
  constexpr int QR = 124, COL = SCREEN_W / 2;
  drawQr((COL - QR) / 2, 36, QR, joinQr);
  drawQr(COL + (COL - QR) / 2, 36, QR, wifi.url);
  drawCentered(UI_BOLD, 0, COL, 180, "1. Scan to join Wi-Fi");
  drawCentered(UI_BOLD, COL, COL, 180, "2. Scan to open page");

  char line[112];
  snprintf(line, sizeof(line), "Wi-Fi  %s   \xC2\xB7   Password  %s", wifi.ssid, wifi.password);
  drawCentered(UI, 0, SCREEN_W, 206, line);
  snprintf(line, sizeof(line), "Then open  %s  in your browser", wifi.url);
  drawCentered(UI, 0, SCREEN_W, 228, line);
}

void renderToc() {
  const auto& ch = book.chapters();
  drawListScreen("Contents", tocSelected, static_cast<int>(ch.size()), tocTop, status, TOC_KEY_ACTIONS,
                 [&](int i, int y, bool sel) {
                   char right[16];
                   snprintf(right, sizeof(right), "%d", book.pageForOffset(ch[i].offset) + 1);
                   // "›" on the selection, "•" on the chapter being read.
                   const char* marker = sel ? "\xE2\x80\xBA" : i == tocCurrent ? "\xE2\x80\xA2" : "";
                   drawRow(y, sel, ch[i].depth * TOC_INDENT, marker, ch[i].title, right);
                 });
}

void renderReader() {
  clear();

  book.layoutPage(page, [](const EpdFontData* f, int x, int baseline, const char* s, size_t n) {
    font::drawText(canvas, f, MARGIN_X + x, BODY_TOP + baseline, s, n);
  });

  hRule(FOOTER_RULE_Y);
  const int count = book.pageCount();
  char right[48];
  if (readerNote[0]) {
    snprintf(right, sizeof(right), "%s  \xC2\xB7  %d / %d", readerNote, page + 1, count);
  } else {
    snprintf(right, sizeof(right), "%d / %d  \xC2\xB7  %d%%", page + 1, count,
             count > 1 ? page * 100 / (count - 1) : 100);
  }
  const int rightW = textWidth(UI, right);
  drawRight(UI, SCREEN_W - MARGIN_X, FOOTER_BASELINE, right);
  char title[112];
  fitText(UI, title, sizeof(title), books[openBook].title, SCREEN_W - 2 * MARGIN_X - rightW - 16);
  drawText(UI, MARGIN_X, FOOTER_BASELINE, title);
}

void renderMessage(const char* heading, const char* detail) {
  clear();
  drawCentered(UI_TITLE, 0, SCREEN_W, 140, heading);
  char line[112];
  fitText(BODY, line, sizeof(line), detail, SCREEN_W - 2 * MARGIN_X);
  drawCentered(BODY, 0, SCREEN_W, 172, line);
  present();
}

// Re-decodes only when the wallpaper file changed since the last decode.
bool loadHomeImage() {
  char path[160];
  wallpaper::current(path, sizeof(path));
  File f = sdOk ? SD.open(path, FILE_READ) : File();
  if (!f) {
    homeImagePath[0] = '\0';
    homeError = sdOk ? "add one over Wi-Fi, or put bg.jpg on the SD card" : "no SD card";
    return false;
  }
  const size_t size = f.size();
  if (size == homeImageSize && strcmp(path, homeImagePath) == 0) return true;
  homeImagePath[0] = '\0';
  if (size == 0 || size > MAX_HOME_IMAGE_BYTES) {
    homeError = "the image is empty or larger than 4 MB";
    return false;
  }
  auto* jpg = static_cast<uint8_t*>(bigAlloc(size));
  if (!jpg) {
    homeError = "out of memory";
    return false;
  }
  const uint32_t t0 = millis();
  bool ok = f.read(jpg, size) == size && decodeJpegToFrame(jpg, size, homeFrame, EPD_W, EPD_H, &homeError);
  bigFree(jpg);
  if (ok) {
    snprintf(homeImagePath, sizeof(homeImagePath), "%s", path);
    homeImageSize = size;
  }
  Serial.printf("[home] %s %s, %u bytes in %lu ms%s%s\n", ok ? "decoded" : "failed", path, static_cast<unsigned>(size),
                millis() - t0, ok ? "" : ": ", ok ? "" : homeError);
  return ok;
}

void renderHome() {
  if (loadHomeImage()) {
    memcpy(imageBuf, homeFrame, sizeof(imageBuf));
    return;
  }
  clear();
  drawCentered(UI_TITLE, 0, SCREEN_W, 120, "CrowPanel Reader");
  drawCentered(BODY, 0, SCREEN_W, 160, "Press MENU for the library");
  char msg[112], line[112];
  snprintf(msg, sizeof(msg), "Wallpaper: %s", homeError);
  fitText(UI, line, sizeof(line), msg, SCREEN_W - 2 * MARGIN_X);
  drawCentered(UI, 0, SCREEN_W, 280, line);
}

void render() {
  if (screen == Screen::Home) renderHome();
  else if (screen == Screen::Library) renderLibrary();
  else if (screen == Screen::Toc) renderToc();
  else if (screen == Screen::Transfer) renderTransfer();
  else renderReader();
  present();
  readerNote[0] = '\0';
}

// ---------------------------------------------------------------- actions

void keepVisible(int sel, int& top) {
  if (sel < top) top = sel;
  if (sel >= top + VISIBLE_ROWS) top = sel - VISIBLE_ROWS + 1;
}

void keepSelectionVisible() { keepVisible(selected, scrollTop); }

const char* bookKey(const BookEntry& e) { return e.builtIn ? "<builtin>" : e.path; }

void selectPath(const char* path) {
  for (int i = 0; i < bookCount; i++) {
    if (strcmp(bookKey(books[i]), path) == 0) {
      selected = i + FIRST_BOOK_ROW;
      break;
    }
  }
  keepSelectionVisible();
}

void rescan() {
  char keep[160] = "";
  if (selected >= FIRST_BOOK_ROW) snprintf(keep, sizeof(keep), "%s", bookKey(books[selected - FIRST_BOOK_ROW]));
  const bool onWifiRow = selected == WIFI_ROW;
  sdOk = libraryMountSd();
  bookCount = libraryScan(books, MAX_BOOKS, sdOk);
  selected = onWifiRow ? WIFI_ROW : FIRST_BOOK_ROW;
  scrollTop = 0;
  selectPath(keep);
}

void startTransfer() {
  if (!sdOk) {
    snprintf(status, sizeof(status), "Insert an SD card to add books");
    return;
  }
  if (!transfer::start(wifi)) {
    snprintf(status, sizeof(status), "Wi-Fi could not start");
    return;
  }
  screen = Screen::Transfer;
}

void stopTransfer() {
  transfer::stop();
  const int added = transfer::receivedCount();
  if (transfer::libraryChanged()) rescan();
  if (transfer::wallpaperChanged()) homeImagePath[0] = '\0';  // re-decode on next home visit
  if (added) snprintf(status, sizeof(status), "Added %d book%s over Wi-Fi", added, added == 1 ? "" : "s");
  else snprintf(status, sizeof(status), "Wi-Fi off");
  screen = Screen::Library;
}

bool openSelected() {
  const BookEntry& e = books[selected - FIRST_BOOK_ROW];
  // First open of an EPUB extracts every chapter; say so rather than look frozen.
  if (bookNeedsExtraction(e)) renderMessage("Opening\xE2\x80\xA6", e.title);
  if (!book.open(e, BOOK_FONTS, BOOK_GEOMETRY)) {
    Serial.printf("[book] open '%s' failed: %s\n", e.title, book.error());
    return false;
  }
  openBook = selected - FIRST_BOOK_ROW;
  Progress p;
  page = loadProgress(e, p) ? book.pageForOffset(p.offset) : 0;
  prefs.putString("last", bookKey(e));
  return true;
}

void handleLibrary(Button b) {
  switch (b) {
    case Button::Up:
    case Button::Down: {
      const int next = selected + (b == Button::Up ? -1 : 1);
      if (next >= 0 && next < bookCount + FIRST_BOOK_ROW) selected = next;
      if (selected == WIFI_ROW) snprintf(status, sizeof(status), "%s: press to add books from your phone", buttonName(b));
      else snprintf(status, sizeof(status), "%s: book %d of %d", buttonName(b), selected, bookCount);
      break;
    }
    case Button::Ok:
      if (selected == WIFI_ROW) {
        startTransfer();
        return;
      }
      if (openSelected()) {
        screen = Screen::Reader;
        snprintf(status, sizeof(status), "OK: opened \xE2\x80\x9C%.60s\xE2\x80\x9D", books[openBook].title);
        return;
      }
      snprintf(status, sizeof(status), "OK: could not open \xE2\x80\x93 %s", book.error());
      break;
    case Button::Menu:
      rescan();
      snprintf(status, sizeof(status), "MENU: rescanned SD \xE2\x80\x93 %d book%s%s", bookCount,
               bookCount == 1 ? "" : "s", sdOk ? "" : " (no SD card)");
      break;
    case Button::Exit:
      screen = Screen::Home;
      break;
  }
  keepSelectionVisible();
}

void openToc() {
  const auto& ch = book.chapters();
  if (ch.empty()) {
    snprintf(readerNote, sizeof(readerNote), "No chapters found");
    return;
  }
  tocCurrent = book.chapterForPage(page);
  tocSelected = tocCurrent < 0 ? 0 : tocCurrent;
  tocTop = 0;
  keepVisible(tocSelected, tocTop);
  snprintf(status, sizeof(status), "Now on page %d of %d \xE2\x80\x93 rotate to pick a chapter", page + 1,
           book.pageCount());
  screen = Screen::Toc;
}

void closeBook(Button b) {
  saveProgress();
  book.close();
  openBook = -1;
  screen = b == Button::Menu ? Screen::Library : Screen::Home;
  snprintf(status, sizeof(status), "%s: closed the book", buttonName(b));
}

void handleToc(Button b) {
  const auto& ch = book.chapters();
  const int count = static_cast<int>(ch.size());
  switch (b) {
    case Button::Up:
      if (tocSelected > 0) tocSelected--;
      snprintf(status, sizeof(status), "UP: chapter %d of %d", tocSelected + 1, count);
      break;
    case Button::Down:
      if (tocSelected + 1 < count) tocSelected++;
      snprintf(status, sizeof(status), "DOWN: chapter %d of %d", tocSelected + 1, count);
      break;
    case Button::Ok:
      page = book.pageForOffset(ch[tocSelected].offset);
      saveProgress();
      screen = Screen::Reader;
      break;
    case Button::Exit:
      screen = Screen::Reader;  // back to the page we came from
      break;
    case Button::Menu:
      closeBook(b);
      break;
  }
  keepVisible(tocSelected, tocTop);
}

void handleReader(Button b) {
  switch (b) {
    case Button::Ok:
      openToc();
      break;
    case Button::Down:
      if (page + 1 < book.pageCount()) {
        page++;
        saveProgress();
      } else {
        snprintf(readerNote, sizeof(readerNote), "End of book");
      }
      break;
    case Button::Up:
      if (page > 0) {
        page--;
        saveProgress();
      } else {
        snprintf(readerNote, sizeof(readerNote), "First page");
      }
      break;
    case Button::Menu:
    case Button::Exit:
      closeBook(b);
      break;
  }
}

// Only MENU does anything at home; other keys don't cost a refresh.
bool handleHome(Button b) {
  if (b != Button::Menu) return false;
  screen = Screen::Library;
  snprintf(status, sizeof(status), "Rotate to choose a book, press to open");
  return true;
}

// Returns true when the screen needs redrawing.
bool handle(Button b) {
  Serial.printf("[btn] %s\n", buttonName(b));
  if (screen == Screen::Home) {
    if (!handleHome(b)) return false;
    lastButton = static_cast<int>(b);
    return true;
  }
  if (screen == Screen::Transfer) {
    if (b != Button::Exit) return false;  // only EXIT ("Done") acts here
    lastButton = static_cast<int>(b);
    stopTransfer();
    return true;
  }
  lastButton = static_cast<int>(b);
  if (screen == Screen::Library) handleLibrary(b);
  else if (screen == Screen::Toc) handleToc(b);
  else handleReader(b);
  return true;
}

}  // namespace

void setup() {
  pinMode(PIN_PWR_MAIN, OUTPUT);
  digitalWrite(PIN_PWR_MAIN, HIGH);
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] CrowPanel reader starting");

  // Panel power rail first: the panel does not respond until it is up.
  pinMode(PIN_PWR_PANEL, OUTPUT);
  digitalWrite(PIN_PWR_PANEL, HIGH);
  delay(10);
  EPD_GPIOInit();

  if (!buttonsBegin()) Serial.println("[boot] button task failed to start");
  prefs.begin("reader", false);

  sdOk = libraryMountSd();
  bookCount = libraryScan(books, MAX_BOOKS, sdOk);
  if (prefs.isKey("last")) selectPath(prefs.getString("last").c_str());

  snprintf(status, sizeof(status), "%s",
           sdOk ? "Rotate to choose a book, press to open" : "No SD card \xE2\x80\x93 built-in book only");
  Serial.printf("[boot] PSRAM free: %u bytes\n", static_cast<unsigned>(ESP.getFreePsram()));

  render();
}

void loop() {
  Button b;
  // While the Wi-Fi transfer screen is up, serve the phone between key checks.
  const bool serving = screen == Screen::Transfer;
  if (!buttonsNext(b, serving ? pdMS_TO_TICKS(5) : portMAX_DELAY)) {
    if (serving) {
      transfer::poll();
      if (transfer::takeSettledChange()) render();
    }
    return;
  }
  bool dirty = handle(b);
  // Apply presses made during the last refresh, then draw once.
  while (buttonsNext(b, 0)) dirty |= handle(b);
  if (dirty) render();
}
