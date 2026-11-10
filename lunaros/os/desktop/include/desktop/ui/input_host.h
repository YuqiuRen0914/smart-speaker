#pragma once
#include "desktop/ui/session.h"
#include "desktop/shell/navigation.h"
#include "desktop/graphics/desktop.h"
namespace lunaredu::desktop {
struct UiInputSample {
  TouchInput touch;
  UiControlSample controls;
  int horizontal=0;
  int64_t now_us=0;
  bool blocked=false;
  int vertical=0,secondary_horizontal=0,secondary_vertical=0;
  bool axes_fresh=true;
};
struct UiInputEvents {
  bool confirm=false,back=false,top_owned=false,edge_back=false,primary=false;
  int encoder=0,command=-1,navigation=0;
  bool combo=false;
  TouchEvent pointer{};
  bool pointer_tracking=false;
};
class UiInputPort {
 public:
  virtual ~UiInputPort()=default;
  virtual void begin_tick()=0;
  virtual bool take_redraw()=0;
  virtual void redraw()=0;
  virtual bool safety()=0;
  virtual void contact_layout_changed()=0;
  virtual NavigationResult navigate()=0;
  virtual bool driving_view() const=0;
  virtual void observe_release(bool)=0;
  virtual void prepare_destination()=0;
  virtual int command(bool system)=0;
  virtual void widgets(bool &,bool &)=0;
  virtual bool dispatch(UiInputEvents)=0;
  virtual bool shell_input(UiInputEvents,bool touch_changed)=0;
  virtual bool axis_navigation() const=0;
  virtual bool widgets_enabled() const=0;
  virtual unsigned rotation() const=0;
  virtual void wake()=0;
  virtual void pointer_ended(const TouchEvent &)=0;
};
// Sequencing is shared by all displayed products. App policy and physical
// mappings arrive through ports; the existing input owner executes every call.
class UiInputHost {
 public:
  UiInputHost(UiSession &session,UiSessionState &state,DesktopShell &shell,
      lunaros::graphics::SceneRuntime &graphics):session_(session),state_(state),shell_(shell),graphics_(graphics){}
  void update(UiInputSample sample,UiInputPort &port);
 private:
  UiSession &session_;UiSessionState &state_;DesktopShell &shell_;lunaros::graphics::SceneRuntime &graphics_;
};
}
