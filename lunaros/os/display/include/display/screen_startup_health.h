#pragma once
#include <cstdint>
namespace lunaredu::display {
// Readiness requires an actual submitted frame, not a fallback foreground.
// Input-owner observation only; product policy adds its hardware requirements.
class ScreenStartupHealth {
 public:
  explicit ScreenStartupHealth(bool initialized):initialized_(initialized){}
  void observe(bool ui_ready,uint32_t completed_transfers){
    ui_ready_=ui_ready;first_transfer_|=ui_ready&&completed_transfers!=0;
  }
  bool has_completed_transfer() const{return first_transfer_;}
  void observe_ready(bool ui_ready){ui_ready_=ui_ready;}
  bool healthy() const{return initialized_&&ui_ready_&&first_transfer_;}
 private:
  const bool initialized_;
  bool ui_ready_=false,first_transfer_=false;
};
}
