# How the CrowPanel 4.2" Screen Got Working

Elecrow CrowPanel ESP32 4.2" HMI, green-sticker PCBA V1.2A (ESP32-S3-N8R8,
400x300 "SSD1683" panel). The working implementation is `src/vendor/EPD.cpp`.

## The two things that matter

### 1. Use the legacy command set

The green-sticker panel only answers the older Good Display/Waveshare-style
protocol from Elecrow's green-sticker examples:

```
reset (RST high 10 ms -> low 100 ms -> high 100 ms)
0x00 3F 4D            panel setting
0x01 03 10 3F 3F 03   power setting
0x06 96 96 29         booster soft start
0x30 09               PLL
0x61 01 90 01 2C      resolution 400x300
0x82 05               VCOM DC
0x50 97, 0x60 22, 0xE3 88
0x50 D7
0x10 <15000 bytes>    "old" RAM plane (same frame; avoids ghosting)
0x13 <15000 bytes>    "new" RAM plane
0x20..0x24 GC LUT     42 bytes each
0x17 A5               update
wait BUSY             (see below)
0x07 A5               deep sleep
```

The "modern" SSD16xx sequence (`0x12` soft reset, `0x11`/`0x44`/`0x45`,
`0x24` RAM, `0x22`+`0x20` update) gets no response: BUSY never releases.
That is what Elecrow's `factory_sourcecode` EPD driver and `zinggjm/GxEPD2`
both send, so neither drives this panel. Elecrow's green-sticker `EPD.cpp` mixes both styles. Only its
`EPD_Init()` + `EPD_Display_Fast()` + `EPD_Update()` path is the legacy one.

### 2. BUSY is active-LOW

Measured on hardware: BUSY idles **HIGH**, drops **LOW** as soon as `0x17 A5`
is sent, and returns HIGH when the refresh finishes (~1.2 s for a GC refresh,
~1.9 s total including reset and the bit-banged RAM writes).

The vendor `EPD_ReadBusy()` waits for LOW, so it returns immediately. If
deep sleep (`0x07 A5`) follows straight away, it aborts the refresh and
**the panel keeps showing its old image with no error**. Vendor sketches
only work because they add `delay(500)` or have other slow code before
sleeping. The fix is to wait *while* BUSY is LOW:

```cpp
while (digitalRead(BUSY) == LOW) { /* with a timeout */ }
```

## Pins and power

| Signal | GPIO |
|---|---|
| SCK / MOSI (bit-banged) | 12 / 11 |
| CS / DC / RST / BUSY | 45 / 46 / 47 / 48 |
| Panel power enable (drive HIGH first) | 7 |
| Main power rail | 41 |
| SD power / SCK / MISO / MOSI / CS | 42 / 39 / 13 / 40 / 10 |
| Buttons MENU / EXIT / rotary up / down / press (active-low) | 2 / 1 / 6 / 4 / 5 |

## Symptoms and causes

| Symptom | Cause |
|---|---|
| Old image stays, log shows BUSY timeout | Modern command set sent |
| Old image stays, refresh "done" in < 1 s | Sleep sent before BUSY went HIGH (polarity inverted) |
| Stuck/garbled after drawing an image | Vendor `EPD_ShowPicture()` writes one row past the buffer; pad the buffer by one row |

Keep in mind that e-paper holds its last image with no power, so "old image
still showing" never tells you whether the firmware is running. Check the
serial log instead (115200 baud on the USB-C CH340 bridge).
