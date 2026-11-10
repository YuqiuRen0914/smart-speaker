#include "desktop/ui/touch_router.h"
namespace lunaredu::desktop {
TouchEvent TouchInputRouter::update_impl(const TouchInput &input, int64_t now_us, bool pull_refresh,const Geometry &g) {
    const auto now_ms = static_cast<uint32_t>(now_us / 1000);
    if (!input.available || !input.valid ||
        now_ms - input.sampled_at_ms > config_.maximum_sample_age_ms || input.contact_count > 1 ||
        (!input.pressed && input.contact_count != 0) ||
        (input.pressed && (input.contact_count != 1 ||
                           input.x >= config_.width || input.y >= config_.height))) {
      cancel();
      return {};
    }
    if (!input.pressed) {
      const bool released = tracking_;
      tracking_ = false;
      armed_ = true;
      if (!released) return {};
      const auto duration_ms = now_ms - started_ms_;
      const float sx = config_.width ? float(g.side) / config_.width : 0;
      const float sy = config_.height ? float(g.side) / config_.height : 0;
      if((duration_ms<=g.maximum_duration && start_x_*sx>=0 && start_x_*sx<g.side && start_y_*sy>=g.side-g.edge && start_y_*sy<g.side && (start_y_*sy-last_y_*sy)>=g.travel && std::abs((last_x_*sx-start_x_*sx))*g.ratio<=(start_y_*sy-last_y_*sy)))
        return {TouchGesture::Home,start_x_,start_y_};
      if (duration_ms <= 500 && max_travel_ <= 12)
        return {TouchGesture::Tap, start_x_, start_y_};
      const int dx = static_cast<int>(last_x_) - start_x_;
      const int dy = static_cast<int>(last_y_) - start_y_;
      if(pull_refresh && duration_ms<=5000 && start_y_>=config_.pull_start_y && start_y_<config_.pull_end_y && dy>=48 && std::abs(dx)*2<=dy)
        return {TouchGesture::PullDown,start_x_,start_y_};
      if (duration_ms <= 1200 && std::abs(dx) >= 40 &&
          std::abs(dy) * 2 <= std::abs(dx))
        return {dx < 0 ? TouchGesture::Next : TouchGesture::Previous,
                start_x_, start_y_};
      return {};
    }
    if (!armed_) return {};
    if (!tracking_) {
      tracking_ = true;
      started_ms_ = now_ms;
      start_x_ = input.x;
      start_y_ = input.y;
      max_travel_ = 0;
    }
    last_x_ = input.x;
    last_y_ = input.y;
    max_travel_ = std::max(max_travel_, std::max(
        std::abs(static_cast<int>(input.x) - start_x_),
        std::abs(static_cast<int>(input.y) - start_y_)));
    return {};
  }

int TouchInputRouter::pull_refresh_distance(int64_t now_us) const {
    const int dx=int(last_x_)-start_x_,dy=int(last_y_)-start_y_;
    if(!tracking_||start_y_<config_.pull_start_y||start_y_>=config_.pull_end_y||uint32_t(now_us/1000)-started_ms_>5000||dy<=12||std::abs(dx)*2>dy)return 0;
    return std::min(dy,96);
  }
}
