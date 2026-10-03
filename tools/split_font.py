#!/usr/bin/env python3
"""Splits a font into a fontconvert stack that stays within 255 kerning classes.

  python tools/split_font.py Merriweather-Regular.ttf Merriweather-Regular
  -> Merriweather-Regular-Kern.ttf  Latin-1 + punctuation, kerning kept
     Merriweather-Regular-Rest.ttf  every glyph, kerning dropped

Pass both to fontconvert.py, Kern first. Fonts with very fine-grained
kerning (Merriweather has 728 classes over its full character set) overflow
the uint8 class ids of EpdFontData; kerning only the common characters keeps
them in range while every other character still renders, unkerned.
"""

import sys

from fontTools import subset
from fontTools.ttLib import TTFont

KERNED = (
    list(range(0x20, 0x7F))
    + list(range(0xA0, 0x100))
    + list(range(0x2010, 0x2060))
    + list(range(0x20A0, 0x20D0))
    + list(range(0xFB00, 0xFB07))
    + [0xFFFD]
)


def main():
    src, out = sys.argv[1], sys.argv[2]
    opts = subset.Options()
    opts.layout_features = ["*"]  # keep kern and pnum
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    font = TTFont(src)
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=KERNED)
    sub.subset(font)
    font.save(out + "-Kern.ttf")

    font = TTFont(src)
    for table in ("GPOS", "kern"):
        if table in font:
            del font[table]
    font.save(out + "-Rest.ttf")


if __name__ == "__main__":
    main()
