#include "lunaredu_lunaros/desktop/local/application_view.h"
#include <cassert>
#include <cstring>
using namespace lunaredu::desktop;
namespace {
int mounts = 0, leaves = 0, updates = 0;
bool entered = false;
bool busy = false;
bool blocks_idle() { return busy; }
void mount(lv_obj_t *, const char *route, bool entering) {
  assert(std::strcmp(route, "test") == 0); ++mounts; entered = entering;
}
void leave(const char *next) { assert(std::strcmp(next, "other") == 0); ++leaves; }
bool changed = false;
bool update() { ++updates; return changed; }
}
int main() {
  const ApplicationViewRoute routes[] = {{"test", false}, {"test-fullscreen", true}};
  assert(!register_application_view({}));
  assert(register_application_view({routes, 2, mount, leave, update, blocks_idle}));
  assert(!register_application_view({routes, 2, mount, leave, update, blocks_idle}));
  const ApplicationViewRoute duplicate[] = {{"new", false}, {"new", false}};
  assert(!register_application_view({duplicate, 2, mount, leave, update}));
  assert(!lunaros_application_view_exists("new"));
  assert(!lunaros_application_view_exists(nullptr));
  assert(lunaros_application_view_exists("test"));
  assert(lunaros_application_view_fullscreen("test-fullscreen"));
  assert(!lunaros_application_view_fullscreen("test"));
  assert(!lunaros_application_view_mount("test", nullptr, true));
  int storage = 0;
  auto *parent = reinterpret_cast<lv_obj_t *>(&storage);
  assert(!lunaros_application_view_mount("unknown", parent, true));
  assert(lunaros_application_view_mount("test", parent, true) && entered);
  assert(lunaros_application_view_mount("test", parent, false) && !entered);
  assert(mounts == 2);
  const ApplicationViewRoute late[] = {{"late", false}};
  assert(!register_application_view({late, 1, mount, leave, update}));
  assert(!lunaros_application_view_exists("late"));
  assert(!update_application_views( "test"));
  changed = true;
  assert(update_application_views( "test"));
  assert(!update_application_views( "other"));
  assert(updates == 3);
  lunaros_application_view_leave("test", "other");
  lunaros_application_view_leave("unknown", "other");
  assert(leaves == 1);
  assert(!lunaros_application_views_block_idle());
  busy = true;
  assert(lunaros_application_views_block_idle());
}
