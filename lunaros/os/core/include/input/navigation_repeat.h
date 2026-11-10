#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace lunaredu::lunaros::input {
enum class NavigationDirection : uint8_t { None, Right, Down, Left, Up };
struct NavigationVector { int x=0, y=0; };
struct NavigationPolicy {
  int neutral_threshold, activation_threshold, accelerated_threshold;
  int axis_hysteresis, full_scale;
  uint32_t initial_delay_ms, repeat_interval_ms, accelerated_interval_ms;
  constexpr bool valid() const {
    return neutral_threshold >= 0 && activation_threshold > neutral_threshold &&
        accelerated_threshold >= activation_threshold && full_scale >= accelerated_threshold &&
        axis_hysteresis >= 0 && initial_delay_ms < 0x80000000u &&
        repeat_interval_ms > 0 && repeat_interval_ms < 0x80000000u &&
        accelerated_interval_ms > 0 && accelerated_interval_ms < 0x80000000u;
  }
};

// UI axes add together, saturating at a full push; opposite pushes cancel.
// Games and remote control continue to receive the original four axes.
inline NavigationVector combine_navigation(NavigationVector first,NavigationVector second,int full_scale) {
  return {std::clamp(first.x+second.x,-full_scale,full_scale),std::clamp(first.y+second.y,-full_scale,full_scale)};
}

// Rearm each physical stick independently before admitting it to UI input.
// A held/stuck stick at startup or after a page switch must neither move the
// new page nor prevent a neutral second stick from taking control. Once armed,
// sustained full pushes remain valid; no timeout guesses at hardware failure.
template<NavigationPolicy Policy> class NavigationMerger {
  static_assert(Policy.valid());
 public:
  void reset() { first_armed_=second_armed_=false; }
  static bool centered(NavigationVector v){return std::abs(v.x)<=Policy.neutral_threshold&&std::abs(v.y)<=Policy.neutral_threshold;}
  NavigationVector update(NavigationVector first,NavigationVector second) {
    return combine_navigation(admit(first,first_armed_),admit(second,second_armed_),Policy.full_scale);
  }
 private:
  static NavigationVector admit(NavigationVector value,bool &armed) {
    if(centered(value)){armed=true;return {};}
    return armed?value:NavigationVector{};
  }
  bool first_armed_=false,second_armed_=false;
};

// No allocation, timer or worker. Call on the input owner's monotonic clock.
// Repeats are bounded to one per observation; late frames never catch up in a
// burst. Reset requires a neutral observation before another page can move.
template<NavigationPolicy Policy> class NavigationRepeat {
  static_assert(Policy.valid());
 public:
  void reset() { armed_=false; direction_=NavigationDirection::None; }
  NavigationDirection update(int x,int y,uint32_t now) {
    const int ax=std::abs(x),ay=std::abs(y),strength=std::max(ax,ay);
    if(strength<=Policy.neutral_threshold){armed_=true;direction_=NavigationDirection::None;return NavigationDirection::None;}
    if(!armed_||strength<Policy.activation_threshold)return NavigationDirection::None;
    bool horizontal=ax>=ay;
    // Retain the dominant axis around a diagonal instead of alternating rows.
    if(direction_==NavigationDirection::Left||direction_==NavigationDirection::Right)horizontal=ay<=ax+Policy.axis_hysteresis;
    else if(direction_!=NavigationDirection::None)horizontal=ax>ay+Policy.axis_hysteresis;
    const auto next=horizontal?(x<0?NavigationDirection::Left:NavigationDirection::Right):
        (y<0?NavigationDirection::Up:NavigationDirection::Down);
    if(next!=direction_){direction_=next;started_=last_=now;return next;}
    const uint32_t interval=strength>=Policy.accelerated_threshold?Policy.accelerated_interval_ms:Policy.repeat_interval_ms;
    if(uint32_t(now-started_)<Policy.initial_delay_ms||uint32_t(now-last_)<interval)return NavigationDirection::None;
    last_=now;return next;
  }
 private:
  bool armed_=false;
  NavigationDirection direction_=NavigationDirection::None;
  uint32_t started_=0,last_=0;
};
inline NavigationVector rotate_navigation(NavigationVector v,unsigned rotation) {
  switch(rotation%4){case 1:return {v.y,-v.x};case 2:return {-v.x,-v.y};case 3:return {-v.y,v.x};default:return v;}
}
inline int navigation_step(NavigationDirection d) {
  return d==NavigationDirection::Right||d==NavigationDirection::Down?1:
      d==NavigationDirection::Left||d==NavigationDirection::Up?-1:0;
}
} // namespace lunaredu::lunaros::input
