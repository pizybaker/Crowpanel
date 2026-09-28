#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Library.h"
#include "TextLayout.h"

class Book {
 public:
  ~Book() { close(); }

  // Loads the whole text into PSRAM, cleans it, and precomputes every page
  // start so prev/next are O(1). On failure returns false and sets error().
  bool open(const BookEntry& entry, int cols, int lines);
  void close();

  bool isOpen() const { return buf != nullptr; }
  int pageCount() const { return static_cast<int>(starts.size()); }
  uint32_t pageOffset(int page) const { return starts[page]; }
  int pageForOffset(uint32_t offset) const;
  const char* error() const { return err; }

  template <typename DrawLineFn>
  void layoutPage(int page, DrawLineFn fn) const {
    text::layoutPage(buf, len, starts[page], cols, lines, fn);
  }

 private:
  char* buf = nullptr;
  size_t len = 0;
  int cols = 0;
  int lines = 0;
  std::vector<uint32_t> starts;
  const char* err = "";
};
