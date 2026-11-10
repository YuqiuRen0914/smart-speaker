#pragma once
#include <cstdint>

namespace lunaredu::desktop {

// Logical display coordinates, supplied by the hardware owner after applying
// the panel transform. Invalid/stale reads cancel a contact instead of acting
// like a finger release.
struct TouchInput {
  bool available = false;
  bool valid = false;
  bool pressed = false;
  uint8_t contact_count = 0;
  uint16_t x = 0;
  uint16_t y = 0;
  uint32_t sampled_at_ms = 0;
};

} // namespace lunaredu::desktop
