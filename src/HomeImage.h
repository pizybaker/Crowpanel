#pragma once

#include <cstddef>
#include <cstdint>

// Decodes a baseline JPEG held in memory into a 1bpp outW x outH frame
// (cover-scaled, dithered). On failure returns false and sets *err.
bool decodeJpegToFrame(uint8_t* jpg, size_t len, uint8_t* frame, int outW, int outH, const char** err);
