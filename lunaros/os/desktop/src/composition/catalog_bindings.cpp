#include "composition/applications_internal.h"
#include "desktop/composition/applications.h"
#include <cstring>
namespace lunaredu::lunaros {
const DesktopAppDescriptor *find_desktop_application(const DesktopAppDescriptor *providers,size_t count,const char *id) {
  if(!id||!providers)return nullptr;
  for(size_t i=0;i<count;++i)if(providers[i].id&&!std::strcmp(providers[i].id,id))return &providers[i];
  return nullptr;
}
}
