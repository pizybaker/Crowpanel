#include "Wallpaper.h"

#include <Arduino.h>
#include <SD.h>
#include <strings.h>

namespace wallpaper {

namespace {
constexpr const char* CHOICE_DIR = "/.reader";
constexpr const char* CHOICE_PATH = "/.reader/wallpaper.txt";
}  // namespace

bool isValidPath(const char* path) {
  if (strcmp(path, DEFAULT_PATH) == 0) return true;
  const size_t dirLen = strlen(DIR);
  if (strncmp(path, DIR, dirLen) != 0 || path[dirLen] != '/') return false;
  const char* name = path + dirLen + 1;
  const size_t n = strlen(name);
  if (!*name || *name == '.' || strchr(name, '/') || strstr(name, "..")) return false;
  return (n > 4 && strcasecmp(name + n - 4, ".jpg") == 0) || (n > 5 && strcasecmp(name + n - 5, ".jpeg") == 0);
}

void current(char* out, size_t cap) {
  snprintf(out, cap, "%s", DEFAULT_PATH);
  File f = SD.open(CHOICE_PATH, FILE_READ);
  if (!f) return;
  char saved[160];
  const int n = f.read(reinterpret_cast<uint8_t*>(saved), sizeof(saved) - 1);
  if (n <= 0) return;
  saved[n] = '\0';
  if (isValidPath(saved) && SD.exists(saved)) snprintf(out, cap, "%s", saved);
}

bool select(const char* path) {
  if (!isValidPath(path) || !SD.exists(path)) return false;
  SD.mkdir(CHOICE_DIR);
  File f = SD.open(CHOICE_PATH, FILE_WRITE);
  return f && f.write(reinterpret_cast<const uint8_t*>(path), strlen(path)) == strlen(path);
}

}  // namespace wallpaper
