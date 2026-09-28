#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Library.h"
#include "TextLayout.h"
#include "Toc.h"

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

  // Chapters in reading order (from the EPUB TOC, else detected headings).
  const std::vector<TocEntry>& chapters() const { return toc; }
  // Index of the chapter containing `page`, or -1 before the first chapter.
  int chapterForPage(int page) const;

  template <typename DrawFn>
  void layoutPage(int page, DrawFn fn) const {
    text::layoutPage(buf, len, starts[page], *fonts, geom, fn);
  }

 private:
  bool loadTxt(const BookEntry& entry);
  bool loadEpub(const BookEntry& entry);
  void paginate();
  void finishToc(bool plainText);

  char* buf = nullptr;
  size_t len = 0;
  const text::Fonts* fonts = nullptr;
  text::Geometry geom{};
  std::vector<text::PageStart> starts;
  std::vector<TocEntry> toc;
  const char* err = "";
};

// True when opening this book will need a slow first-time EPUB extraction.
bool bookNeedsExtraction(const BookEntry& entry);
