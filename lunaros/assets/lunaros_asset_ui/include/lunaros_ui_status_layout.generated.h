// Generated from lunaros/assets/ui/layout.json.
#pragma once
#include "lunaros_ui_metrics.h"
namespace lunaredu::desktop::status_layout {
struct Rect { int x,y,width,height; };
namespace metrics=lunaros::graphics::metrics;
inline constexpr int width = metrics::native?metrics::side:240;
inline constexpr int height = metrics::native?metrics::status_height:28;
inline constexpr int idleMs = metrics::native?5000:5000;
inline constexpr int padding = metrics::native?metrics::inset:8;
inline constexpr int textSize = metrics::native?metrics::font(12):16;
inline constexpr int titleRight = metrics::native?metrics::side-metrics::inset-(240-8-108)*metrics::status_height/28:108;
inline constexpr Rect wifi = metrics::native?Rect{metrics::side-metrics::inset-(240-8-112)*metrics::status_height/28,7*metrics::status_height/28,18*metrics::status_height/28,14*metrics::status_height/28}:Rect{112,7,18,14};
inline constexpr Rect platform = metrics::native?Rect{metrics::side-metrics::inset-(240-8-160)*metrics::status_height/28,6*metrics::status_height/28,16*metrics::status_height/28,16*metrics::status_height/28}:Rect{160,6,16,16};
inline constexpr Rect peer = metrics::native?Rect{metrics::side-metrics::inset-(240-8-182)*metrics::status_height/28,6*metrics::status_height/28,16*metrics::status_height/28,16*metrics::status_height/28}:Rect{182,6,16,16};
inline constexpr Rect battery = metrics::native?Rect{metrics::side-metrics::inset-(240-8-204)*metrics::status_height/28,8*metrics::status_height/28,28*metrics::status_height/28,12*metrics::status_height/28}:Rect{204,8,28,12};
inline constexpr Rect bluetooth = metrics::native?Rect{metrics::side-metrics::inset-(240-8-136)*metrics::status_height/28,4*metrics::status_height/28,16*metrics::status_height/28,20*metrics::status_height/28}:Rect{136,4,16,20};
inline constexpr Rect batteryFill = metrics::native?Rect{metrics::side-metrics::inset-(240-8-206)*metrics::status_height/28,11*metrics::status_height/28,21*metrics::status_height/28,6*metrics::status_height/28}:Rect{206,11,21,6};
inline constexpr Rect batterySymbol = metrics::native?Rect{metrics::side-metrics::inset-(240-8-214)*metrics::status_height/28,9*metrics::status_height/28,6*metrics::status_height/28,10*metrics::status_height/28}:Rect{214,9,6,10};
}
