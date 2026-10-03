// Font-comparison firmware for the CrowPanel 4.2": one page per font, the
// same sample line at every size. Rotary down/up steps through the fonts.
// Build/flash: pio run -e font_test -t upload
// Fonts come from tools/fonttest/gen_fonts.py.

#include <Arduino.h>

#include "../Buttons.h"
#include "../vendor/EPD.h"
#include "FontTestPage.h"

namespace {

constexpr int PIN_PWR_MAIN = 41;
constexpr int PIN_PWR_PANEL = 7;

// One spare row: the vendor driver can write one row past the frame.
uint8_t imageBuf[(EPD_W * EPD_H) / 8 + EPD_W / 8];
const font::Canvas canvas{imageBuf, EPD_W, EPD_H};

int page = 0;
bool panelNeedsReset = true;

void present() {
  const uint32_t t0 = millis();
  if (panelNeedsReset) {
    EPD_RESET();
    panelNeedsReset = false;
  }
  EPD_Init();
  if (!EPD_Display(imageBuf)) panelNeedsReset = true;
  Serial.printf("[fonttest] %s refresh %lu ms\n", TEST_FONTS[page].name, millis() - t0);
}

void show() {
  fonttest::renderPage(canvas, page);
  present();
}

}  // namespace

void setup() {
  pinMode(PIN_PWR_MAIN, OUTPUT);
  digitalWrite(PIN_PWR_MAIN, HIGH);
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[fonttest] %d fonts\n", TEST_FONT_COUNT);

  // Panel power rail first: the panel does not respond until it is up.
  pinMode(PIN_PWR_PANEL, OUTPUT);
  digitalWrite(PIN_PWR_PANEL, HIGH);
  delay(10);
  EPD_GPIOInit();

  if (!buttonsBegin()) Serial.println("[fonttest] button task failed to start");
  show();
}

void loop() {
  Button b;
  if (!buttonsNext(b, portMAX_DELAY)) return;
  int step = 0;
  do {
    if (b == Button::Down) step++;
    if (b == Button::Up) step--;
  } while (buttonsNext(b, 0));  // fold presses made during the last refresh
  if (step == 0) return;
  page = ((page + step) % TEST_FONT_COUNT + TEST_FONT_COUNT) % TEST_FONT_COUNT;
  show();
}
