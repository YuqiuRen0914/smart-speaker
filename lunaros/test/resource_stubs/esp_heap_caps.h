#pragma once
#include <cstddef>
inline constexpr unsigned MALLOC_CAP_INTERNAL = 1;
inline size_t heap_caps_get_largest_free_block(unsigned) { return 65536; }
