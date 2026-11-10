#pragma once
#include <cstdint>

namespace lunaredu::lunaros::assets {
enum class MaskFormat : uint8_t { Mono1, Alpha8 };
// Borrowed immutable coverage. Mono1 is row-major/MSB first; Alpha8 is
// coverage rather than a precomposited color. stride is measured in bytes.
struct MaskView {
  const uint8_t *data = nullptr;
  uint16_t width = 0, height = 0, stride = 0;
  MaskFormat format = MaskFormat::Mono1;
};
struct MaskBounds { int x = 0, y = 0, width = 0, height = 0; };
} // namespace lunaredu::lunaros::assets
