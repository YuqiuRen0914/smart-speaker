#include "desktop/navigation/snapshot.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#endif
namespace lunaredu::desktop {
namespace {
struct Slot {std::atomic<unsigned> state{0};DesktopShell *shell=nullptr;bool retain=false;};
std::array<Slot,2> slots;
DesktopShell *create(bool &retain){
#ifdef ESP_PLATFORM
  void *p=heap_caps_malloc_prefer(sizeof(DesktopShell),2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  retain=p&&esp_ptr_external_ram(p);
#else
  void *p=std::malloc(sizeof(DesktopShell));retain=true;
#endif
  return p?new(p) DesktopShell:nullptr;
}
void destroy(DesktopShell *s){if(s){s->~DesktopShell();std::free(s);}}
}
void release_shell_render_leases(DesktopShell &s){
  s.wallpaper_image.reset();s.surface_state.reset();
  for(auto &surface:s.retained_surfaces)surface.reset();
  for(auto &value:s.menu_values)value.identity.reset();
  s.home.layout.reset();s.home.frames.reset();s.compact.layout.reset();
  for(auto *text:{&s.home.error,&s.home.pending_uninstall,&s.compact.selected,&s.compact.pending_uninstall,&s.compact.error}){
    text->clear();if(text->capacity()>512)std::string().swap(*text);
  }
  s.apps=nullptr;s.count=0;s.menus=s.active_menu=s.leaf_menu=nullptr;
  s.menu_count=0;s.action=nullptr;s.home.widgets=nullptr;s.home.widget_count=0;
}
NavigationSnapshot::NavigationSnapshot(const DesktopShell &source){
  for(size_t i=0;i<slots.size();++i){
    unsigned expected=0;if(!slots[i].state.compare_exchange_strong(expected,1))continue;
    slot_=int(i);auto &slot=slots[i];
    if(!slot.shell)slot.shell=create(slot.retain);
    shell_=slot.shell;break;
  }
  if(slot_<0){bool unused=false;shell_=create(unused);}
  if(shell_)*shell_=source;
}
NavigationSnapshot::~NavigationSnapshot(){
  if(slot_<0){destroy(shell_);return;}
  auto &slot=slots[size_t(slot_)];if(shell_)release_shell_render_leases(*shell_);
  unsigned expected=1;
  if(slot.retain&&slot.state.compare_exchange_strong(expected,0))return;
  destroy(slot.shell);slot.shell=nullptr;slot.retain=false;slot.state.store(0);
}
void retire_navigation_snapshots(){
  for(auto &slot:slots){
    unsigned expected=0;
    while(!slot.state.compare_exchange_weak(expected,2)){
      if(expected==2)break;
      if(expected==1){if(slot.state.compare_exchange_strong(expected,2))break;}
      expected=0;
    }
    if(expected==0){destroy(slot.shell);slot.shell=nullptr;slot.retain=false;slot.state.store(0);}
  }
}
}
