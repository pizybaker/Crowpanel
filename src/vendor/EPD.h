#ifndef _EPD_H_
#define _EPD_H_

#include "EPD_SPI.h"

#define EPD_W 400
#define EPD_H 300

// Legacy SSD1683 protocol used by the green-sticker V1.2A board (same sequence
// as freeink-sdk Ssd1683LegacyDriver, confirmed on hardware).
void EPD_ReadBusy(void);
void EPD_RESET(void);
void EPD_Init(void);
// Full GC refresh: writes both RAM planes, loads the GC LUT, updates.
void EPD_Display(const uint8_t *Image);
void EPD_Sleep(void);

#endif
