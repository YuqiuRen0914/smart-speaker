#pragma once
#include "desktop/ui/system_views.h"
namespace lunaredu::desktop {
struct ViewNavigationLocation {
  int launcher_index=0;
  const char *menu=nullptr,*item=nullptr;
  app::ViewRoute previous=kShellView;
};
}
