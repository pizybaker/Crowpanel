#pragma once

#include <cstddef>

// Home-screen wallpapers: /bg.jpg plus any JPEG in /wallpapers. The chosen one
// is remembered in /.reader/wallpaper.txt on the SD card.
namespace wallpaper {

constexpr const char* DIR = "/wallpapers";
constexpr const char* DEFAULT_PATH = "/bg.jpg";

// "/bg.jpg" or "/wallpapers/<name>.jpg|.jpeg" (no subfolders, no "..").
bool isValidPath(const char* path);

// The saved choice if that file still exists, else /bg.jpg.
void current(char* out, size_t cap);

bool select(const char* path);

}  // namespace wallpaper
