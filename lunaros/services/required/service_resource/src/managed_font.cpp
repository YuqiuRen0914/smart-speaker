#include "lunaros_resource_fonts.h"
#include "lunaros_resource_managed_assets.h"
#include <esp_heap_caps.h>
namespace lunaredu::lunaros {
namespace {
uint8_t *font_data=nullptr,*glyph_cache=nullptr;
size_t font_bytes=0,glyph_cache_bytes=0;
}
bool load_managed_font(const uint8_t *&data,size_t &bytes,uint8_t *&cache,
                       size_t &cache_bytes,std::string &error) {
  if (!font_data) {
    font_data=static_cast<uint8_t *>(load_managed_asset("fonts",font_bytes,error));
    if (!font_data) return false;
    glyph_cache=static_cast<uint8_t *>(heap_caps_malloc(cache_bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    glyph_cache_bytes=glyph_cache?cache_bytes:0;
  }
  data=font_data;bytes=font_bytes;cache=glyph_cache;cache_bytes=glyph_cache_bytes;
  error.clear();return true;
}
void discard_managed_font() {
  if(font_data)heap_caps_free(font_data);
  if(glyph_cache)heap_caps_free(glyph_cache);
  font_data=glyph_cache=nullptr;font_bytes=glyph_cache_bytes=0;
}
}
