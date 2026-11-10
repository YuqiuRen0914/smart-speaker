#pragma once
#include <cstdint>
#include "lunaros_ui_typography.generated.h"
#include "lunaros_ui_design.generated.h"

namespace lunaredu::lunaros::graphics::theme {
// Native page roles. Applications select a role rather than inventing colors.
using design::background; using design::surface; using design::raised;
using design::text; using design::secondary; using design::accent;
using design::success; using design::warning; using design::danger;
using design::radius;
using design::appbackground; using design::appsurface; using design::appraised;
using design::appborder; using design::appsecondary; using design::appaccent;
using design::mint; using design::violet; using design::amber; using design::coral;
enum class Tone { Normal, Accent, Success, Warning, Danger };
inline constexpr uint16_t ink(Tone tone) {
  switch(tone){
    case Tone::Accent:return accent;
    case Tone::Success:return success;
    case Tone::Warning:return warning;
    case Tone::Danger:return danger;
    default:return text;
  }
}
} // namespace lunaredu::lunaros::graphics::theme
