#pragma once
#include <string>
#include <cstddef>
#include <cstdint>
namespace lunaredu::lunaros {
// Loads the selected managed font once on the display owner. Font/cache memory
// remains owned by Resource for the renderer lifetime.
bool load_managed_font(const uint8_t *&data,size_t &bytes,uint8_t *&cache,
                       size_t &cache_bytes,std::string &error);
// Only the initial display owner may discard a font rejected before use.
void discard_managed_font();
}
