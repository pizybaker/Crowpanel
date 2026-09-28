#pragma once

#include <cstddef>
#include <cstdint>

struct BookEntry {
  char path[160];  // SD path; empty for the built-in book
  char title[96];  // UTF-8
  uint32_t size;
  bool builtIn;
  bool epub;
};

static constexpr int MAX_BOOKS = 100;

bool libraryMountSd();

// Fills `out` with every .txt/.epub in "/" and "/books" (sorted by title),
// then the built-in book last. Returns the number of entries (always >= 1).
int libraryScan(BookEntry* out, int max, bool sdOk);

// Built-in book embedded in flash via board_build.embed_txtfiles.
const char* builtinBookText();
size_t builtinBookLen();
