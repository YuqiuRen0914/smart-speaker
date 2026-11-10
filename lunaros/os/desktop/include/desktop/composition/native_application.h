#pragma once
#include "app/lifecycle/application.h"
#include "desktop/ui/route.h"

namespace lunaredu::desktop {
struct NativeApplicationRoutePort {
  void *context=nullptr;
  bool (*entered)(void *, app::ViewRoute)=nullptr;
};
// Retained alongside the native application. All original callbacks retain
// their context; route admission happens only after a successful start. A
// route failure is reported to AppController, which owns checked cleanup.
class NativeApplicationBinding {
 public:
  NativeApplicationBinding(app::ForegroundDefinition application, app::ViewRoute route,
      NativeApplicationRoutePort port):application_(application),route_(route),port_(port) {}
  NativeApplicationBinding(const NativeApplicationBinding &)=delete;
  NativeApplicationBinding &operator=(const NativeApplicationBinding &)=delete;
  app::ForegroundDefinition definition();
 private:
  static NativeApplicationBinding &self(void *p){return *static_cast<NativeApplicationBinding *>(p);}
  app::ForegroundDefinition application_;
  app::ViewRoute route_;
  NativeApplicationRoutePort port_;
};
}
