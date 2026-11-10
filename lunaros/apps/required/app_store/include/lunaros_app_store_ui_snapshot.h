#pragma once
#include "desktop/ui/entry.h"
#include "lunaros_app_store_views.h"
#include "desktop/ui/paint_port.h"
namespace lunaredu::lunaros::apps::manager {
inline desktop::UiEntry ui_entry() { return {kStoreView}; }
struct RenderPresentation {
  View view;
  std::shared_ptr<const StoreSnapshot> store;
  void paint(desktop::UiPaintPort &port,bool) const { paint_view(view,store.get(),port.rotation(),port,port.shell().preferences.auto_hide_status); }
};
template<class Bindings>
typename Bindings::Definition ui_binding() {
  return {"os.appstore",
    [](typename Bindings::CaptureContext &c) -> typename Bindings::Presentation { return c.store_presentation(); },
    [](typename Bindings::InputContext &c) { return c.primary && !c.system_owned() && c.package_input(); },
    [](typename Bindings::EntryContext &c) { c.enter_ui(ui_entry()); },
    [](typename Bindings::LaunchContext &c,std::string &) { return c.open_store(); }};
}

}
