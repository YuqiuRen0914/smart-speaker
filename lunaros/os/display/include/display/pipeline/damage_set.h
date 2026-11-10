#pragma once
#include "display/backend.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::display {
inline constexpr size_t kScreenBatchMaxRegions = 4;
// A bounded collection of panel or redraw damage. Touching rectangles are
// merged; excess complexity falls back to a complete frame.
class ScreenDamageSet {
 public:
  bool configure(int width, int height, std::string &error);
  void clear();
  void fill();
  bool add(ScreenRegion region);
  bool add_costed(ScreenRegion region, unsigned window_pixels);
  bool intersects(ScreenRegion region) const;
  const ScreenRegion *regions() const { return regions_.data(); }
  size_t count() const { return count_; }
  bool full() const { return full_; }

 private:
  std::array<ScreenRegion, kScreenBatchMaxRegions> regions_{};
  int width_ = 0;
  int height_ = 0;
  size_t count_ = 0;
  bool full_ = false;
};

}
