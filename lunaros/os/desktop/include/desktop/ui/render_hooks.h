#pragma once
#include "desktop/ui/route.h"
#include "display/screen_host.h"
#include "display/frame.h"
namespace lunaredu::desktop {
struct UiRenderCache {
  void *context=nullptr;
  void (*inactive)(void *)=nullptr;
};
// Existing render/transfer owners execute these borrowed callbacks. Tickets
// acknowledge auxiliary copies even when a queued frame becomes obsolete.
struct UiRenderHooks {
  void *context=nullptr;
  bool (*before)(void *,uint32_t,app::ViewRoute)=nullptr;
  void (*after)(void *,uint32_t)=nullptr;
  void (*transfer)(void *,display::ScreenPatchView,display::ScreenRegion,unsigned)=nullptr;
  void (*unchanged)(void *,display::FrameCanvas)=nullptr;
  void (*presented)(void *,display::ScreenFrameView,const display::ScreenRegion *,size_t,unsigned)=nullptr;
  void (*prepared)(void *,display::FrameCanvas,uint32_t)=nullptr;
};
}
