#include "app/execution/application_input.h"
namespace lunaredu::lunaros::apps {
bool ApplicationInput::cancel(void *context,Sink sink) {
    if(!contact_)return true;
    if(!sink||!sink(context,6,0,0,0))return false;
    contact_=false;return true;
  }

bool ApplicationInput::update(const ApplicationInputSample &s,void *context,Sink sink) {
    if(!sink)return false;
    bool ok=true;
    auto send=[&](int kind,int a=0,int b=0,int c=0){const bool accepted=sink(context,kind,a,b,c);ok=accepted&&ok;return accepted;};
    if(!s.pointer_valid){ok=cancel(context,sink)&&ok;}
    else if(s.pointer_pressed){const bool accepted=send(contact_?3:2,s.x,s.y);contact_=contact_||accepted;}
    else if(contact_){if(send(4,s.x,s.y))contact_=false;}
    if(s.realtime){
      send(8,0,s.fresh?s.axes[0]:0,s.fresh?s.axes[1]:0);
      send(8,1,s.fresh?s.axes[2]:0,s.fresh?s.axes[3]:0);
      send(9,s.fresh?s.buttons:0,s.fresh?1:0);
      if(s.fresh&&s.encoder)send(10,s.encoder);
    }else if(s.fresh&&!s.confirm&&!s.direction){
      const auto axis=input::combine_navigation({s.axes[0],-s.axes[1]},{s.axes[2],-s.axes[3]},lunaredu::app::kApplicationNavigationPolicy.full_scale);
      using D=input::NavigationDirection;
      const auto d=stick_.update(axis.x,axis.y,s.now_ms);
      if(d!=D::None)send(1,d==D::Left?1:d==D::Right?2:d==D::Up?3:4);
    }else stick_.reset();
    if(s.confirm)send(5);
    if(s.direction>=1&&s.direction<=4)send(1,s.direction);
    return ok;
  }
}
