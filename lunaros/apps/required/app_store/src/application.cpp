#include "lunaros_app_manager_application.h"
#include "lunaros_app_manager_navigation.h"
#include <algorithm>
#include <cstdlib>
namespace lunaredu::lunaros::apps::manager {
void activate_row(View &v,const StoreSnapshot &s) {
  if(v.selection>=s.discovered.size()||busy(s))return;
  v.detail_id=s.discovered[v.selection].id;v.detail_action=0;v.message.clear();v.failed=false;
}
bool back(View &v){if(v.detail_id.empty())return false;v.detail_id.clear();return true;}
ActionResult dispatch(View &v,Store &store,ApplicationsService &,uint32_t action) {
  const auto &s=store.snapshot();
  if(action==Cancel){back(v);return {};}
  if(busy(s))return {};
  const auto submitted=[&](bool ok){v.failed=!ok;v.message=ok?"":store.snapshot().error.empty()?"安装未完成，请重试":store.snapshot().error;};
  if(action==Refresh||action==Retry)submitted(store.refresh());
  else if(action==Previous||action==Next) {
    if(!s.discovered.empty())v.selection=size_t(std::clamp(int(v.selection/3*3)+(action==Next?3:-3),0,int(s.discovered.size()-1)));
  }else if(action>=Row) {
    if(action-Row<s.discovered.size()){v.selection=action-Row;activate_row(v,s);}
  }else if(action==Primary||action==Update) {
    // Only the advertised package may be installed. Installed applications are
    // opened from the launcher and removed through Settings.
    if(find(s.discovered.entries(),v.detail_id)&&(!find(s.installed.entries(),v.detail_id)||update_available(s,v.detail_id)))submitted(store.install(v.detail_id));
  }
  return {};
}
NavigationResult Navigation::update(View &v,const StoreSnapshot &s,NavigationInput input,void *context,ActionSink sink) {
  NavigationResult result;if(!sink)return result;
  auto action=[&](uint32_t id){sink(context,id);result.changed=true;};
  int step=input.confirm||input.back?0:input.step;
  if(input.confirm||input.back||step||!input.axes_fresh)stick_.reset();
  else {
    const auto axis=lunaros::input::combine_navigation({input.axis_x,-input.axis_y},{input.secondary_x,-input.secondary_y},lunaredu::app::kApplicationNavigationPolicy.full_scale);
    step=lunaros::input::navigation_step(stick_.update(axis.x,axis.y,input.now_ms));
  }
  if(v.detail_id.empty()) {
    if(input.next_page||input.previous_page)action(input.next_page?Next:Previous);
    if(step&&!s.discovered.empty()){v.selection=size_t(std::clamp(int(v.selection)+step,0,int(s.discovered.size()-1)));result.changed=true;}
    if(input.confirm){if(s.discovered.empty())action(Refresh);else{activate_row(v,s);result.changed=true;}}
  }else if(input.confirm)action(Primary);
  if(input.back&&back(v)){result.back_consumed=true;result.changed=true;}
  return result;
}
}
