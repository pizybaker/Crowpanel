#pragma once

// Large buffers (book text, EPUB entries) go to PSRAM on the device. Plain
// malloc on the host so the text pipeline can be tested there.

#include <cstddef>
#include <cstdlib>

#ifdef ARDUINO
#include <esp_heap_caps.h>
inline void* bigAlloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
inline void* bigRealloc(void* p, size_t n) { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
inline void bigFree(void* p) { heap_caps_free(p); }
#else
inline void* bigAlloc(size_t n) { return malloc(n); }
inline void* bigRealloc(void* p, size_t n) { return realloc(p, n); }
inline void bigFree(void* p) { free(p); }
#endif
