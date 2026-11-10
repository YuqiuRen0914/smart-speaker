#pragma once
#include <cstddef>
#include "lunaros_ui_metrics.h"
namespace lunaredu::desktop {
namespace content_layout_detail {
struct Geometry {int top,height,rows,pitch,row,card,center;};
constexpr Geometry compile(bool hidden) {
  namespace m=lunaros::graphics::metrics;
  const int top=m::inset+(hidden?0:m::status_height),bottom=m::side-m::inset;
  const int height=bottom-top;
  const int rows=m::native?std::clamp((height+m::gap)/(m::row_min+m::gap),1,8):3;
  const int pitch=(height+m::gap)/rows;
  return {top,height,rows,pitch,pitch-m::gap,(height-m::gap)/2,(top+bottom)/2};
}
inline constexpr Geometry variants[]{compile(false),compile(true)};
}
// Geometry follows the preference, never the temporary overlay visibility.
// The Home gesture is an overlay too, so it reserves no content space.
struct ContentLayout {
  static constexpr int inset=lunaros::graphics::metrics::inset,
    width=lunaros::graphics::metrics::side-inset*2,gap=lunaros::graphics::metrics::gap,
    bottom=lunaros::graphics::metrics::side-inset;
  int top;
  explicit constexpr ContentLayout(bool hide_status):top(content_layout_detail::variants[hide_status].top),variant_(hide_status){}
  constexpr int height() const { return geometry().height; }
  constexpr int rows() const { return geometry().rows; }
  constexpr int row_pitch() const { return geometry().pitch; }
  constexpr int row_height() const { return geometry().row; }
  constexpr int card_height() const { return geometry().card; }
  constexpr int card_y(size_t index) const { return top+int(index/2)*(card_height()+gap); }
  constexpr int card_icon_y(size_t index) const { return card_y(index)+(card_height()-lunaros::graphics::metrics::px(69))/2; }
  constexpr int center_y() const { return geometry().center; }
 private:
  bool variant_;
  constexpr const content_layout_detail::Geometry &geometry() const { return content_layout_detail::variants[variant_]; }
};
}
