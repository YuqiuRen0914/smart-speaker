#pragma once
#include <cstddef>
constexpr unsigned MALLOC_CAP_INTERNAL = 1;
constexpr unsigned MALLOC_CAP_8BIT = 2;
void *heap_caps_malloc(size_t, unsigned);
void heap_caps_free(void *);
