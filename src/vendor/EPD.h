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
void EPD_Sleep(void);

#endif
