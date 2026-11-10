#include "lunaros_ui_metrics.h"
#include "lunaros_settings_application_data.h"
#include "graphics/scene.h"
#include "desktop/ui/content_layout.h"
#include <algorithm>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#include "desktop/graphics/page.h"
#include "desktop/graphics/scene_icon.h"
namespace lunaredu::settings {
namespace ui=::lunaredu::lunaros::graphics::metrics;
using namespace desktop;
std::shared_ptr<const ApplicationDataState> application_data(const DesktopShell &s) {
  const auto &state=s.retained_surfaces[static_cast<size_t>(DesktopSurface::ApplicationData)];
  return state&&state->kind==&kApplicationDataKind?std::static_pointer_cast<const ApplicationDataState>(state):nullptr;
}
namespace {
using lunaros::apps::ApplicationOperationPhase;
auto edit(const DesktopShell &s) {
  auto current=application_data(s);
  return current?std::make_shared<ApplicationDataState>(*current):std::make_shared<ApplicationDataState>();
}
void publish(DesktopShell &s,std::shared_ptr<ApplicationDataState> v){++v->revision;s.retained_surfaces[static_cast<size_t>(DesktopSurface::ApplicationData)]=v;s.surface_state=std::move(v);}
bool refresh(ApplicationDataState &v) {
  std::string error;
  if((v.ports.prepare&&!v.ports.prepare(v.ports.prepare_context,error))||!v.ports.catalog||!v.ports.catalog(v.ports.context,v.applications,error)) {
    v.applications={};v.failed=true;v.message="应用列表读取失败，请重试";return false;
  }
  v.selected=std::min(v.selected,v.applications.empty()?size_t(0):v.applications.size()-1);
  v.failed=false;return true;
}
void reveal(DesktopShell &s,const ApplicationDataState &v) {
  if(v.selected<s.list_offset)s.list_offset=uint8_t(v.selected);
  if(v.selected>=size_t(s.list_offset)+ContentLayout(s.preferences.auto_hide_status).rows())s.list_offset=uint8_t(v.selected-ContentLayout(s.preferences.auto_hide_status).rows()+1);
}
bool command(DesktopShell &s,DesktopCommand cmd) {
  auto v=edit(s);
  if(v->operation)return cmd==DesktopCommand::Back?s.open_menu("os.settings"):false;
  if(cmd==DesktopCommand::Back) {
    if(v->page==DataPage::Confirm)return application_data_action(s,DataAction::Cancel);
    return s.open_menu("os.settings");
  }
  if(cmd==DesktopCommand::Confirm) {
    if(v->page==DataPage::Confirm) {
      const DataAction actions[]={DataAction::DeleteData,DataAction::Cancel,DataAction::Apply};
      return application_data_action(s,actions[std::min(v->action,2u)]);
    }
    if(v->selected>=v->applications.size())return false;
    v->page=DataPage::Confirm;v->action=1;v->delete_data=false;v->message.clear();v->failed=false;
  } else {
    const bool prev=cmd==DesktopCommand::Previous||cmd==DesktopCommand::Up||cmd==DesktopCommand::Left;
    const bool next=cmd==DesktopCommand::Next||cmd==DesktopCommand::Down||cmd==DesktopCommand::Right;
    if(!prev&&!next)return false;
    if(v->page==DataPage::Confirm)v->action=unsigned(std::clamp(int(v->action)+(prev?-1:1),0,2));
    else if(!v->applications.empty())v->selected=size_t(std::clamp(int(v->selected)+(prev?-1:1),0,int(v->applications.size()-1)));
  }
  reveal(s,*v);publish(s,v);return true;
}
void paint(lunaros::graphics::Scene &scene,const DesktopShell &s) {
  const ContentLayout layout(s.preferences.auto_hide_status);
  namespace page=lunaros::graphics::page;namespace theme=lunaros::graphics::theme;
  const auto v=application_data(s);if(!v)return;
  if(v->operation) {
    page::content_message(scene,layout,"正在卸载", "正在提交目录，请稍候");return;
  }
  if(v->page==DataPage::Confirm&&v->selected<v->applications.size()) {
    scene.label(20,{ui::px(8),layout.top,ui::px(224),ui::px(28)},v->applications[v->selected].name.c_str(),ui::font(20),theme::text);
    scene.label(21,{ui::px(8),layout.top+ui::px(28),ui::px(224),ui::px(20)},"卸载这个应用？",ui::font(16),theme::text);
    scene.label(22,{ui::px(8),layout.top+ui::px(48),ui::px(224),ui::px(16)},v->failed?v->message.c_str():"默认保留应用进度",ui::font(12),v->failed?theme::danger:theme::appsecondary);
    page::switch_row(scene,uint32_t(DataAction::DeleteData),1702,{ui::px(8),layout.bottom-ui::px(88),ui::px(224),ui::px(32)},"删除进度",v->delete_data,v->action==0,true,0,12);
    page::content_actions(scene,layout,{uint32_t(DataAction::Cancel),"取消"},{uint32_t(DataAction::Apply),"卸载",bool(v->ports.uninstall),theme::Tone::Danger});
    return;
  }
  if(v->applications.empty()) {
    page::content_message(scene,layout,v->failed?"读取失败":"暂无应用",v->failed?v->message.c_str():"到商店安装应用");
    page::content_actions(scene,layout,{}, {uint32_t(DataAction::Refresh),"刷新"});return;
  }
  scene.container(5000,{ui::px(8),layout.top,ui::px(224),layout.height()},s.list_offset*layout.row_pitch(),5001);
  const int gap=layout.row_pitch()-layout.row_height();
  scene.flex_layout(5000,false,gap);
  // Bounded visible widgets, with ordinary Flex spacers for the omitted rows.
  const size_t first=std::min(size_t(s.list_offset),v->applications.size());
  const size_t end=std::min(v->applications.size(),first+4);
  unsigned order=0;
  if(first){scene.label(0xff900001,{0,0,1,int(first)*layout.row_pitch()-gap},"",ui::font(12),theme::text,lunaros::graphics::Align::Left,5000);scene.flow_item(0xff900001,0,order++);}
  for(size_t i=first;i<end;++i){
    const auto &app=v->applications[i];
    page::row(scene,1000+i,{0,0,ui::px(224),layout.row_height()},app.name.c_str(),app.version.c_str(),s.keys_enabled&&i==v->selected,true,5000,lunaros::graphics::theme::Tone::Normal,40);
    scene.flow_item(1000+i,0,order++);
    scene_icon(scene,6000+i,{ui::px(8),(layout.row_height()-ui::px(24))/2,ui::px(24),ui::px(24)},lunaros::icons::find(app.icon_resource.c_str()),theme::appaccent,1000+i,s.preferences.antialias);
    scene.overlay(6000+i,{ui::px(8),(layout.row_height()-ui::px(24))/2,ui::px(24),ui::px(24)});
  }
  if(end<v->applications.size()){scene.label(0xff900002,{0,0,1,int(v->applications.size()-end)*layout.row_pitch()-gap},"",ui::font(12),theme::text,lunaros::graphics::Align::Left,5000);scene.flow_item(0xff900002,0,order);}
}
}
void bind_application_data(DesktopShell &s,lunaros::apps::ApplicationDataPorts ports) {
  auto v=edit(s);v->ports=ports;++v->revision;
  s.retained_surfaces[static_cast<size_t>(DesktopSurface::ApplicationData)]=v;
  if(s.surface==DesktopSurface::ApplicationData)s.surface_state=std::move(v);
}
bool open_application_data(DesktopShell &s) {
  auto v=edit(s);v->page=DataPage::Applications;v->message.clear();refresh(*v);
  s.surface=DesktopSurface::ApplicationData;s.row=s.list_offset=0;s.cancel_contact();publish(s,v);return true;
}
bool application_data_action(DesktopShell &s,DataAction action) {
  auto v=edit(s);
  if(v->operation)return false;
  if(action==DataAction::Refresh)refresh(*v);
  else if(v->page!=DataPage::Confirm||v->selected>=v->applications.size())return false;
  else if(action==DataAction::Cancel){v->page=DataPage::Applications;v->message.clear();v->failed=false;}
  else if(action==DataAction::DeleteData)v->delete_data=!v->delete_data;
  else if(action==DataAction::Apply) {
    std::string error;const bool ok=v->ports.uninstall&&v->ports.uninstall(v->ports.context,v->applications[v->selected].id,v->delete_data,v->operation,error)&&bool(v->operation);
    v->failed=!ok;v->message=ok?"卸载已受理":"卸载失败，请重试";
    if(ok)v->operation_id=v->applications[v->selected].id;
  }else return false;
  publish(s,v);return true;
}
const DesktopSurfaceExtension &application_data_extension() {
  static const auto value=[] {
    DesktopSurfaceExtension v;v.foreground_id="os.settings";v.open=[](DesktopShell &s){open_application_data(s);};v.command=command;
    v.poll=[](DesktopShell &s,uint32_t) {
      const auto current=application_data(s);
      if(!current||!current->operation)return false;
      const auto result=current->operation.result();
      if(result.phase==ApplicationOperationPhase::Pending)return false;
      uint64_t revision=0;
      const bool observed=result.phase!=ApplicationOperationPhase::Succeeded ||
          (current->ports.observe_revision&&current->ports.observe_revision(current->ports.context,revision));
      if(observed&&result.phase==ApplicationOperationPhase::Succeeded&&revision<result.catalog_revision)return false;
      auto next=edit(s);next->operation={};
      next->failed=result.phase!=ApplicationOperationPhase::Succeeded||!observed;
      next->message=!observed?"目录已提交，应用列表暂不可用":next->failed?"卸载未完成，请检查后重试":"应用已卸载";
      if(!next->failed){
        next->page=DataPage::Applications;refresh(*next);reveal(s,*next);
        if(!next->failed&&std::any_of(next->applications.begin(),next->applications.end(),[&](const auto &app){return app.id==next->operation_id;})){
          next->failed=true;next->message="目录已提交，当前应用已被其他操作修改";
        }
      }
      next->operation_id.clear();
      publish(s,next);return true;
    };
    v.title=[](const DesktopShell &){return "应用";};
    v.revision=[](const DesktopShell &s){auto v=application_data(s);return v?v->revision:0;};
    v.build=[](lunaros::graphics::Scene &scene,const DesktopShell &s,const DesktopStatus &){paint(scene,s);};
    v.action=[](DesktopShell &s,lunaros::graphics::Action a) {
      const auto current=application_data(s);if(current&&current->operation)return false;
      if(a.id==1702)return application_data_action(s,DataAction::DeleteData);
      if(a.id==5001){s.list_offset=uint8_t(std::clamp<int>(a.value/ContentLayout(s.preferences.auto_hide_status).row_pitch(),0,31));return true;}
      if(a.id>=1000&&a.id<1032) {
        auto v=edit(s);if(v->page!=DataPage::Applications||a.id-1000>=v->applications.size())return false;
        v->selected=a.id-1000;v->page=DataPage::Confirm;v->action=1;v->delete_data=false;v->failed=false;v->message.clear();publish(s,v);return true;
      }
      return application_data_action(s,DataAction(a.id));
    };return v;
  }();return value;
}
}
