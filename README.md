# CrowPanel Reader

E-reader firmware for the **Elecrow CrowPanel ESP32 4.2" E-Paper HMI** (400×300, SSD1683 panel, ESP32-S3-WROOM-1-N8R8). Reads EPUB and plain-text books from an SD card, with proper book typography, chapter navigation and Wi-Fi uploads from your phone.

> Built and tested on the **green-sticker (PCBA V1.2A)** board revision. Other revisions use a different panel protocol; see [docs/screen-bringup.md](docs/screen-bringup.md).

## Features

- **EPUB and TXT books.** EPUB chapters are read in spine order, keeping paragraphs, headings, bold and italic. Each chapter starts on a new page.
- **Book typography.** Noto Serif with kerning, justified lines and first-line indents. Noto Sans for menus.
- **Chapter contents.** Jump to any chapter from the book's table of contents (EPUB 3 nav or EPUB 2 NCX). Books without one fall back to their headings.
- **Wi-Fi upload.** The reader starts its own hotspot. Scan a QR code to join, scan another to open an upload page, and send books from your phone.
- **Home screen.** Any JPEG named `bg.jpg` on the SD card, scaled to fit and dithered for e-paper.
- **Reading progress** is saved per book. Every page turn is a full refresh.

## Controls

The board has MENU and EXIT keys and a three-way dial (rotate up, rotate down, press).

| Screen | Dial up / down | Dial press | MENU | EXIT |
|---|---|---|---|---|
| Home | – | – | Library | – |
| Library | Move selection | Open book | Rescan SD | Home |
| Reading | Previous / next page | Contents | Library | Home |
| Contents | Move selection | Jump to chapter | Library | Back to page |
| Wi-Fi transfer | – | – | – | Done (Wi-Fi off) |

In the library and contents screens, the key bar at the bottom highlights the last button pressed and the status line says what it did.

## SD card

```
SD card/
├── bg.jpg              home screen image (optional, baseline JPEG)
├── books/              .epub and .txt books (Wi-Fi uploads go here)
│   └── ...
├── Some Book.epub      books in the root folder work too
└── .reader/            created automatically: extracted EPUB text cache
```

Only the root folder and `/books` are scanned. Format the card as FAT32.

## Adding books over Wi-Fi

1. In the library, choose **Add books over Wi-Fi** (the top row).
2. Scan the left QR code to join the `CrowPanel-XXXX` network. The password is new every session.
3. Scan the right QR code, or browse to `http://192.168.4.1`.
4. Pick `.epub` or `.txt` files and upload them. The page also lists and deletes books.
5. Press **EXIT** on the reader. Wi-Fi turns off and the library refreshes.

Your phone may warn that the network has no internet. That's expected.

## Build and flash

Needs [PlatformIO](https://platformio.org/). The board's USB-C port is a CH340 serial bridge.

```sh
pio run -t upload            # build and flash
pio device monitor           # serial log, 115200 baud
```

`src/book.txt` is the built-in book, embedded in flash. Replace it with any UTF-8 text before building.

## Project layout

| Path | What |
|---|---|
| `src/main.cpp` | Screens, input handling, rendering |
| `src/vendor/EPD.*` | Panel driver (legacy SSD1683 sequence, active-low BUSY) |
| `src/Epub.*`, `src/HtmlText.*` | ZIP/OPF/NCX/nav parsing, XHTML to text |
| `src/TextLayout.h`, `src/Font.*` | Pagination and proportional font rendering |
| `src/Book.*`, `src/Library.*`, `src/Toc.h` | Book loading, caching, library scan, chapters |
| `src/Transfer.*`, `src/WebPage.h` | Wi-Fi hotspot, web server, upload page |
| `src/HomeImage.*`, `src/ImageDither.h` | `bg.jpg` decoding and dithering |
| `src/fonts/` | Generated 1-bit fonts |
| `tools/fontconvert.py` | Font generator |
| `tools/layout_test.cpp` | Host test: pagination loses no text, draws nothing off-page |

### Regenerating fonts

```sh
pip install freetype-py fonttools
python tools/fontconvert.py serif9_regular 9 NotoSerif-Regular.ttf --pnum --mono-threshold 4 > src/fonts/serif9_regular.h
```

Sizes are at 150 DPI. `--mono-threshold 4` keeps small text crisp on this panel.

### Host test

```sh
c++ -std=c++17 -Isrc -Iinclude tools/layout_test.cpp src/Font.cpp -o /tmp/layout_test
/tmp/layout_test src/book.txt
```

## Credits and licences

- `include/EpdFontData.h` and `tools/fontconvert.py` come from [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) (MIT, see `LICENSES/MIT-CrossPoint.txt`), originally from [epdiy](https://github.com/vroland/epdiy).
- Fonts in `src/fonts/` are generated from Noto Serif and Noto Sans (SIL Open Font License, see `LICENSES/OFL-Noto.txt`).
- `lib/uzlib` is [uzlib](https://github.com/pfalcon/uzlib) (zlib licence).
- [JPEGDEC](https://github.com/bitbank2/JPEGDEC) (Apache 2.0) and [QRCode](https://github.com/ricmoo/QRCode) (MIT) are fetched by PlatformIO.
- `src/vendor/EPD_SPI.*` and the panel register sequence in `src/vendor/EPD.cpp` are based on Elecrow's CrowPanel example code.
