#pragma once
#include "desktop/ui/touch_input.h"
#include <array>

namespace lunaredu::desktop {
// A borrowed observation of logical controls. The product assigns physical
// buttons once; Apps decide how these identities affect their own controls.
// Reading this value never acknowledges encoder deltas or acquires hardware.
struct LogicalInput {
  bool fresh = false;
  std::array<int16_t, 4> axes{};
  bool primary = false, secondary = false, left_stick = false, right_stick = false;
  bool any_button = false;
  TouchInput touch{};
};
}
