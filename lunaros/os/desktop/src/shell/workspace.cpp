#include "desktop/shell/workspace.h"
#include "desktop/shell/shell.h"
#include "desktop/graphics/canvas_spec.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace lunaredu::desktop {
namespace {
struct Grid {int x,y,w,h;};
constexpr auto compile_grids(){
  std::array<Grid,18> grids{};
  for(unsigned s=0;s<2;++s) {
    for(unsigned r=3;r<=5;++r) {
      for(unsigned c=3;c<=5;++c) {
        const int margin=canvas_layout::side/20,top=s?status_layout::height:0;
        grids[s*9+(r-3)*3+c-3]={margin,top+8,(canvas_layout::side-margin*2)/int(c),(canvas_layout::side-top-36)/int(r)};
      }
    }
  }
  return grids;
}
constexpr auto grids=compile_grids();
bool identity(const std::string &id){
  if(id.empty()||id.size()>63)return false;
  for(char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;
  return true;
}
bool overlap(const HomeItem &a,const HomeItem &b){return a.page==b.page&&a.column<b.column+b.width&&b.column<a.column+a.width&&a.row<b.row+b.height&&b.row<a.row+a.height;}
bool fits(const HomeLayout &l,const HomeItem &item,int skip=-1){
  if(item.page>=l.pages||!item.width||!item.height||item.column+item.width>l.columns||item.row+item.height>l.rows)return false;
  for(size_t i=0;i<l.items.size();++i)if(int(i)!=skip&&overlap(l.items[i],item))return false;
  return true;
}
bool vacant(HomeLayout &l,HomeItem &item,unsigned first){
  for(unsigned p=first;p<HomeLayout::max_pages;++p){
    if(p>=l.pages)l.pages=uint8_t(p+1);
    item.page=uint8_t(p);
    for(unsigned y=0;y+item.height<=l.rows;++y)for(unsigned x=0;x+item.width<=l.columns;++x){
      item.column=uint8_t(x);item.row=uint8_t(y);if(fits(l,item))return true;
    }
  }return false;
}
HomeWidgetContent clock_content(){
  HomeWidgetContent c;const auto now=std::time(nullptr);std::tm local{};
#ifdef _WIN32
  localtime_s(&local,&now);
#else
  localtime_r(&now,&local);
#endif
  if(local.tm_year<120){std::strcpy(c.value,"--:--");std::strcpy(c.detail,"等待网络校时");}
  else{std::strftime(c.value,sizeof(c.value),"%H:%M",&local);std::snprintf(c.detail,sizeof(c.detail),"%d 月 %d 日",local.tm_mon+1,local.tm_mday);}
  return c;
}
}
HomeBox home_cell(unsigned c,unsigned r,bool status,unsigned x,unsigned y,unsigned w,unsigned h){
  c=std::clamp(c,3u,5u);r=std::clamp(r,3u,5u);const auto g=grids[unsigned(status)*9+(r-3)*3+c-3];
  return {g.x+int(x)*g.w,g.y+int(y)*g.h,int(w)*g.w-12,int(h)*g.h-12};
}
const HomeWidgetDefinition *system_home_widgets(size_t &count){
  static const HomeWidgetDefinition definitions[]={{"system.clock",nullptr,"时钟",2,2,clock_content,1000,1},{"system.connection",nullptr,"连接",2,1,nullptr,1000,0,HomeWidgetRoute::Settings}};
  count=std::size(definitions);return definitions;
}
bool valid_home_layout(const HomeLayout &l){
  if(l.columns<3||l.columns>5||l.rows<3||l.rows>5||!l.pages||l.pages>HomeLayout::max_pages||l.items.size()>HomeLayout::max_items||l.hidden.size()>32)return false;
  for(size_t i=0;i<l.items.size();++i){const auto &a=l.items[i];
    if(!identity(a.id)||unsigned(a.kind)>1||!fits(l,a,int(i)))return false;
    for(size_t j=0;j<i;++j)if(a.id==l.items[j].id&&a.kind==l.items[j].kind)return false;
    if(a.kind==HomeItemKind::Application&&(a.width!=1||a.height!=1||std::find(l.hidden.begin(),l.hidden.end(),a.id)!=l.hidden.end()))return false;
  }
  for(size_t i=0;i<l.hidden.size();++i){if(!identity(l.hidden[i]))return false;for(size_t j=0;j<i;++j)if(l.hidden[i]==l.hidden[j])return false;}
  return true;
}
std::string encode_home_layout(const HomeLayout &l){
  if(!valid_home_layout(l))return {};
  std::string out="h1:"+std::to_string(l.columns)+","+std::to_string(l.rows)+","+std::to_string(l.pages)+";";
  for(const auto &i:l.items)out+=std::to_string(unsigned(i.kind))+","+i.id+","+std::to_string(i.page)+","+std::to_string(i.column)+","+std::to_string(i.row)+","+std::to_string(i.width)+","+std::to_string(i.height)+";";
  out+='|';for(const auto &id:l.hidden)out+=id+";";return out;
}
bool decode_home_layout(const std::string &record,HomeLayout &out){
  if(record.size()>8192||record.substr(0,3)!="h1:")return false;
  const auto divide=record.find('|');if(divide==std::string::npos)return false;
  const auto end=record.find(';');if(end==std::string::npos||end>divide)return false;
  HomeLayout l;unsigned c,r,p;char extra;
  if(std::sscanf(record.substr(3,end-3).c_str(),"%3u,%3u,%3u%c",&c,&r,&p,&extra)!=3||c>255||r>255||p>255)return false;
  l.columns=uint8_t(c);l.rows=uint8_t(r);l.pages=uint8_t(p);
  size_t start=end+1;
  while(start<divide){const auto stop=record.find(';',start);if(stop==std::string::npos||stop>divide||l.items.size()>=HomeLayout::max_items)return false;
    unsigned k,x,y,w,h;char id[64]{};
    if(std::sscanf(record.substr(start,stop-start).c_str(),"%3u,%63[^,],%3u,%3u,%3u,%3u,%3u%c",&k,id,&p,&x,&y,&w,&h,&extra)!=7||k>1||p>255||x>255||y>255||w>255||h>255)return false;
    l.items.push_back({id,HomeItemKind(k),uint8_t(p),uint8_t(x),uint8_t(y),uint8_t(w),uint8_t(h)});start=stop+1;
  }
  start=divide+1;while(start<record.size()){const auto stop=record.find(';',start);if(stop==std::string::npos||l.hidden.size()>=32)return false;l.hidden.push_back(record.substr(start,stop-start));start=stop+1;}
  if(!valid_home_layout(l))return false;
  out=std::move(l);return true;
}
const HomeWidgetDefinition *HomeWorkspace::definition(const char *id)const{
  if(!id)return nullptr;
  for(size_t i=0;i<widget_count;++i)if(widgets[i].id&&!std::strcmp(widgets[i].id,id))return widgets+i;
  size_t n=0;const auto *system=system_home_widgets(n);for(size_t i=0;i<n;++i)if(!std::strcmp(system[i].id,id))return system+i;return nullptr;
}
const HomeWidgetContent *HomeWorkspace::content(const char *id)const{if(frames)for(const auto &f:*frames)if(f.id==id)return &f.content;return nullptr;}
void HomeWorkspace::configure(DesktopShell &s,const HomeWidgetDefinition *declared,size_t n,const HomeLayout *saved,bool read_failed,bool catalog_ready){
  enabled=lunaros::graphics::metrics::native;if(!enabled)return;
  widgets=declared;widget_count=declared?n:0;HomeLayout initial;
  if(read_failed){save_failed=true;error="桌面记录不可用，使用默认布局";}
  if(saved&&valid_home_layout(*saved))initial=*saved;
  else{
    initial.items.push_back({"system.clock",HomeItemKind::Widget,0,0,0,2,2});
    initial.items.push_back({"system.connection",HomeItemKind::Widget,0,2,0,2,1});
    if(n&&declared[0].width==2&&declared[0].height==1)initial.items.push_back({declared[0].id,HomeItemKind::Widget,0,2,1,2,1});
    for(size_t i=0;i<s.count;++i){HomeItem item{s.apps[i].id};if(vacant(initial,item,0))initial.items.push_back(std::move(item));}
  }
  layout=std::make_shared<const HomeLayout>(std::move(initial));reconcile(s,catalog_ready);
  s.preferences.layout=DesktopLayout::Grid;mode=HomeMode::Desktop;
}
void HomeWorkspace::reconcile(const DesktopShell &s,bool catalog_ready){
  if(!enabled||!layout)return;
  auto l=std::make_shared<HomeLayout>(*layout);bool changed=false;
  const auto stop=std::remove_if(l->items.begin(),l->items.end(),[&](const HomeItem &i){
    if(i.kind==HomeItemKind::Application)return catalog_ready&&s.find_app(i.id.c_str())>=s.count;
    const auto *d=definition(i.id.c_str());return !d||i.width!=d->width||(i.height!=d->height&&(!d->alternate_height||i.height!=d->alternate_height));
  });
  changed=stop!=l->items.end();l->items.erase(stop,l->items.end());
  if(catalog_ready){const auto end=std::remove_if(l->hidden.begin(),l->hidden.end(),[&](const std::string &id){return s.find_app(id.c_str())>=s.count;});changed|=end!=l->hidden.end();l->hidden.erase(end,l->hidden.end());}
  for(size_t i=0;i<s.count;++i){const auto id=std::string(s.apps[i].id);
    if(std::find(l->hidden.begin(),l->hidden.end(),id)!=l->hidden.end())continue;
    if(std::any_of(l->items.begin(),l->items.end(),[&](const HomeItem &v){return v.kind==HomeItemKind::Application&&v.id==id;}))continue;
    HomeItem item{id};if(l->items.size()<HomeLayout::max_items&&vacant(*l,item,l->pages-1)){l->items.push_back(std::move(item));changed=true;}
  }
  if(changed){layout=l;dirty=true;selected=-1;cancel();}
  const auto pages=(mode==HomeMode::AllApps||mode==HomeMode::DrawerActions)?std::max<size_t>(1,(s.count+layout->columns*layout->rows-1)/(layout->columns*layout->rows)):layout->pages;
  page=uint8_t(std::min<size_t>(page,pages-1));return_page=std::min<uint8_t>(return_page,layout->pages-1);
}
bool HomeWorkspace::place(const HomeItem &item,int replace,bool swap){
  if(!layout||!identity(item.id)||replace<-1||replace>=int(layout->items.size()))return false;
  if(item.kind==HomeItemKind::Widget){const auto *d=definition(item.id.c_str());if(!d||item.width!=d->width||(item.height!=d->height&&(!d->alternate_height||item.height!=d->alternate_height)))return false;}
  auto l=std::make_shared<HomeLayout>(*layout);if(item.page>=HomeLayout::max_pages)return false;l->pages=std::max<uint8_t>(l->pages,item.page+1);
  if(!fits(*l,item,replace)&&swap&&replace>=0&&item.width==1&&item.height==1){
    int other=-1;for(size_t i=0;i<l->items.size();++i)if(int(i)!=replace&&overlap(item,l->items[i])){if(other>=0||l->items[i].width!=1||l->items[i].height!=1)return false;other=int(i);}
    if(other>=0){auto target=l->items[other];const auto old=l->items[replace];target.page=old.page;target.column=old.column;target.row=old.row;l->items[other]=target;}
  }
  if(!fits(*l,item,replace))return false;
  if(replace>=0)l->items[replace]=item;else{if(l->items.size()>=HomeLayout::max_items)return false;l->items.push_back(item);}
  if(item.kind==HomeItemKind::Application)l->hidden.erase(std::remove(l->hidden.begin(),l->hidden.end(),item.id),l->hidden.end());
  if(!valid_home_layout(*l))return false;
  layout=l;dirty=true;error.clear();return true;
}
bool HomeWorkspace::remove(size_t i,bool hide){
  if(!layout||i>=layout->items.size())return false;
  auto l=std::make_shared<HomeLayout>(*layout);const auto item=l->items[i];
  if(hide&&item.kind==HomeItemKind::Application&&std::find(l->hidden.begin(),l->hidden.end(),item.id)==l->hidden.end()){if(l->hidden.size()>=32)return false;l->hidden.push_back(item.id);}
  l->items.erase(l->items.begin()+int(i));layout=l;dirty=true;selected=-1;return true;
}
bool HomeWorkspace::add_application(const char *id,unsigned preferred){
  if(!layout||!id||std::any_of(layout->items.begin(),layout->items.end(),[&](const HomeItem &i){return i.kind==HomeItemKind::Application&&i.id==id;}))return false;
  auto l=*layout;HomeItem item{id};if(!vacant(l,item,std::min<unsigned>(preferred,l.pages-1)))return false;return place(item);
}
bool HomeWorkspace::add_widget(const char *id){const auto *d=definition(id);if(!d||!layout||std::any_of(layout->items.begin(),layout->items.end(),[&](const HomeItem &i){return i.kind==HomeItemKind::Widget&&i.id==id;}))return false;auto l=*layout;HomeItem item{id,HomeItemKind::Widget,page,0,0,d->width,d->height};if(!vacant(l,item,page))return false;return place(item);}
bool HomeWorkspace::density(unsigned c,unsigned r){
  if(!layout||c<3||c>5||r<3||r>5)return false;
  auto l=std::make_shared<HomeLayout>(*layout);l->columns=uint8_t(c);l->rows=uint8_t(r);l->items.clear();
  for(auto item:layout->items){if(item.width>c||item.height>r)return false;if(!fits(*l,item)&&!vacant(*l,item,item.page))return false;l->items.push_back(std::move(item));}
  if(!valid_home_layout(*l))return false;
  layout=l;dirty=true;return true;
}
bool HomeWorkspace::add_page(){if(!layout||layout->pages>=HomeLayout::max_pages)return false;auto l=std::make_shared<HomeLayout>(*layout);page=l->pages++;layout=l;dirty=true;return true;}
bool HomeWorkspace::delete_page(unsigned p){
  if(!layout||layout->pages<=1||p>=layout->pages)return false;
  auto l=std::make_shared<HomeLayout>(*layout);std::vector<HomeItem> moved;
  l->items.erase(std::remove_if(l->items.begin(),l->items.end(),[&](const HomeItem &i){if(i.page==p){moved.push_back(i);return true;}return false;}),l->items.end());
  --l->pages;for(auto &i:l->items)if(i.page>p)--i.page;
  for(auto i:moved){if(!vacant(*l,i,0)||l->pages>=layout->pages)return false;l->items.push_back(std::move(i));}
  layout=l;page=std::min<uint8_t>(page,l->pages-1);dirty=true;return true;
}
bool HomeWorkspace::move_page(unsigned from,unsigned to){
  if(!layout||from>=layout->pages||to>=layout->pages)return false;
  auto l=std::make_shared<HomeLayout>(*layout);
  for(auto &i:l->items){if(i.page==from)i.page=uint8_t(to);else if(from<to&&i.page>from&&i.page<=to)--i.page;else if(from>to&&i.page>=to&&i.page<from)++i.page;}
  layout=l;page=uint8_t(to);dirty=true;return true;
}
bool HomeWorkspace::arrange(){if(!layout)return false;auto l=std::make_shared<HomeLayout>(*layout);l->items.clear();for(auto i:layout->items){if(!vacant(*l,i,0))return false;l->items.push_back(std::move(i));}layout=l;dirty=true;return true;}
bool HomeWorkspace::visible(unsigned p)const{return animating||swiping?p==source_page||p==destination_page:p==page;}
HomeBox HomeWorkspace::box(const DesktopShell &s,const HomeItem &i,bool moving)const{
  (void)s;auto b=home_cell(layout->columns,layout->rows,true,i.column,i.row,i.width,i.height);
  if(moving&&(animating||swiping))b.x+=int(offset)+(i.page==destination_page&&destination_page!=source_page?(destination_page>source_page?canvas_layout::side:-canvas_layout::side):0);
  return b;
}
HomeItem HomeWorkspace::drop_target(int x,int y)const{
  if(!layout||drag_item<0||size_t(drag_item)>=layout->items.size())return {};
  auto item=layout->items[size_t(drag_item)];item.page=page;
  const auto first=home_cell(layout->columns,layout->rows,true,0,0);
  // Center the dragged footprint, then snap its upper-left corner to a cell.
  item.column=uint8_t(std::clamp(int(std::lround(float(x-first.x)/(first.width+12)-item.width*.5f)),0,int(layout->columns-item.width)));
  item.row=uint8_t(std::clamp(int(std::lround(float(y-first.y)/(first.height+12)-item.height*.5f)),0,int(layout->rows-item.height)));
  return item;
}
int HomeWorkspace::hit(const DesktopShell &s,int x,int y)const{if(!layout)return -1;for(size_t i=0;i<layout->items.size();++i){const auto &item=layout->items[i];if(item.page!=page)continue;const auto b=box(s,item,false);if(x>=b.x&&x<b.x+b.width+12&&y>=b.y&&y<b.y+b.height+12)return int(i);}return -1;}
void HomeWorkspace::cancel(){contact=dragging=swiping=animating=long_pressed=false;drag_item=pressed_item=-1;offset=0;edge_started=0;}
void HomeWorkspace::turn(DesktopShell &s,int delta,bool animate){
  const unsigned pages=mode==HomeMode::AllApps?(unsigned(s.count)+layout->columns*layout->rows-1)/(layout->columns*layout->rows):layout->pages;
  if(!pages)return;
  const auto next=uint8_t(std::clamp(int(page)+delta,0,int(pages)-1));source_page=page;destination_page=next;page=next;
  offset_from=offset;offset_to=source_page==destination_page?0:next>source_page?-float(canvas_layout::side):float(canvas_layout::side);motion_started=s.now_ms;
  animating=animate&&s.preferences.animations&&source_page!=destination_page;if(!animating)offset=0;
}
bool HomeWorkspace::tick(DesktopShell &s,uint32_t now){
  if(!enabled||!layout||s.surface!=DesktopSurface::Launcher)return false;
  bool changed=false;
  if(contact&&!swiping&&!long_pressed&&uint32_t(now-started)>=500&&std::hypot(float(last_x-start_x),float(last_y-start_y))<12){
    if(mode==HomeMode::Desktop){mode=HomeMode::Edit;selected=pressed_item;long_pressed=true;changed=true;}
    else if(mode==HomeMode::AllApps&&drawer_selected>=0){mode=HomeMode::DrawerActions;long_pressed=true;changed=true;}
  }
  if(dragging){const int direction=last_x<24?-1:last_x>=canvas_layout::side-24?1:0;
    if(!direction)edge_started=0;else if(!edge_started)edge_started=now;
    else if(now-edge_started>=650){if(direction>0&&page+1==layout->pages)add_page();else turn(s,direction,false);edge_started=now;changed=true;}
  }
  if(animating){const float t=std::min(1.f,float(now-motion_started)/200.f);offset=offset_from+(offset_to-offset_from)*(1-(1-t)*(1-t)*(1-t));if(t>=1){animating=false;offset=0;}changed=true;}
  if(mode!=HomeMode::Desktop&&mode!=HomeMode::Edit)return changed;
  if(now-last_capture<250)return changed;
  last_capture=now;
  size_t visible_count=0;bool capture_due=false;
  for(const auto &i:layout->items)if(i.kind==HomeItemKind::Widget&&i.page==page){
    const auto *d=definition(i.id.c_str());if(!d||!d->capture)continue;++visible_count;
    const HomeWidgetFrame *old=nullptr;if(frames)for(const auto &f:*frames)if(f.id==i.id){old=&f;break;}
    if(!old||uint32_t(now-old->captured_ms)>=std::max<uint32_t>(1000,d->interval_ms))capture_due=true;
  }
  if(!capture_due&&frames&&frames->size()==visible_count)return changed;
  auto next=std::make_shared<std::vector<HomeWidgetFrame>>();
  for(const auto &i:layout->items)if(i.kind==HomeItemKind::Widget&&i.page==page){
    const auto *d=definition(i.id.c_str());if(!d||!d->capture)continue;
    const HomeWidgetFrame *old=nullptr;if(frames)for(const auto &frame:*frames)if(frame.id==i.id){old=&frame;break;}
    if(old&&uint32_t(now-old->captured_ms)<std::max<uint32_t>(1000,d->interval_ms)){next->push_back(*old);continue;}
    auto value=d->capture();value.value[sizeof(value.value)-1]=0;value.detail[sizeof(value.detail)-1]=0;
    next->push_back({i.id,value,now});
  }
  bool equal=frames&&next->size()==frames->size();if(equal)for(size_t i=0;i<next->size();++i){const auto &a=(*next)[i],&b=(*frames)[i];if(a.id!=b.id||std::strcmp(a.content.value,b.content.value)||std::strcmp(a.content.detail,b.content.detail)||a.content.color!=b.content.color||a.content.available!=b.content.available){equal=false;break;}}
  // Capture timestamps advance even when content is unchanged, without a redraw.
  frames=next;if(!equal)changed=true;return changed;
}
bool HomeWorkspace::persist(bool (*save)(const HomeLayout &)){if(!dirty||contact||dragging)return !save_failed;save_failed=!layout||!save||!save(*layout);if(!save_failed)dirty=false;return !save_failed;}
}
