#include "fonts.h"
#include "lunaros_text_raster.h"
#include <algorithm>
namespace lunaredu::lunaros::graphics {
const lv_font_t *SceneFonts::get(unsigned size) {
  const auto source=text_raster::font_source();
  if(!source.data)return &lv_font_montserrat_14;
  size=std::min(64U,std::max(1U,size));
  Entry *slot=nullptr;
  for(auto &entry:entries_){
    if(entry.font&&entry.size==size)return entry.font;
    if(!entry.font&&!slot)slot=&entry;
  }
  if(!slot)return nullptr;
  // At most eight small-font caches, two glyphs each. Larger glyphs use the
  // official uncached path, keeping the existing 128 KiB pool bounded.
  const unsigned cache=size<=24&&cached_fonts_<8?2:0;
  slot->font=lv_tiny_ttf_create_data_ex(source.data,source.size,size,LV_FONT_KERNING_NONE,cache);
  if(slot->font){slot->size=size;if(cache)++cached_fonts_;}
  return slot->font;
}
void SceneFonts::clear(){
  for(auto &entry:entries_){if(entry.font)lv_tiny_ttf_destroy(entry.font);entry={};}
  cached_fonts_=0;
}
uint32_t SceneFonts::generation() const{return text_raster::font_generation();}
} // namespace lunaredu::lunaros::graphics
