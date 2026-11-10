#pragma once
#include "app/execution/navigation_policy.h"
#include <array>
#include <cstdlib>
#include "input/navigation_repeat.h"
namespace lunaredu::lunaros::apps {
struct ApplicationInputSample {
  bool realtime=false,fresh=false,confirm=false;
  std::array<int,4> axes{};int buttons=0,encoder=0,direction=0;
  bool pointer_valid=false,pointer_pressed=false;int x=0,y=0;
  uint32_t now_ms=0;
};
class ApplicationInput {
 public:
  using Sink=bool (*)(void *,int,int,int,int);
  bool cancel(void *context,Sink sink);
  void reset(){contact_=false;stick_.reset();}
  bool update(const ApplicationInputSample &s,void *context,Sink sink);
 private:bool contact_=false;lunaredu::app::ApplicationNavigationRepeat stick_;
};
}
