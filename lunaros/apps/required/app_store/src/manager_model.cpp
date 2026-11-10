#include "lunaros_app_store_desktop.h"
#include "lunaros_app_manager_view.h"
#include <algorithm>
#include <cstdio>
namespace lunaredu::lunaros::apps::manager {
const Application *find(const std::vector<Application> &apps,const std::string &id) {
  auto it=std::find_if(apps.begin(),apps.end(),[&](const auto &a){return a.id==id;});return it==apps.end()?nullptr:&*it;
}

const std::vector<Application> &listed(const StoreSnapshot &s,const View &) {return s.discovered.entries();}

size_t listed_count(const StoreSnapshot &s,const View &) {return s.discovered.size();}

const Application &listed_at(const StoreSnapshot &s,const View &,size_t index) {return s.discovered[index];}

bool busy(const StoreSnapshot &s) {return s.phase==StorePhase::Downloading||s.phase==StorePhase::Verifying||s.phase==StorePhase::Committing;}

bool update_available(const StoreSnapshot &s,const std::string &id) {auto a=find(s.installed.entries(),id),b=find(s.discovered.entries(),id);return a&&b&&compare_app_versions(b->version,a->version)>0;}

const char *status(const StoreSnapshot &s) {
  switch(s.phase){case StorePhase::Fetching:return "正在连接商店";case StorePhase::Downloading:return "正在下载应用";case StorePhase::Verifying:return "正在校验应用";case StorePhase::Committing:return "正在保存应用";default:return !s.error.empty()?"操作未完成，请重试":s.message.c_str();}
}

unsigned download_percent(const StoreSnapshot &s) {
  return s.total_bytes?unsigned(std::min<uint64_t>(100,uint64_t(s.received_bytes)*100/s.total_bytes)):0;
}

std::string package_size(uint32_t bytes) {
  if(!bytes)return "大小未知";
  char text[32];
  if(bytes>=1024*1024)std::snprintf(text,sizeof(text),"%.1f MB",double(bytes)/(1024*1024));
  else if(bytes>=1024)std::snprintf(text,sizeof(text),"%u KB",unsigned((uint64_t(bytes)+1023)/1024));
  else std::snprintf(text,sizeof(text),"%u B",unsigned(bytes));
  return text;
}
}

namespace lunaredu::lunaros::apps::manager {
void reconcile_selection(const StoreSnapshot &snapshot, View &view) {
  const auto count = listed_count(snapshot, view);
  if (view.selection >= count) view.selection = count ? count - 1 : 0;
}
std::string view_key(const StoreSnapshot &snapshot, const View &view) {
  return std::to_string(snapshot.generation) + ":" + std::to_string(view.selection) + ":" +
      view.detail_id + ":" + std::to_string(view.failed) + ":" + view.message;
}
}
