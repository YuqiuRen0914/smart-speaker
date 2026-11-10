#pragma once
#include "desktop/ui/route.h"
#include "desktop/ui/system_views.h"
namespace lunaredu::app {
enum class ViewBackDisposition { PassThrough, Blocked, Navigate };
enum class ViewBackOrigin { Control, SystemGesture };
struct ViewBackRequest {
  ViewBackDisposition disposition=ViewBackDisposition::PassThrough;
  ViewRoute target=desktop::kShellView;
  const char *menu=nullptr;
};
// Apps prepare business cancellation/confirmation and declare the destination.
// OS executes the navigation transaction through the same checked view path.
template<class Navigate> bool dispatch_view_back(ViewBackRequest request,Navigate navigate) {
  if(request.disposition==ViewBackDisposition::PassThrough)return false;
  if(request.disposition==ViewBackDisposition::Navigate)navigate(request.target,request.menu);
  return true;
}
}
