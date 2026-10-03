#ifndef _EPD_H_
#define _EPD_H_

#include "EPD_SPI.h"

#define EPD_W 400
#define EPD_H 300

// Legacy SSD1683 protocol used by the green-sticker V1.2A board, confirmed on
// hardware (see docs/screen-bringup.md).
// Returns false if BUSY never released (10 s timeout).
bool EPD_ReadBusy(void);
void EPD_RESET(void);
void EPD_Init(void);
// Full GC refresh: writes both RAM planes, loads the GC LUT, updates.
// Returns false if the panel never finished.
bool EPD_Display(const uint8_t *Image);
// Differential DU refresh: no flash, much shorter, but leaves faint ghosts.
// Needs a GC refresh first (the panel's old-frame plane must be valid).
// `frames` is the DU drive length (~23 ms each); fewer is faster but lighter.
constexpr uint8_t DU_FRAMES_DEFAULT = 20;
bool EPD_DisplayFast(const uint8_t *Image, uint8_t frames = DU_FRAMES_DEFAULT);

// Stage times of the last refresh, in microseconds. The new frame is fully
// visible after upload + powerOn + waveform; sync runs after (DU only).
struct EpdTiming {
  uint32_t uploadUs;
  uint32_t powerOnUs;
  uint32_t waveformUs;
  uint32_t syncUs;
};
const EpdTiming &EPD_LastTiming(void);
// Refreshes leave the panel's high-voltage supply on so back-to-back updates
// skip its power-up; call this once the screen has been idle for a while.
void EPD_PowerOff(void);
void EPD_Sleep(void);

#endif
