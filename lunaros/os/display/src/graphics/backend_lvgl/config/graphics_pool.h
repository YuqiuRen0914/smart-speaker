#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *lunaros_graphics_allocate_pool(size_t bytes);
void *lunaros_graphics_allocate_buffer(size_t bytes);
void lunaros_graphics_free_buffer(void *buffer);
#ifdef __cplusplus
}
#endif
