#pragma once
#include "lunaros_text_raster.h"
namespace lunaredu::lunaros::text_raster {
enum class TextAlignment { Left, Center, Right };
struct TextOrigin { int x, y; };
inline TextOrigin text_origin(TextBounds bounds, int x, int y, int width, int height,
                              TextAlignment alignment = TextAlignment::Left) {
  const int offset = alignment == TextAlignment::Center ? (width - bounds.width) / 2 :
      alignment == TextAlignment::Right ? width - bounds.width : 0;
  return {x + offset - bounds.left, y + (height - bounds.height) / 2 - bounds.top};
}
}
