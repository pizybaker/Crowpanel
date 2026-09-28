#pragma once

#include <cstddef>
#include <cstdint>

// Random-access byte source (SD file on device, FILE* on host).
struct ByteSource {
  void* ctx;
  uint32_t size;
  bool (*readAt)(void* ctx, uint32_t offset, void* buf, size_t n);
};

struct EpubText {
  char* text = nullptr;  // bigAlloc'd, owned by caller (bigFree)
  size_t len = 0;
  char title[96] = "";
};

// Extracts every spine chapter as book text (see Markup.h). On failure returns
// false and sets *err to a short reason.
bool epubExtract(const ByteSource& src, EpubText& out, const char** err);

// Reads only the OPF title (fast; used for library listings).
bool epubTitle(const ByteSource& src, char* title, size_t cap);
