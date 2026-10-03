// DU waveform comparison firmware for the CrowPanel 4.2". For each frame
// count in FRAME_SETTINGS: a GC refresh to a clean page, then
// CHANGES_PER_SETTING timed DU changes between two different book pages. The
// last page stays up for inspection: rotate down for the next setting, up to
// repeat this one. A summary of the measured times follows the last setting.
// Build/flash: pio run -e waveform_test -t upload

#include <Arduino.h>

#include "../Buttons.h"
#include "../vendor/EPD.h"
#include "WaveTestPage.h"

namespace {

constexpr int PIN_PWR_MAIN = 41;
constexpr int PIN_PWR_PANEL = 7;
constexpr uint32_t PAUSE_BETWEEN_CHANGES_MS = 1500;

// One spare row: the vendor driver can write one row past the frame.
uint8_t imageBuf[(EPD_W * EPD_H) / 8 + EPD_W / 8];
const font::Canvas canvas{imageBuf, EPD_W, EPD_H};

wavetest::Result results[wavetest::SETTING_COUNT];
int setting = 0;

void runSetting(int i) {
  const uint8_t frames = wavetest::FRAME_SETTINGS[i];
  char label[64];

  snprintf(label, sizeof(label), "DU %u frames  -  clean start (full refresh)", frames);
  wavetest::renderPage(canvas, 0, label);
  EPD_Display(imageBuf);
  delay(PAUSE_BETWEEN_CHANGES_MS);

  wavetest::Result& r = results[i];
  r.count = 0;
  for (int k = 0; k < wavetest::CHANGES_PER_SETTING; k++) {
    const bool last = k == wavetest::CHANGES_PER_SETTING - 1;
    snprintf(label, sizeof(label), "DU %u frames  -  change %d of %d%s", frames, k + 1,
             wavetest::CHANGES_PER_SETTING, last ? "  -  rotate down: next" : "");
    wavetest::renderPage(canvas, (k + 1) % 2, label);
    if (!EPD_DisplayFast(imageBuf, frames)) Serial.println("[wavetest] BUSY timeout");
    const EpdTiming& t = EPD_LastTiming();
    const uint32_t visibleUs = t.uploadUs + t.powerOnUs + t.waveformUs;
    r.visibleMs[r.count++] = (visibleUs + 500) / 1000;
    Serial.printf("[wavetest] frames=%u change=%d visible=%lu ms (upload %lu, power-on %lu, waveform %lu us)\n",
                  frames, k + 1, static_cast<unsigned long>(visibleUs / 1000), static_cast<unsigned long>(t.uploadUs),
                  static_cast<unsigned long>(t.powerOnUs), static_cast<unsigned long>(t.waveformUs));
    if (!last) delay(PAUSE_BETWEEN_CHANGES_MS);
  }
}

void showSummary() {
  wavetest::renderSummary(canvas, results);
  EPD_Display(imageBuf);
  for (int i = 0; i < wavetest::SETTING_COUNT; i++) {
    uint32_t sum = 0;
    for (int k = 0; k < results[i].count; k++) sum += results[i].visibleMs[k];
    if (results[i].count) {
      Serial.printf("[wavetest] summary frames=%u avg=%lu ms\n", wavetest::FRAME_SETTINGS[i],
                    static_cast<unsigned long>(sum / results[i].count));
    }
  }
}

Button waitForTurn() {
  Button b;
  while (!buttonsNext(b, portMAX_DELAY) || (b != Button::Down && b != Button::Up)) {
  }
  while (buttonsNext(b, 0)) {
  }  // drop presses queued during the run
  return b;
}

}  // namespace

void setup() {
  pinMode(PIN_PWR_MAIN, OUTPUT);
  digitalWrite(PIN_PWR_MAIN, HIGH);
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[wavetest] starting");

  // Panel power rail first: the panel does not respond until it is up.
  pinMode(PIN_PWR_PANEL, OUTPUT);
  digitalWrite(PIN_PWR_PANEL, HIGH);
  delay(10);
  EPD_GPIOInit();
  EPD_RESET();
  EPD_Init();

  if (!buttonsBegin()) Serial.println("[wavetest] button task failed to start");
}

void loop() {
  if (setting < wavetest::SETTING_COUNT) {
    runSetting(setting);
    if (waitForTurn() == Button::Down) setting++;  // Up repeats this setting
    if (setting == wavetest::SETTING_COUNT) showSummary();
    return;
  }
  if (waitForTurn() == Button::Down) setting = 0;  // run the whole test again
}
