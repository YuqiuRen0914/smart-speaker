// Generated from lunaros/assets/ui/layout.json.
#pragma once
#include "lunaros_ui_metrics.h"
namespace lunaredu::desktop::honeycomb_layout {
inline constexpr int spacing = lunaros::graphics::metrics::native?std::max(lunaros::graphics::metrics::min_touch,lunaros::graphics::metrics::launcher_icon)+lunaros::graphics::metrics::gap*2:68;
inline constexpr int coreRadius = lunaros::graphics::metrics::native?std::max(lunaros::graphics::metrics::min_touch,lunaros::graphics::metrics::launcher_icon)+lunaros::graphics::metrics::gap*2:68;
inline constexpr int falloffSpan = lunaros::graphics::metrics::px(88);
inline constexpr float minimumScale = 0.28f;
inline constexpr float spacingCompression = 0.8f;
inline constexpr int detailFalloffSpan = lunaros::graphics::metrics::px(44);
inline constexpr int sizeWithLabels = lunaros::graphics::metrics::native?std::max(lunaros::graphics::metrics::min_touch,lunaros::graphics::metrics::launcher_icon):60;
inline constexpr int sizeWithoutLabels = lunaros::graphics::metrics::native?std::max(lunaros::graphics::metrics::min_touch,lunaros::graphics::metrics::launcher_icon)+lunaros::graphics::metrics::gap:66;
inline constexpr int minimumTapSize = lunaros::graphics::metrics::min_touch;
}
namespace lunaredu::desktop::home_gesture_layout {
inline constexpr int screenSize = lunaros::graphics::metrics::side;
inline constexpr int edgeHeight = lunaros::graphics::metrics::gesture_edge;
inline constexpr int travel = lunaros::graphics::metrics::gesture_travel;
inline constexpr int directionRatio = 2;
inline constexpr int maximumDurationMs = 1200;
inline constexpr int revealTravel = lunaros::graphics::metrics::px(12);
inline constexpr int tapWidth = lunaros::graphics::metrics::px(72);
inline constexpr int barWidth = lunaros::graphics::metrics::px(48);
inline constexpr int barHeight = lunaros::graphics::metrics::px(4);
inline constexpr int barBottom = lunaros::graphics::metrics::px(2);
inline constexpr int revealDurationMs = 120;
inline constexpr int transitionDurationMs = 260;
inline constexpr int reboundDurationMs = 220;
inline constexpr int idleDurationMs = 3000;
inline constexpr int dragSpan = lunaros::graphics::metrics::px(144);
inline constexpr int dragDeadzone = lunaros::graphics::metrics::gesture_slop;
inline constexpr float dragScaleReduction = 0.5f;
inline constexpr int dragRadius = lunaros::graphics::metrics::px(16);
inline constexpr float returnFadeStart = 0.55f;
}
