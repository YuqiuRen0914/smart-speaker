#pragma once
#include "desktop/composition/applications.h"
#include "desktop/composition/shell_observers.h"
#include "desktop/ui/render_host.h"
#include "desktop/shell/application.h"
#include "app/execution/application_context.h"
#include <array>
#include <iterator>
#include <cstring>
#include <type_traits>
#include <cstddef>

namespace lunaredu::lunaros {
// A borrowed, read-only range with the same pointer/count contract used by
// DesktopShell. It also works with the repository's GCC 8 host fixtures.
template<class T> class DesktopRange {
 public:
  DesktopRange(const T *data,size_t count):data_(data),count_(count){}
  template<size_t N> DesktopRange(const T (&data)[N]):DesktopRange(data,N){}
  template<size_t N> DesktopRange(const std::array<T,N> &data):DesktopRange(data.data(),N){}
  const T *data() const {return data_;}
  size_t size() const {return count_;}
  bool empty() const {return count_==0;}
  const T *begin() const {return data_;}
  const T *end() const {return count_?data_+count_:data_;}
 private:
  const T *data_;size_t count_;
};
// Selection owns bounded metadata only. App factories still own their bindings;
// omitted Apps have no preparation, registration or lifecycle side effects.
template<size_t Apps, size_t Menus> struct DesktopCatalog {
  std::array<desktop::DesktopApplication, Apps> applications{}, launcher_storage{};
  std::array<desktop::DesktopMenu, Menus> menu_storage{};
  size_t launcher_count=0, menu_count=0;
  bool contains(const char *id) const {
    if(id)for(const auto &entry:applications)if(entry.id&&!std::strcmp(entry.id,id))return true;
    return false;
  }
  auto launchers() const {return DesktopRange<desktop::DesktopApplication>(launcher_storage.data(),launcher_count);}
  auto menus() const {return DesktopRange<desktop::DesktopMenu>(menu_storage.data(),menu_count);}
};
struct DesktopPreparation {
  const char *shell_id;
  const char *version;
  bool bindings_valid, display_ready;
  const DesktopAppDescriptor *providers=nullptr;
  size_t provider_count=0;
};
bool prepare_desktop_shell(desktop::DesktopUiHost &,DesktopRange<desktop::DesktopApplication>,
    DesktopRange<desktop::DesktopMenu>,DesktopPreparation,std::string &);
bool start_desktop_foreground(desktop::DesktopUiHost &,ApplicationContext *,std::string &);
// Synchronous default desktop sequence. Phase callbacks are never retained.
template<class Prepare,class Declare>
bool start_desktop_session(desktop::DesktopUiHost &ui,ApplicationContext *device,Prepare prepare,Declare declare,
    const char *&stage,std::string &error){
  stage="desktop_preparation";
  if(!ui.session().prepare(prepare,error))return false;
  stage="application_registration";
  if(!ui.session().declare(declare,error))return false;
  stage="foreground_startup";
  return start_desktop_foreground(ui,device,error);
}
template<class Catalog,class Sessions>
bool prepare_desktop_applications(desktop::DesktopUiHost &ui,const Catalog &catalog,
    KernelRuntime *runtime,ApplicationContext *device,Sessions &sessions,
    DesktopShellObservers &observers,DesktopPreparation options,std::string &error) {
  if(!prepare_desktop_shell(ui,catalog.launchers(),catalog.menus(),options,error))return false;
  sessions.bind(device);
  if(!runtime){error="desktop_runtime_missing";return false;}
  if(!sessions.prepare(*runtime,error))return false;
  if(!observers.configure(*runtime,options.providers,options.provider_count,
      catalog.applications.data(),catalog.applications.size(),error))return false;
  ui.restore_preferences(sessions.recovery_required());
  return true;
}
}
