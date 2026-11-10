#pragma once
#include "desktop/shell/shell.h"
#include "lunaros_text_layout.h"

namespace lunaredu::desktop {
using lunaros::text_raster::TextAlignment;
using lunaros::text_raster::TextOrigin;
// Center visible glyph ink, not the font's em square or baseline. The same
// rule applies to labels, values and mixed Chinese/Latin text in a row.
inline TextOrigin text_origin(DesktopPainter painter, const char *value, int size,
    int x, int y, int width, int height, TextAlignment alignment=TextAlignment::Left) {
  const auto bounds=painter.text_bounds?painter.text_bounds(painter.context,value,size):
      DesktopPainter::TextBounds{0,0,painter.text_width?painter.text_width(painter.context,value,size):0,size};
  return lunaros::text_raster::text_origin(
      {bounds.left,bounds.top,bounds.width,bounds.height},x,y,width,height,alignment);
}
} // namespace lunaredu::desktop
