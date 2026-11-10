#pragma once
#include "lunaros_python_view.h"
#include "lunaros_python_views.h"
#include "desktop/ui/paint_port.h"
namespace lunaredu::python {
struct UiState { Presentation python_view; };
struct RenderPresentation {
  Presentation view;
  void paint(desktop::UiPaintPort &port,bool) const {
    if(port.begin_widgets()&&paint_presentation(port.graphics().scene(),view,port.shell().preferences.auto_hide_status)&&port.finish_widgets("编程"))port.present_widgets();
  }
};
template<class Bindings>
typename Bindings::Definition ui_binding() {
  return {"system.python",
    [](typename Bindings::CaptureContext &c) -> typename Bindings::Presentation { return RenderPresentation{c.python_presentation()}; },
    [](typename Bindings::InputContext &c) {
      if(c.primary)return false;
      if(c.back||c.edge_back||c.command==int(desktop::DesktopCommand::Back))c.exit_python(false);
      else if(c.confirm)c.exit_python(true);
      if(c.python_exit_action())c.exit_python(true);
      return true;
    },nullptr,
    [](typename Bindings::LaunchContext &c,std::string &error) { return c.launch_foreground("system.python",error); }};
}

}
