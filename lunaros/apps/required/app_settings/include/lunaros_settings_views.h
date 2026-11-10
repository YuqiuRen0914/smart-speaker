#pragma once
#include "desktop/ui/route.h"
namespace lunaredu::settings {
inline constexpr app::ViewRoute kDetailView{"os.settings",0,"settings",true};
}
#include "lunaros_settings_renderer.h"
namespace lunaredu::desktop { class ScreenPaintPort; }
namespace lunaredu::settings {
class DetailView {
 public:
  void clear() { renderer_.clear(); }
  void paint(Detail detail,const SystemPresentation &model,const char *version,const char *device_name,
      desktop::ProvisioningStep step,unsigned rotation,desktop::ScreenPaintPort &port,bool auto_hide=false,unsigned focus=0);
 private:
  DetailRenderer renderer_;
};
}
