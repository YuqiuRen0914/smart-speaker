#include "lunaros_settings_views.h"
#include "desktop/ui/screen_paint_port.h"

namespace lunaredu::settings {
void DetailView::paint(Detail detail,const SystemPresentation &model,const char *version,const char *device_name,
    desktop::ProvisioningStep step,unsigned rotation,desktop::ScreenPaintPort &port,bool auto_hide,unsigned focus) {
    if (!port.begin_widgets()) return;
    if (!renderer_.build(port.graphics().scene(),detail,model,version,device_name,step,{1,2},auto_hide,focus) ||
        !port.finish_widgets(DetailRenderer::title(detail,model),false)) return;
    renderer_.paint_activity(port.surface());
    (void)renderer_.paint_overlay(&port,[](void *p,int x,int y,int w,int h,bool dark) {
      static_cast<desktop::ScreenPaintPort *>(p)->surface().rect({x,y,w,h},dark?0:0xffff);
    });
    port.rotate(rotation); port.present_widgets();
  }
} // namespace lunaredu::settings
