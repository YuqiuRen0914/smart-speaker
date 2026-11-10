#pragma once
#include "input/navigation_repeat.h"
namespace lunaredu::app {
// Logical application navigation defaults. Physical calibration remains product-owned.
inline constexpr lunaros::input::NavigationPolicy kApplicationNavigationPolicy{
    200, 420, 850, 140, 1000, 360, 210, 140};
using ApplicationNavigationRepeat = lunaros::input::NavigationRepeat<kApplicationNavigationPolicy>;
using ApplicationNavigationMerger = lunaros::input::NavigationMerger<kApplicationNavigationPolicy>;
}
