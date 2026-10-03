#include "EPD.h"

#include <cstring>

// Legacy (Good Display/Waveshare-style) SSD1683 command set from Elecrow's
// green-sticker examples. This panel does not answer the "modern" SSD16xx
// sequence (0x12 / 0x24 / 0x22+0x20): BUSY never releases.

namespace {

constexpr uint8_t LUT_GC[5][42] = {
    {0x01, 0x14, 0x0A, 0x14, 0x00, 0x01, 0x01},
    {0x01, 0x54, 0x0A, 0x94, 0x00, 0x01, 0x01},
    {0x01, 0x54, 0x0A, 0x94, 0x00, 0x01, 0x01},
    {0x01, 0x94, 0x0A, 0x54, 0x00, 0x01, 0x01},
    {0x01, 0x94, 0x0A, 0x54, 0x00, 0x01, 0x01},
};

// DU: one short drive phase, no flash. Leaves faint ghosts that build up, so
// callers mix in a GC refresh every few updates. Each phase byte is
// [drive level:2][frame count:6]; EPD_DisplayFast() swaps in its frame count.
constexpr uint8_t LUT_DU[5][42] = {
    {0x01, 0x14, 0x00, 0x00, 0x00, 0x01},
    {0x01, 0x14, 0x00, 0x00, 0x00, 0x01},
    {0x01, 0x94, 0x00, 0x00, 0x00, 0x01},
    {0x01, 0x54, 0x00, 0x00, 0x00, 0x01},
    {0x01, 0x14, 0x00, 0x00, 0x00, 0x01},
};

void loadLut(const uint8_t (&lut)[5][42]) {
  for (int r = 0; r < 5; r++) {
    EPD_WR_REG(0x20 + r);
    EPD_WR_DATA(lut[r], 42);
  }
}

// The high-voltage supply stays on between refreshes (EPD_PowerOff() after
// idle): the 0x17 A5 auto sequence powers it on and off around every refresh,
// a fixed ~160 ms per update.
bool powered = false;
EpdTiming lastTiming = {};

bool refreshPanel(uint32_t &ponUs) {
  const uint32_t t0 = micros();
  if (!powered) {
    EPD_WR_REG(0x04);  // power on
    if (!EPD_ReadBusy()) return false;
    powered = true;
  }
  ponUs = micros() - t0;
  EPD_WR_REG(0x12);  // display refresh
  return EPD_ReadBusy();
}

void writePlane(uint8_t reg, const uint8_t *image) {
  EPD_WR_REG(reg);
  EPD_WR_DATA(image, (EPD_W / 8) * EPD_H);
}

}  // namespace

// BUSY is active-LOW on this panel (measured: idle HIGH, LOW for ~1.2 s during
// a GC refresh). Sleeping before it returns HIGH aborts the refresh.
// Bounded so a stuck panel logs instead of hanging the firmware.
bool EPD_ReadBusy(void) {
  uint32_t start = millis();
  delay(1);
  while (EPD_ReadBUSY == LOW) {
    if (millis() - start > 10000) {
      Serial.println("[epd] BUSY timeout");
      return false;
    }
    delay(1);
  }
  return true;
}

void EPD_RESET(void) {
  powered = false;
  EPD_RES_Set();
  delay(10);
  EPD_RES_Clr();
  delay(100);
  EPD_RES_Set();
  delay(100);
}

void EPD_Init(void) {
  EPD_WR_REG(0x00);  // panel setting
  EPD_WR_DATA8(0x3F);
  EPD_WR_DATA8(0x4D);

  EPD_WR_REG(0x01);  // power setting
  EPD_WR_DATA8(0x03);
  EPD_WR_DATA8(0x10);
  EPD_WR_DATA8(0x3F);
  EPD_WR_DATA8(0x3F);
  EPD_WR_DATA8(0x03);

  EPD_WR_REG(0x06);  // booster soft start
  EPD_WR_DATA8(0x96);
  EPD_WR_DATA8(0x96);
  EPD_WR_DATA8(0x29);

  EPD_WR_REG(0x30);  // PLL
  EPD_WR_DATA8(0x09);

  EPD_WR_REG(0x61);  // resolution 400 x 300
  EPD_WR_DATA8(0x01);
  EPD_WR_DATA8(0x90);
  EPD_WR_DATA8(0x01);
  EPD_WR_DATA8(0x2C);

  EPD_WR_REG(0x82);  // VCOM DC
  EPD_WR_DATA8(0x05);

  EPD_WR_REG(0x50);  // VCOM / data interval
  EPD_WR_DATA8(0x97);

  EPD_WR_REG(0x60);  // TCON
  EPD_WR_DATA8(0x22);

  EPD_WR_REG(0xE3);  // power saving
  EPD_WR_DATA8(0x88);
}

// Writing the "old" plane (0x10) as well as the "new" one (0x13) makes the GC
// waveform clear against a known frame, so page turns don't accumulate ghosts.
bool EPD_Display(const uint8_t *Image) {
  const uint32_t t0 = micros();
  EPD_WR_REG(0x50);
  EPD_WR_DATA8(0xD7);
  writePlane(0x10, Image);
  writePlane(0x13, Image);
  loadLut(LUT_GC);
  const uint32_t t1 = micros();
  uint32_t pon = 0;
  const bool ok = refreshPanel(pon);
  lastTiming = {t1 - t0, pon, micros() - t1 - pon, 0};
  Serial.printf("[epd] GC upload %lu us, power-on %lu us, waveform %lu us\n", t1 - t0, pon, micros() - t1 - pon);
  return ok;
}

// The DU waveform drives each pixel by its (old 0x10, new 0x13) pair, so the
// old plane must hold what is on the panel. Both refresh paths leave 0x10
// equal to the frame just shown: GC writes it up front, DU syncs it after
// BUSY releases, while the reader is looking at the page.
bool EPD_DisplayFast(const uint8_t *Image, uint8_t frames) {
  static uint8_t lut[5][42];
  memcpy(lut, LUT_DU, sizeof(lut));
  for (auto &row : lut) row[1] = (row[1] & 0xC0) | (frames & 0x3F);

  EPD_WR_REG(0x50);
  EPD_WR_DATA8(0xD7);
  const uint32_t t0 = micros();
  writePlane(0x13, Image);
  loadLut(lut);
  const uint32_t t1 = micros();
  uint32_t pon = 0;
  const bool ok = refreshPanel(pon);
  const uint32_t t2 = micros();
  writePlane(0x10, Image);
  lastTiming = {t1 - t0, pon, t2 - t1 - pon, micros() - t2};
  Serial.printf("[epd] DU%u upload %lu us, power-on %lu us, waveform %lu us, sync %lu us\n", frames,
                lastTiming.uploadUs, pon, lastTiming.waveformUs, lastTiming.syncUs);
  return ok;
}

const EpdTiming &EPD_LastTiming(void) { return lastTiming; }

void EPD_PowerOff(void) {
  if (!powered) return;
  const uint32_t t0 = micros();
  EPD_WR_REG(0x02);  // power off
  EPD_ReadBusy();
  powered = false;
  Serial.printf("[epd] power-off %lu us\n", micros() - t0);
}

void EPD_Sleep(void) {
  EPD_PowerOff();
  EPD_WR_REG(0x07);  // deep sleep
  EPD_WR_DATA8(0xA5);
  delay(50);
}
