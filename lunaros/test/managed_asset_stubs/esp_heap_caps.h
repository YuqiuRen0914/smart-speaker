#pragma once
#include <cstdlib>
#include <cassert>
inline constexpr unsigned MALLOC_CAP_SPIRAM = 1;
inline constexpr unsigned MALLOC_CAP_8BIT = 2;
inline bool asset_allocation_fail = false;
inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned caps) {
  assert(alignment == 16 && caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return asset_allocation_fail ? nullptr : std::malloc(size);
}
inline void heap_caps_free(void *data) { std::free(data); }
