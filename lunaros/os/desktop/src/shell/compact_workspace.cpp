#include "internal.h"
#include "desktop/shell/compact_workspace.h"
namespace lunaredu::desktop {
namespace {
bool id_valid(const std::string &id){
  if(id.empty()||id.size()>63)return false;
  for(char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;
  return true;
}
bool unique(const std::vector<std::string> &ids,size_t limit){
  if(ids.size()>limit)return false;
  for(size_t i=0;i<ids.size();++i)if(!id_valid(ids[i])||std::find(ids.begin(),ids.begin()+i,ids[i])!=ids.begin()+i)return false;
  return true;
}
bool split(const std::string &text,std::vector<std::string> &ids,size_t maximum){
  size_t start=0;while(start<text.size()){
    const auto end=text.find(';',start);if(end==std::string::npos||ids.size()>=maximum)return false;
    ids.push_back(text.substr(start,end-start));start=end+1;
  }return true;
}
}
bool valid_compact_layout(const CompactLayout &l){
  if(!unique(l.order,32)||!unique(l.hidden,32))return false;
  for(const auto &id:l.hidden)if(std::find(l.order.begin(),l.order.end(),id)==l.order.end())return false;
  return true;
}
std::string encode_compact_layout(const CompactLayout &l){
  if(!valid_compact_layout(l))return {};
  std::string out="c2:";for(const auto &id:l.order)out+=id+';';out+='|';
  for(const auto &id:l.hidden)out+=id+';';
  return out;
}
bool decode_compact_layout(const std::string &text,CompactLayout &out){
  if(text.size()>8192||text.substr(0,3)!="c2:")return false;
  const auto separator=text.find('|');
  if(separator==std::string::npos||text.find('|',separator+1)!=std::string::npos)return false;
  CompactLayout l;if(!split(text.substr(3,separator-3),l.order,32)||!split(text.substr(separator+1),l.hidden,32)||!valid_compact_layout(l))return false;
  out=std::move(l);return true;
}
bool CompactWorkspace::visible(const char *id)const{return !enabled||!layout||!id||(std::find(layout->order.begin(),layout->order.end(),id)!=layout->order.end()&&std::find(layout->hidden.begin(),layout->hidden.end(),id)==layout->hidden.end());}
void CompactWorkspace::configure(DesktopShell &s,const CompactLayout *saved,bool read_failed,bool catalog_ready){
  if constexpr(lunaros::graphics::metrics::native)return;
  enabled=true;CompactLayout initial;
  if(saved&&valid_compact_layout(*saved))initial=*saved;
  else{for(size_t i=0;i<s.count;++i)initial.order.push_back(s.apps[i].id);}
  if(read_failed){save_failed=true;error="桌面记录不可用";}
  layout=std::make_shared<const CompactLayout>(std::move(initial));reconcile(s,catalog_ready);assign_slots(s);
}
void CompactWorkspace::assign_slots(DesktopShell &s)const{
  if(!enabled||!layout)return;
  std::fill_n(s.honeycomb_slots,s.capacity,uint8_t(255));uint8_t slots[3]{};
  for(const auto &id:layout->order){const auto index=s.find_app(id.c_str());if(index>=s.count||!visible(id.c_str()))continue;s.honeycomb_slots[index]=slots[unsigned(s.apps[index].category)]++;}
  if(s.focus>=s.count||!visible(s.apps[s.focus].id)){s.focus=0;while(s.focus<s.count&&!visible(s.apps[s.focus].id))++s.focus;if(s.focus==s.count)s.focus=0;}
  s.target_focus=s.focus;s.moving=false;s.camera=s.camera_target=s.point(s.focus);
}
void CompactWorkspace::reconcile(DesktopShell &s,bool catalog_ready){
  if(!enabled||!layout)return;
  auto next=std::make_shared<CompactLayout>(*layout);
  if(catalog_ready){const auto missing=[&](const std::string &id){return s.find_app(id.c_str())>=s.count;};
    next->order.erase(std::remove_if(next->order.begin(),next->order.end(),missing),next->order.end());
    next->hidden.erase(std::remove_if(next->hidden.begin(),next->hidden.end(),missing),next->hidden.end());}
  for(size_t i=0;i<s.count;++i)if(next->order.size()<32&&std::find(next->order.begin(),next->order.end(),s.apps[i].id)==next->order.end())next->order.push_back(s.apps[i].id);
  if(next->order!=layout->order||next->hidden!=layout->hidden){layout=next;dirty=true;cancel();}
  assign_slots(s);
  if(!selected.empty()&&s.find_app(selected.c_str())>=s.count){selected.clear();mode=CompactMode::Desktop;}
  page=std::min<unsigned>(page,s.count?(unsigned(s.count)-1)/3:0);
}
bool CompactWorkspace::hide(DesktopShell &s,const char *id){
  if(!layout||!id||s.find_app(id)>=s.count||!visible(id))return false;
  auto next=std::make_shared<CompactLayout>(*layout);next->hidden.push_back(id);
  if(!valid_compact_layout(*next))return false;
  layout=next;dirty=true;assign_slots(s);return true;
}
bool CompactWorkspace::restore(DesktopShell &s,const char *id){
  if(!layout||!id||s.find_app(id)>=s.count||visible(id))return false;
  auto next=std::make_shared<CompactLayout>(*layout);
  if(std::find(next->order.begin(),next->order.end(),id)==next->order.end()){if(next->order.size()>=32)return false;next->order.push_back(id);}
  next->hidden.erase(std::remove(next->hidden.begin(),next->hidden.end(),id),next->hidden.end());layout=next;dirty=true;assign_slots(s);return true;
}
bool CompactWorkspace::reorder(DesktopShell &s,const char *from,const char *to){
  const auto a=s.find_app(from),b=s.find_app(to);if(!layout||a>=s.count||b>=s.count||a==b||!visible(from)||!visible(to)||s.apps[a].category!=s.apps[b].category)return false;
  auto next=std::make_shared<CompactLayout>(*layout);auto first=std::find(next->order.begin(),next->order.end(),from),second=std::find(next->order.begin(),next->order.end(),to);
  if(first==next->order.end()||second==next->order.end())return false;
  const auto camera=s.camera;std::iter_swap(first,second);layout=next;dirty=true;assign_slots(s);s.camera=s.camera_target=camera;return true;
}
void CompactWorkspace::cancel(){contact=dragging=long_pressed=false;pressed_app=-1;edge_started=edge_step_ms=0;}
bool CompactWorkspace::tick(DesktopShell &s,uint32_t now){
  if(!enabled||s.surface!=DesktopSurface::Launcher)return false;
  bool changed=false;
  if(dragging){
    const int dx=last_x<18?-1:last_x>221?1:0,dy=last_y<s.launcher_top()+42?-1:last_y>221?1:0;
    if(!dx&&!dy)edge_started=edge_step_ms=0;
    else if(!edge_started)edge_started=now;
    else if(uint32_t(now-edge_started)>=350&&uint32_t(now-edge_step_ms)>=50){edge_step_ms=now;changed|=s.pan_honeycomb(s.camera,float(-dx*8),float(-dy*8));}
  }
  if(mode==CompactMode::Desktop&&s.contact&&!s.dragged&&uint32_t(now-s.contact_started_ms)>=500){
    selected.clear();const auto p=s.last;for(size_t i=0;i<s.count;++i){const auto tile=s.tile(i);if(tile.visible&&std::hypot(tile.x-p.x,tile.y-p.y)<=22){selected=s.apps[i].id;break;}}
    s.contact=false;mode=CompactMode::Edit;long_pressed=true;contact=true;last_x=int(p.x);last_y=int(p.y);changed=true;
  }
  if(contact&&!dragging&&!long_pressed&&uint32_t(now-started)>=500&&std::hypot(float(last_x-start_x),float(last_y-start_y))<8){
    if(mode==CompactMode::AllApps&&page*3+row<s.count){selected=s.apps[page*3+row].id;mode=CompactMode::AppActions;page=row=0;long_pressed=true;changed=true;}
  }
  return changed;
}
bool CompactWorkspace::persist(bool(*save)(const CompactLayout &)){
  if(!dirty||contact||dragging)return !save_failed;
  save_failed=!layout||!save||!save(*layout);if(!save_failed)dirty=false;return !save_failed;
}
}
