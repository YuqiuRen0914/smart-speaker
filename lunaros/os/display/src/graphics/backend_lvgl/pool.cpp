#include "graphics_pool.h"
#include <cstdlib>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif
namespace { void *pool = nullptr; }
extern "C" void *lunaros_graphics_allocate_buffer(size_t bytes) {
#ifdef ESP_PLATFORM
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::malloc(bytes);
#endif
}
extern "C" void lunaros_graphics_free_buffer(void *buffer) { std::free(buffer); }
extern "C" void *lunaros_graphics_allocate_pool(size_t bytes) {
  if (!pool) {
#ifdef ESP_PLATFORM
    pool = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    pool = std::malloc(bytes);
#endif
  }
  return pool;
}
