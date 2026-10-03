#include "EPD.h"

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

void loadLutGc() {
  for (int r = 0; r < 5; r++) {
    EPD_WR_REG(0x20 + r);
    EPD_WR_DATA(LUT_GC[r], 42);
  }
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
  EPD_WR_REG(0x50);
  EPD_WR_DATA8(0xD7);
  writePlane(0x10, Image);
  writePlane(0x13, Image);
  loadLutGc();
  EPD_WR_REG(0x17);  // update
  EPD_WR_DATA8(0xA5);
  return EPD_ReadBusy();
}

void EPD_Sleep(void) {
  EPD_WR_REG(0x07);  // deep sleep
  EPD_WR_DATA8(0xA5);
  delay(50);
}
