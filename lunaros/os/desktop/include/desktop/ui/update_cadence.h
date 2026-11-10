#pragma once
#include <cstdint>
namespace lunaredu::desktop {
// Presentation work shares the display clock. Edge events remain eligible at
// every caller tick; analogue movement uses the latest observation each frame.
// No source ownership, queue, thread or retained borrowed data lives here.
struct UiUpdateSignal {
  uint64_t view=0;
  int32_t encoder=0;
  uint16_t buttons=0;
  uint32_t input_epoch=0;
  uint8_t contacts=0,axes_active=0;
  bool touch_available=false,touch_valid=false,touch_pressed=false;
  bool axes_fresh=false,blocked=false;
  bool operator==(const UiUpdateSignal &s) const {
    return view==s.view&&input_epoch==s.input_epoch&&encoder==s.encoder&&buttons==s.buttons&&contacts==s.contacts&&axes_active==s.axes_active&&
      touch_available==s.touch_available&&touch_valid==s.touch_valid&&touch_pressed==s.touch_pressed&&
      axes_fresh==s.axes_fresh&&blocked==s.blocked;
  }
};
class UiUpdateCadence {
 public:
  explicit UiUpdateCadence(uint32_t hz):hz_(hz){}
  bool admit(int64_t now,const UiUpdateSignal &signal,bool immediate=false) {
    if(now<0){initialized_=false;return true;}
    const auto time=uint64_t(now);
    // Divide before multiplying the long-running clock. Absolute slots carry
    // the fractional 33.333 ms without a drifting integer-ms interval.
    const auto slot=(time/1'000'000)*hz_+(time%1'000'000)*hz_/1'000'000;
    if(initialized_&&hz_&&time>=previous_&&slot==slot_&&signal==signal_&&!immediate)return false;
    initialized_=true;previous_=time;slot_=slot;signal_=signal;return true;
  }
 private:
  UiUpdateSignal signal_{};
  uint64_t previous_=0,slot_=0;
  uint32_t hz_=0;
  bool initialized_=false;
};
}
