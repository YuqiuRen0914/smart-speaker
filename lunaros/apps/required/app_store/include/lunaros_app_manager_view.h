#pragma once
#include "lunaros_app_store.h"
#include "app/package/application_package_policy.h"
#include "desktop/graphics/page.h"
#include "desktop/graphics/scene_icon.h"
namespace lunaredu::lunaros::apps::manager {
constexpr uint32_t Refresh=200, Discover=201, Installed=202, Cancel=209,
  Primary=210, Update=211, Uninstall=212, DeleteData=213,
  Previous=216, Next=217, Retry=218, Row=300;
struct View {
  bool failed=false;
  size_t selection=0;
  int detail_action=0,pull_distance=0;
  std::string detail_id,message;
};
const Application *find(const std::vector<Application> &apps,const std::string &id);
const std::vector<Application> &listed(const StoreSnapshot &s,const View &);
size_t listed_count(const StoreSnapshot &s,const View &);
const Application &listed_at(const StoreSnapshot &s,const View &,size_t index);
bool busy(const StoreSnapshot &s);
bool update_available(const StoreSnapshot &s,const std::string &id);
const char *status(const StoreSnapshot &s);
// Shared geometry and Scene children keep the app artwork inside its card.
struct CatalogLayout : desktop::ContentLayout {
  int first,item_pitch,item_height;
  explicit CatalogLayout(bool hide_status):ContentLayout(hide_status),first(top+graphics::metrics::px(36)),
    item_pitch((bottom-first+gap)/3),item_height(item_pitch-gap){}
};
unsigned download_percent(const StoreSnapshot &s);
std::string package_size(uint32_t bytes);
void application_icon(graphics::Scene &scene,uint32_t id,graphics::Rect box,
    const Application &app,uint32_t parent=0);
void paint(graphics::Scene &scene,const View &v,const StoreSnapshot &s,const CatalogLayout &layout);
inline void paint(graphics::Scene &scene,const View &v,const StoreSnapshot &s,bool hide_status=false) {
  paint(scene,v,s,CatalogLayout(hide_status));
}
}
