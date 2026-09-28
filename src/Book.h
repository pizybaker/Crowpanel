#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Library.h"
#include "TextLayout.h"

class Book {
 public:
  ~Book() { close(); }

  // Loads the book text into PSRAM (EPUBs are extracted once, then cached on
  // SD under /.reader/) and precomputes every page start.
  bool open(const BookEntry& entry, const text::Fonts& fonts, const text::Geometry& geometry);
  void close();

  bool isOpen() const { return buf != nullptr; }
  int pageCount() const { return static_cast<int>(starts.size()); }
  uint32_t pageOffset(int page) const { return starts[page].offset; }
  int pageForOffset(uint32_t offset) const;
  const char* error() const { return err; }

  template <typename DrawFn>
  void layoutPage(int page, DrawFn fn) const {
    text::layoutPage(buf, len, starts[page], *fonts, geom, fn);
  }

 private:
  bool loadTxt(const BookEntry& entry);
  bool loadEpub(const BookEntry& entry);
  void paginate();

  char* buf = nullptr;
  size_t len = 0;
  const text::Fonts* fonts = nullptr;
  text::Geometry geom{};
  std::vector<text::PageStart> starts;
  const char* err = "";
};

// True when opening this book will need a slow first-time EPUB extraction.
bool bookNeedsExtraction(const BookEntry& entry);
