// Book reader firmware for the Elecrow CrowPanel 4.2" (green-sticker V1.2A).
//
// Boot -> Library screen: pick a .txt book from the SD card (or the built-in
// one). The selected row is inverted and a 5-key bar at the bottom highlights
// the last button pressed, with a status line saying what it did.
// Reader screen: rotary DOWN / press = next page, rotary UP = previous page,
// MENU / EXIT = back to the library. Every page turn is a full refresh.

#include <Arduino.h>
#include <Preferences.h>

#include "Book.h"
#include "Buttons.h"
#include "Library.h"
#include "vendor/EPD.h"
#include "vendor/EPD_GUI.h"

namespace {

constexpr int PIN_PWR_MAIN = 41;
constexpr int PIN_PWR_PANEL = 7;

constexpr int SCREEN_W = EPD_W;

// Vendor font sizes: size N is an (N/2) x N cell.
constexpr int FONT_S = 12;
constexpr int FONT_M = 16;
constexpr int FONT_L = 24;

// Reader layout
constexpr int MARGIN_X = 8;
constexpr int HEADER_Y = 2;
constexpr int HEADER_RULE_Y = 17;
constexpr int BODY_TOP = 21;
constexpr int LINE_H = 18;
constexpr int FOOTER_RULE_Y = 277;
constexpr int FOOTER_Y = 283;
constexpr int BODY_COLS = (SCREEN_W - 2 * MARGIN_X) / (FONT_M / 2);
constexpr int BODY_LINES = (FOOTER_RULE_Y - 3 - BODY_TOP) / LINE_H;

// Library layout
constexpr int TITLE_BAR_H = 28;
constexpr int LIST_TOP = 32;
constexpr int ROW_H = 24;
constexpr int STATUS_RULE_Y = 238;
constexpr int BAR_TOP = 258;
constexpr int BAR_BOTTOM = 297;
constexpr int BAR_KEY_W = SCREEN_W / BUTTON_COUNT;
constexpr int VISIBLE_ROWS = (STATUS_RULE_Y - LIST_TOP) / ROW_H;
constexpr int ROW_TITLE_CHARS = (SCREEN_W - 2 * MARGIN_X) / (FONT_M / 2) - 2 - 5;  // "> " prefix, " 100%"

// Bar order matches Button enum order.
constexpr const char* LIBRARY_KEY_ACTIONS[BUTTON_COUNT] = {"Rescan", "Prev", "Open", "Next", "-"};

// +1 row: vendor EPD_ShowPicture/ShowChar offset y by 1 without clamping.
uint8_t imageBuf[(EPD_W * EPD_H) / 8 + (EPD_W / 8)];

enum class Screen { Library, Reader };

struct Progress {
  uint32_t offset;
  uint8_t percent;
};

Preferences prefs;
BookEntry books[MAX_BOOKS];
int bookCount = 0;
int selected = 0;
int scrollTop = 0;
bool sdOk = false;

Book book;
int openBook = -1;
int page = 0;

Screen screen = Screen::Library;
int lastButton = -1;
char status[72] = "";
char readerNote[48] = "";

// ---------------------------------------------------------------- drawing

// Clipped to the screen width: vendor Paint_SetPixel has no bounds check, so
// an overlong string would wrap into the next rows of the framebuffer.
void drawText(int x, int y, const char* s, int size, bool inverted = false) {
  char clipped[80];
  int maxChars = (SCREEN_W - 2 - x) / (size / 2);
  snprintf(clipped, sizeof(clipped), "%.*s", maxChars < 0 ? 0 : maxChars, s);
  EPD_ShowString(x, y, clipped, size, inverted ? WHITE : BLACK);
}

void drawTextCentered(int x0, int w, int y, const char* s, int size, bool inverted) {
  int tw = static_cast<int>(strlen(s)) * (size / 2);
  drawText(x0 + (w - tw) / 2, y, s, size, inverted);
}

void drawTextRight(int xRight, int y, const char* s, int size, bool inverted = false) {
  drawText(xRight - static_cast<int>(strlen(s)) * (size / 2), y, s, size, inverted);
}

void fillRect(int x0, int y0, int x1, int y1, bool black) { EPD_ClearWindows(x0, y0, x1, y1, black ? BLACK : WHITE); }

void hRule(int y) { EPD_DrawLine(0, y, SCREEN_W - 1, y, BLACK); }

void fitText(char* out, size_t cap, const char* in, int maxChars) {
  int n = static_cast<int>(strlen(in));
  if (n <= maxChars) {
    snprintf(out, cap, "%s", in);
    return;
  }
  snprintf(out, cap, "%.*s...", maxChars - 3, in);
}

// Every screen change is the same full refresh: reset + init before each one,
// exactly like the vendor examples and Ssd1683LegacyDriver.
void present() {
  uint32_t t0 = millis();
  EPD_RESET();
  EPD_Init();
  EPD_Display(imageBuf);
  EPD_Sleep();
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
  return prefs.getBytes(key, &out, sizeof(out)) == sizeof(out);
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

void renderLibrary() {
  EPD_Full(WHITE);

  fillRect(0, 0, SCREEN_W, TITLE_BAR_H, true);
  drawText(MARGIN_X, 2, "Library", FONT_L, true);
  char buf[72];
  snprintf(buf, sizeof(buf), "%d/%d", selected + 1, bookCount);
  drawTextRight(SCREEN_W - MARGIN_X, 6, buf, FONT_M, true);

  for (int r = 0; r < VISIBLE_ROWS; r++) {
    int i = scrollTop + r;
    if (i >= bookCount) break;
    int y = LIST_TOP + r * ROW_H;
    bool sel = i == selected;
    if (sel) fillRect(0, y, SCREEN_W, y + ROW_H - 2, true);

    char title[72];
    fitText(title, sizeof(title), books[i].title, ROW_TITLE_CHARS);
    snprintf(buf, sizeof(buf), "%s%s", sel ? "> " : "  ", title);
    drawText(MARGIN_X, y + 3, buf, FONT_M, sel);

    Progress p;
    if (loadProgress(books[i], p)) {
      snprintf(buf, sizeof(buf), "%u%%", p.percent);
      drawTextRight(SCREEN_W - MARGIN_X, y + 3, buf, FONT_M, sel);
    }
  }
  if (scrollTop > 0) drawTextRight(SCREEN_W - 2, LIST_TOP - 4, "^", FONT_S);
  if (scrollTop + VISIBLE_ROWS < bookCount) drawTextRight(SCREEN_W - 2, STATUS_RULE_Y - 14, "v", FONT_S);

  hRule(STATUS_RULE_Y);
  drawText(MARGIN_X - 2, STATUS_RULE_Y + 3, status, FONT_S);

  for (int k = 0; k < BUTTON_COUNT; k++) {
    int x0 = k * BAR_KEY_W;
    int x1 = x0 + BAR_KEY_W - 1;
    bool hit = k == lastButton;
    if (hit) fillRect(x0, BAR_TOP, x1 + 1, BAR_BOTTOM + 1, true);
    EPD_DrawRectangle(x0, BAR_TOP, x1, BAR_BOTTOM, BLACK, 0);
    drawTextCentered(x0, BAR_KEY_W, BAR_TOP + 3, buttonName(static_cast<Button>(k)), FONT_M, hit);
    drawTextCentered(x0, BAR_KEY_W, BAR_TOP + 22, LIBRARY_KEY_ACTIONS[k], FONT_S, hit);
  }
}

void renderReader() {
  EPD_Full(WHITE);

  char buf[72];
  fitText(buf, sizeof(buf), books[openBook].title, (SCREEN_W - 2 * MARGIN_X) / (FONT_S / 2));
  drawText(MARGIN_X, HEADER_Y, buf, FONT_S);
  hRule(HEADER_RULE_Y);

  book.layoutPage(page, [](int line, const char* s, int len) {
    if (len > 0) drawText(MARGIN_X, BODY_TOP + line * LINE_H, s, FONT_M);
  });

  hRule(FOOTER_RULE_Y);
  int count = book.pageCount();
  snprintf(buf, sizeof(buf), "Page %d of %d", page + 1, count);
  drawText(MARGIN_X, FOOTER_Y, buf, FONT_S);
  snprintf(buf, sizeof(buf), "%d%%", count > 1 ? page * 100 / (count - 1) : 100);
  drawTextRight(SCREEN_W - MARGIN_X, FOOTER_Y, buf, FONT_S);
  if (readerNote[0]) drawTextCentered(0, SCREEN_W, FOOTER_Y, readerNote, FONT_S, false);
}

void render() {
  if (screen == Screen::Library) renderLibrary();
  else renderReader();
  present();
  readerNote[0] = '\0';
}

// ---------------------------------------------------------------- actions

void keepSelectionVisible() {
  if (selected < scrollTop) scrollTop = selected;
  if (selected >= scrollTop + VISIBLE_ROWS) scrollTop = selected - VISIBLE_ROWS + 1;
}

void selectPath(const char* path) {
  for (int i = 0; i < bookCount; i++) {
    const char* p = books[i].builtIn ? "<builtin>" : books[i].path;
    if (strcmp(p, path) == 0) {
      selected = i;
      break;
    }
  }
  keepSelectionVisible();
}

void rescan() {
  char keep[160];
  snprintf(keep, sizeof(keep), "%s", books[selected].builtIn ? "<builtin>" : books[selected].path);
  sdOk = libraryMountSd();
  bookCount = libraryScan(books, MAX_BOOKS, sdOk);
  selected = 0;
  scrollTop = 0;
  selectPath(keep);
}

bool openSelected() {
  const BookEntry& e = books[selected];
  if (!book.open(e, BODY_COLS, BODY_LINES)) {
    Serial.printf("[book] open '%s' failed: %s\n", e.title, book.error());
    return false;
  }
  openBook = selected;
  Progress p;
  page = loadProgress(e, p) ? book.pageForOffset(p.offset) : 0;
  prefs.putString("last", e.builtIn ? "<builtin>" : e.path);
  return true;
}

void handleLibrary(Button b) {
  switch (b) {
    case Button::Up:
      if (selected > 0) {
        selected--;
        snprintf(status, sizeof(status), "UP: book %d of %d", selected + 1, bookCount);
      } else {
        snprintf(status, sizeof(status), "UP: already at the first book");
      }
      break;
    case Button::Down:
      if (selected + 1 < bookCount) {
        selected++;
        snprintf(status, sizeof(status), "DOWN: book %d of %d", selected + 1, bookCount);
      } else {
        snprintf(status, sizeof(status), "DOWN: already at the last book");
      }
      break;
    case Button::Ok:
      if (openSelected()) {
        screen = Screen::Reader;
        snprintf(status, sizeof(status), "OK: opened \"%.40s\"", books[openBook].title);
        return;
      }
      snprintf(status, sizeof(status), "OK: could not open - %s", book.error());
      break;
    case Button::Menu:
      rescan();
      snprintf(status, sizeof(status), "MENU: rescanned SD - %d book%s%s", bookCount, bookCount == 1 ? "" : "s",
               sdOk ? "" : " (no SD card)");
      break;
    case Button::Exit:
      snprintf(status, sizeof(status), "EXIT: already on the library screen");
      break;
  }
  keepSelectionVisible();
}

void handleReader(Button b) {
  switch (b) {
    case Button::Down:
    case Button::Ok:
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
      saveProgress();
      book.close();
      openBook = -1;
      screen = Screen::Library;
      snprintf(status, sizeof(status), "%s: back to library", buttonName(b));
      break;
  }
}

void handle(Button b) {
  lastButton = static_cast<int>(b);
  Serial.printf("[btn] %s\n", buttonName(b));
  if (screen == Screen::Library) handleLibrary(b);
  else handleReader(b);
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
  Paint_NewImage(imageBuf, EPD_W, EPD_H, ROTATE_0, WHITE);

  if (!buttonsBegin()) Serial.println("[boot] button task failed to start");
  prefs.begin("reader", false);

  sdOk = libraryMountSd();
  bookCount = libraryScan(books, MAX_BOOKS, sdOk);
  String last = prefs.getString("last", "");
  if (last.length()) selectPath(last.c_str());

  snprintf(status, sizeof(status), sdOk ? "Rotate to choose a book, press to open" : "No SD card - built-in book only");
  Serial.printf("[boot] PSRAM free: %u bytes\n", static_cast<unsigned>(ESP.getFreePsram()));

  render();
}

void loop() {
  Button b;
  if (!buttonsNext(b, portMAX_DELAY)) return;
  handle(b);
  // Apply presses made during the last refresh, then draw once.
  while (buttonsNext(b, 0)) handle(b);
  render();
}
