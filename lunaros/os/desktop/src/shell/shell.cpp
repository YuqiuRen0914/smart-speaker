#include "internal.h"

namespace lunaredu::desktop {

using namespace shell_detail;

bool DesktopShell::configure(const DesktopApplication *declarations,size_t size) {
  if (!declarations || !size || size>capacity) return false;
  for (size_t i=0;i<size;++i) {
    if (!declarations[i].id || !*declarations[i].id || !valid_name(declarations[i].name) || !valid_render_hints(declarations[i].rendering)) return false;
    if(std::strlen(declarations[i].id)>=sizeof(preferences.startup_id) ||
        static_cast<unsigned>(declarations[i].icon)>static_cast<unsigned>(DesktopIcon::Music) ||
        static_cast<unsigned>(declarations[i].kind)>static_cast<unsigned>(DesktopAppKind::Surface) ||
        static_cast<unsigned>(declarations[i].category)>static_cast<unsigned>(DesktopAppCategory::Extension))return false;
    for(const char *p=declarations[i].id;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='_'||*p=='-'))return false;
    for (size_t j=0;j<i;++j) if (!std::strcmp(declarations[i].id,declarations[j].id)) return false;
  }
  const auto settings_route = settings_id;
  const auto registry = surface_registry;
  *this=DesktopShell{}; settings_id=settings_route;surface_registry=registry; apps=declarations; count=size;
  uint8_t slots[3]{};
  for(size_t i=0;i<size;++i)honeycomb_slots[i]=slots[static_cast<unsigned>(apps[i].category)]++;
  select(0);return true;
}
bool DesktopShell::replace_applications(const DesktopApplication *declarations,size_t size){
  auto checked=std::make_unique<DesktopShell>();
  if(!checked->configure(declarations,size))return false;
  const std::string selected=focus<count?apps[focus].id:"";
  for(size_t i=0;i<size;i++)for(size_t old=0;old<count;old++)if(!std::strcmp(declarations[i].id,apps[old].id)){
    checked->unread[i]=unread[old];checked->todo[i]=todo[old];break;
  }
  apps=declarations;count=size;std::copy_n(checked->unread,capacity,unread);std::copy_n(checked->todo,capacity,todo);
  std::copy_n(checked->honeycomb_slots,capacity,honeycomb_slots);
  const size_t selected_index=find_app(selected.c_str());focus=selected_index<count?selected_index:0;
  target_focus=focus;cancel_contact();settle_motion();camera=point(focus);if(lunaros::graphics::metrics::native&&home.enabled)home.reconcile(*this);
  if(!lunaros::graphics::metrics::native&&compact.enabled)compact.reconcile(*this);
  return true;
}
size_t DesktopShell::find_app(const char *id) const {
  if(id)for(size_t i=0;i<count;++i)if(!std::strcmp(apps[i].id,id))return i;
  return count;
}
bool DesktopShell::activate(const char *id){
  if(!id||!*id)return false;
  const auto index=find_app(id);
  if(index<count){if(apps[index].kind==DesktopAppKind::Surface&&!application_surface(*this,id))return false;enter(*this,index);return true;}
  // A selected system App may expose a surface without a launcher shortcut.
  DesktopSurface slot=DesktopSurface::Launcher;const auto *extension=application_surface(*this,id,&slot);
  if(!extension)return false;
  if(const auto close=desktop_surface_extension(*this,surface).close)close(*this);
  settle_motion();surface=slot;leaf_menu=nullptr;row=list_offset=0;cancel_contact();extension->open(*this);return true;
}
bool DesktopShell::restore_startup_selection(bool defer_launch) {
  if(!defer_launch)return activate(preferences.startup_id);
  const auto index=find_app(preferences.startup_id);
  if(index==count)return false;
  select(index);return true;
}

void DesktopShell::set_preferences(DesktopPreferences value, bool preserve_missing_startup) {
  if(value.auto_hide_status!=preferences.auto_hide_status){status_navigation.visible=false;status_navigation.activity(now_ms);}
  if (static_cast<unsigned>(value.layout)>2) value.layout=DesktopPreferences{}.layout;
  if constexpr(!lunaros::graphics::metrics::native)value.layout=DesktopLayout::Honeycomb;
  if(lunaros::graphics::metrics::native&&home.enabled)value.layout=DesktopLayout::Grid;
  if(value.text_size>2)value.text_size=1;
  if(static_cast<unsigned>(value.wallpaper)>4)value.wallpaper=DesktopWallpaper::Solid;
  value.wallpaper_path[sizeof(value.wallpaper_path)-1]=0;
  if(value.wallpaper!=preferences.wallpaper||std::strcmp(value.wallpaper_path,preferences.wallpaper_path))wallpaper_image.reset();
  value.rotation%=4; preferences=value; cancel_contact(); select(focus);
  preferences.startup_id[sizeof(preferences.startup_id)-1]=0;
  if(*preferences.startup_id) {
    bool found=false;
    for(size_t i=0;i<count;++i)if(!std::strcmp(apps[i].id,preferences.startup_id))found=true;
    if(!found && !preserve_missing_startup)preferences.startup_id[0]=0;
  }
}
void DesktopShell::set_badge(size_t i,uint32_t messages,uint32_t pending) {
  if (i<count) { unread[i]=std::min<uint32_t>(messages,99999); todo[i]=std::min<uint32_t>(pending,99999); }
}

bool DesktopShell::has_badge(size_t app) const {
  if(app>=count)return false;
  if(unread[app]||todo[app])return true;
  for(unsigned i=0;i<unsigned(DesktopSurface::Count);++i){const auto &e=desktop_surface_extension(*this,static_cast<DesktopSurface>(i));if(e.application && e.badge && !std::strcmp(e.application->id,apps[app].id))return e.badge(*this);}
  return false;
}
size_t system_surfaces(const DesktopShell &shell,DesktopSurface *out,size_t capacity) {
  std::array<DesktopSurface,static_cast<size_t>(DesktopSurface::Count)> entries{};
  size_t count=0;
  for(unsigned slot=0;slot<unsigned(DesktopSurface::Count);++slot) {
    const auto surface=static_cast<DesktopSurface>(slot);
    const auto &extension=desktop_surface_extension(shell,surface);
    if(!extension.open || extension.system_menu_order<0)continue;
    size_t index=count;
    while(index && desktop_surface_extension(shell,entries[index-1]).system_menu_order>extension.system_menu_order) {
      entries[index]=entries[index-1];--index;
    }
    entries[index]=surface;++count;
  }
  if(out)for(size_t index=0;index<std::min(count,capacity);++index)out[index]=entries[index];
  return count;
}
const char *DesktopShell::title() const {
  if(const auto title=desktop_surface_extension(*this,surface).title)return title(*this);
  switch(surface) {case DesktopSurface::Actions:return active_menu?active_menu->title:"设置";case DesktopSurface::System:return "系统";default:return preferences.show_labels&&apps&&count?apps[focus].name:"桌面";}
}

uint64_t shell_content_revision(const DesktopShell &s) {
  uint64_t hash=1469598103934665603ULL;
  const auto add=[&](uint64_t n){hash^=n;hash*=1099511628211ULL;};
  add(unsigned(s.surface));add(s.preferences.rotation);add(s.preferences.auto_hide_status);
  add(s.preferences.text_size);add(reinterpret_cast<uintptr_t>(s.active_menu));add(s.grid_menu()?s.row/4:0);
  if(const auto revision=desktop_surface_extension(s,s.surface).revision)add(revision(s));
  return hash?hash:1;
}
} // namespace lunaredu::desktop
