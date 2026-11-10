#include "desktop/graphics/canvas_spec.h"
#include "internal.h"
#include "shell/widget_ids.h"
#include "desktop/ui/content_layout.h"
#include "graphics/scene.h"

namespace lunaredu::desktop {

using namespace shell_detail;
bool DesktopShell::apply_widget_action(lunaros::graphics::Action a) {
  using namespace shell_widgets;
  const ContentLayout layout(preferences.auto_hide_status);
  if(surface==DesktopSurface::Launcher||home_navigation.contact||edge_navigation.owned||moving||page_drag)return false;
  if(const auto action=desktop_surface_extension(*this,surface).action)return action(*this,a);
  if(a.id==scroll_id){const unsigned value=unsigned(std::max<int32_t>(0,a.value));list_offset=uint8_t(std::min(value/layout.row_pitch(),31u));return true;}
  if(a.id>=row_id&&a.id<row_id+33)return activate_row(a.id-row_id);
  if(a.id>=toggle_id&&a.id<toggle_id+32)return activate_row(a.id-toggle_id);
  return false;
}

bool DesktopShell::configure_menus(const DesktopMenu *declarations,size_t size){
  if(!declarations||!size||size>capacity)return false;
  for(size_t i=0;i<size;++i){
    const auto &m=declarations[i];
    if(!m.id||!*m.id||std::strlen(m.id)>=48||!valid_name(m.title)||!m.items||!m.count||m.count>capacity)return false;
    if(static_cast<unsigned>(m.layout)>static_cast<unsigned>(DesktopMenuLayout::Direction))return false;
    if(m.layout==DesktopMenuLayout::Direction&&m.count!=4)return false;
    for(size_t n=0;n<i;++n)if(!std::strcmp(m.id,declarations[n].id))return false;
    for(size_t n=0;n<m.count;++n){const auto &item=m.items[n];
      if(!item.id||!*item.id||std::strlen(item.id)>=48||!valid_name(item.label))return false;
      if(static_cast<unsigned>(item.icon)>static_cast<unsigned>(DesktopIcon::Music))return false;
      if(static_cast<unsigned>(item.preference)>=static_cast<unsigned>(DesktopPreferenceAction::Count))return false;
      for(size_t p=0;p<n;++p)if(!std::strcmp(item.id,m.items[p].id))return false;
      if(item.submenu){bool found=false;for(size_t p=0;p<size;++p)if(declarations[p].id&&!std::strcmp(item.id,declarations[p].id))found=true;if(!found)return false;}
    }
  }
  // Reject cycles and ambiguous parents before publishing borrowed declarations.
  for(size_t i=0;i<size;++i){size_t current=i;
    for(size_t depth=0;depth<=size;++depth){size_t parent=size;
      for(size_t p=0;p<size;++p)for(size_t n=0;n<declarations[p].count;++n)
        if(declarations[p].items[n].submenu&&!std::strcmp(declarations[p].items[n].id,declarations[current].id)){if(parent!=size)return false;parent=p;}
      if(parent==size)break;
      if(depth==size)return false;
      current=parent;
    }
  }
  menus=declarations;menu_count=size;active_menu=nullptr;leaf_menu=nullptr;action=nullptr;
  ++menu_configuration_generation;
  for(auto &value:menu_values)value={};
  return true;
}
const char *DesktopShell::menu_value(const char *id) const {
  for(unsigned i=0;i<static_cast<unsigned>(DesktopSurface::Count);++i)
    if(const auto read=desktop_surface_extension(*this,static_cast<DesktopSurface>(i)).menu_value)
      if(const auto *value=read(*this,id))return value;
  if(id)for(const auto &value:menu_values)if(value.id&&!std::strcmp(value.id,id))return value.text;
  return "";
}
bool DesktopShell::menu_selected(const DesktopMenuItem &item) const {
  for(unsigned i=0;i<static_cast<unsigned>(DesktopSurface::Count);++i)
    if(const auto selected=desktop_surface_extension(*this,static_cast<DesktopSurface>(i)).menu_selected;selected&&selected(*this,item))return true;
  return false;
}
bool DesktopShell::set_menu_value(const char *id,const char *text){
  if(!id||!text||std::strlen(text)>=sizeof(menu_values[0].text))return false;
  // Registered slots already borrow a validated declaration. Reconfiguration
  // clears all slots, so steady UI ticks need not search every menu again.
  for(auto &value:menu_values)if(value.id&&!std::strcmp(value.id,id)){
    if(!std::strcmp(value.text,text))return false;
    std::strcpy(value.text,text);return true;
  }
  const char *canonical=nullptr;
  for(size_t i=0;i<menu_count;++i)for(size_t n=0;n<menus[i].count;++n)
    if(!std::strcmp(menus[i].items[n].id,id))canonical=menus[i].items[n].id;
  if(!canonical)return false;
  for(auto &value:menu_values)if(!value.id){value.id=canonical;std::strcpy(value.text,text);return true;}
  return false;
}
const DesktopMenuIdentity *DesktopShell::menu_identity(const char *id) const {
  if(id)for(const auto &value:menu_values)if(value.id&&!std::strcmp(value.id,id))return value.identity.get();
  return nullptr;
}
bool DesktopShell::set_menu_identity(const char *id,const char *title,std::shared_ptr<const DesktopImage> image){
  return title&&set_menu_identity(id,std::string_view(title),std::move(image));
}
bool DesktopShell::set_menu_identity(const char *id,std::string_view title,std::shared_ptr<const DesktopImage> image){
  if(!id||title.size()>=sizeof(DesktopMenuIdentity::title)||title.find('\0')!=std::string_view::npos)return false;
  DesktopMenuValue *slot=nullptr;
  for(auto &value:menu_values)if(value.id&&!std::strcmp(value.id,id)){slot=&value;break;}
  if(!slot){
    set_menu_value(id,"");
    for(auto &value:menu_values)if(value.id&&!std::strcmp(value.id,id)){slot=&value;break;}
  }
  if(slot){auto &value=*slot;
    const auto *previous=value.identity.get();
    if(title.empty()&&!image){if(!previous)return false;value.identity.reset();return true;}
    if(previous&&title==std::string_view(previous->title)&&previous->image==image)return false;
    auto identity=std::make_shared<DesktopMenuIdentity>();
    if(!title.empty())std::memcpy(identity->title,title.data(),title.size());
    identity->title[title.size()]='\0';identity->image=std::move(image);
    value.identity=std::move(identity);return true;
  }
  return false;
}
bool DesktopShell::open_menu(const char *id){
  if(!id)return false;
  for(size_t i=0;i<menu_count;++i)if(!std::strcmp(menus[i].id,id)){
    if(const auto close=desktop_surface_extension(*this,surface).close)close(*this);
    moving=page_drag=false;page_offset=0;
    surface=DesktopSurface::Actions;active_menu=&menus[i];leaf_menu=nullptr;row=menus[i].layout==DesktopMenuLayout::Direction?preferences.rotation:0;list_offset=0;action=nullptr;cancel_contact();return true;
  }
  return false;
}

void DesktopShell::navigate_menu(size_t item) {
  if(!grid_menu())return;
  item=std::min(item,active_menu->count-1);
  const size_t old_page=row/4,new_page=item/4;
  if(moving&&!page_drag&&old_page==new_page){row=static_cast<uint8_t>(item);return;}
  moving=page_drag=false;page_offset=0;row=static_cast<uint8_t>(item);list_offset=0;
  if(old_page!=new_page&&preferences.animations&&clock_ready){
    source_page=old_page;destination_page=new_page;offset_from=0;
    offset_target=new_page>old_page?-float(canvas_layout::side):float(canvas_layout::side);page_direction=new_page>old_page?1:-1;
    motion_started=now_ms;moving=true;
  }
}

} // namespace lunaredu::desktop
