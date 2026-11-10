#pragma once
#include "graphics/scene_runtime.h"
#include "desktop/shell/shell.h"
namespace lunaredu::lunaros::graphics {
Page select_desktop(SceneRuntime &,uint64_t,const desktop::DesktopShell &);
bool render_desktop(SceneRuntime &,Page,const desktop::DesktopShell &,const desktop::DesktopStatus &,
    RasterOutput,desktop::DesktopPainter,bool indicator=false,uint16_t *navigation_source=nullptr);
bool paint_desktop(Scene &,uint64_t epoch,const desktop::DesktopShell &,
    const desktop::DesktopStatus &,RasterOutput,desktop::DesktopPainter,
    Scene::Emit=nullptr,void *context=nullptr,bool indicator=true,uint16_t *navigation_source=nullptr);
}
