#pragma once
#include "desktop/ui/route.h"
namespace lunaredu::lunaros::apps::manager {
inline constexpr app::ViewRoute kStoreView{"os.appstore",0,"appstore",true};
}
#include "lunaros_app_manager_view.h"
#include "desktop/ui/screen_paint_port.h"
namespace lunaredu::lunaros::apps::manager {
inline void paint_view(const View &view,const StoreSnapshot *store,unsigned rotation,desktop::ScreenPaintPort &port,bool hide_status=false) {
  if (!port.begin_widgets()) return;
  if (store) paint(port.graphics().scene(),view,*store,hide_status);
  if (!port.finish_widgets("应用商店",false)) return;
  port.rotate(rotation); port.present_widgets();
}
}
