#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
namespace runtime_test { inline int buffers = 0; }
inline void *heap_caps_malloc(size_t size, int) {
  void *p = std::malloc(size); if (p) ++runtime_test::buffers; return p;
}
inline void heap_caps_free(void *p) {
  if (p) --runtime_test::buffers; std::free(p);
}
