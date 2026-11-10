#include "display/pipeline/frame_pacer.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "screen_geometry_internal.h"

namespace lunaredu::display {

using namespace screen_detail;

bool ScreenFramePacer::configure(unsigned target_fps) {
  if (target_fps == 0 || target_fps > 240) return false;
  period_us_ = 1000000U / target_fps;
  reset();
  return true;
}

bool ScreenFramePacer::due(uint64_t now_us) {
  if (period_us_ == 0) return false;
  if (next_us_ == 0) next_us_ = now_us;
  if (now_us < next_us_) return false;
  const uint64_t skipped = (now_us - next_us_) / period_us_;
  missed_ += skipped;
  next_us_ += (skipped + 1) * period_us_;
  return true;
}

} // namespace lunaredu::display
