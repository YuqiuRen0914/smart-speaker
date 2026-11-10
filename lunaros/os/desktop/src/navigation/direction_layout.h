#pragma once
#include <cstddef>
#include "desktop/ui/content_layout.h"
namespace lunaredu::desktop::direction_layout {
// Physical edge order matches stored quarter turns: bottom, left, top, right.
inline constexpr int width=lunaros::graphics::metrics::px(56),height=lunaros::graphics::metrics::px(38);
inline constexpr int x(size_t i){return i==1?ContentLayout::inset:i==3?lunaros::graphics::metrics::side-ContentLayout::inset-width:(lunaros::graphics::metrics::side-width)/2;}
inline constexpr int y(size_t i,bool hide_status=false){const ContentLayout layout(hide_status);return i==0?layout.bottom-height:i==2?layout.top:layout.center_y()-height/2;}
} // namespace lunaredu::desktop::direction_layout
