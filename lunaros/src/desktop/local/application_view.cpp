#include "lunaredu_lunaros/desktop/local/application_view.h"
#include <array>
#include <cstring>
namespace {
using namespace lunaredu::desktop;
std::array<ApplicationViewPorts, 12> views{};
size_t count = 0;
bool sealed = false;
const ApplicationViewPorts *find(const char *route) {
  if (!route) return nullptr;
  for (size_t i = 0; i < count; ++i)
    for (size_t j = 0; j < views[i].route_count; ++j)
      if (std::strcmp(views[i].routes[j].id, route) == 0) return &views[i];
  return nullptr;
}
}
namespace lunaredu::desktop {
bool register_application_view(const ApplicationViewPorts &view) {
  if (sealed || !view.routes || !view.route_count || !view.mount || !view.take_change ||
      count == views.size()) return false;
  for (size_t i = 0; i < view.route_count; ++i) {
    if (!view.routes[i].id || !view.routes[i].id[0] || find(view.routes[i].id)) return false;
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(view.routes[i].id, view.routes[j].id) == 0) return false;
  }
  views[count++] = view;
  return true;
}
bool update_application_views(const char *active_route) {
  const auto *active = find(active_route);
  bool dirty = false;
  for (size_t i = 0; i < count; ++i) {
    const bool changed = views[i].take_change();
    dirty |= changed && active == &views[i];
  }
  return dirty;
}
}
bool lunaros_application_view_mount(const char *route, lv_obj_t *parent, bool entering) {
  const auto *view = find(route);
  if (!view || !parent) return false;
  sealed = true;
  view->mount(parent, route, entering);
  return true;
}
void lunaros_application_view_leave(const char *route, const char *next_route) {
  const auto *view = find(route);
  if (view && view->leave) view->leave(next_route);
}
bool lunaros_application_view_fullscreen(const char *route) {
  const auto *view = find(route);
  if (view) for (size_t i = 0; i < view->route_count; ++i)
    if (std::strcmp(view->routes[i].id, route) == 0) return view->routes[i].fullscreen;
  return false;
}
bool lunaros_application_view_exists(const char *route) { return find(route) != nullptr; }

bool lunaros_application_views_block_idle(void) {
  for (size_t i = 0; i < count; ++i)
    if (views[i].blocks_idle && views[i].blocks_idle()) return true;
  return false;
}
