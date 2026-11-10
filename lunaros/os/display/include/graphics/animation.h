#pragma once
#include <cstdint>
namespace lunaredu::lunaros::graphics {
enum class AnimationCurve:uint8_t { Linear, EaseOut, EaseInOut };
// Pure evaluation of LVGL's public paths. The caller retains clock, gesture
// arbitration and completion; no registered animation borrows caller state.
float animation_progress(uint32_t elapsed_ms,uint32_t duration_ms,AnimationCurve curve=AnimationCurve::EaseOut);
}
