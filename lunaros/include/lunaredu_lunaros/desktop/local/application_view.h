#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
// These operations run on the single LVGL owner. The shell owns the root;
// apps own child widgets and transient state and release them on departure.
bool lunaros_application_view_mount(const char *route, lv_obj_t *parent, bool entering);
void lunaros_application_view_leave(const char *route, const char *next_route);
bool lunaros_application_view_fullscreen(const char *route);
bool lunaros_application_view_exists(const char *route);
// Active app work may defer the shell idle return without app-specific flags.
bool lunaros_application_views_block_idle(void);
#ifdef __cplusplus
}
namespace lunaredu::desktop {
struct ApplicationViewRoute { const char *id; bool fullscreen = false; };
struct ApplicationViewPorts {
  const ApplicationViewRoute *routes = nullptr;
  size_t route_count = 0;
  void (*mount)(lv_obj_t *, const char *, bool) = nullptr;
  void (*leave)(const char *) = nullptr;
  bool (*take_change)() = nullptr;
  bool (*blocks_idle)() = nullptr;
};
// Fixed registry, immutable after product startup; duplicate routes rejected.
bool register_application_view(const ApplicationViewPorts &view);
bool update_application_views(const char *active_route);
}
#endif
