#pragma once
#include "desktop/ui/entry.h"
#include "desktop/application/view.h"
#include "desktop/application/downloaded_view.h"
#include "desktop/ui/paint_port.h"
namespace lunaredu::lunaros::apps {
inline desktop::UiEntry downloaded_ui_entry() { return {kDownloadedView}; }
struct DownloadedPresentation {
  std::shared_ptr<const DrawList> frame;
  std::string title;
  void paint(desktop::UiPaintPort &port,bool) const {
    if(!port.begin_canvas())return;
    if(frame)paint_application_frame(port.surface(),*frame);
    port.overlay(title.c_str());port.rotate(port.rotation());port.flush();
  }
};
template<class Bindings>
typename Bindings::Definition downloaded_ui_binding() {
  return {"os.downloaded",
    [](typename Bindings::CaptureContext &c) -> typename Bindings::Presentation { return c.downloaded_presentation(); },
    [](typename Bindings::InputContext &c) { return c.primary && !c.system_owned() && c.package_input(); },
    [](typename Bindings::EntryContext &c) { c.enter_ui(downloaded_ui_entry()); }};
}

}
