#pragma once
#include <cstddef>
#include <cstdint>
#include "desktop/ui/render_policy.h"
#include "desktop/ui/wallpaper.h"
#include "desktop/navigation/home_navigation.h"
#include "desktop/navigation/edge_navigation.h"
#include "desktop/navigation/status_navigation.h"
#include <memory>
#include <string_view>
#include "desktop/ui/surface_extension.h"
#include "desktop/shell/workspace.h"
#include "desktop/shell/compact_workspace.h"

namespace lunaredu::desktop {
// The shell is a value object: the input owner mutates it, the renderer consumes
// a snapshot. Application declarations and their strings have static lifetime.
enum class DesktopLayout : uint8_t { Grid, Single, Honeycomb };
enum class DesktopSurface : uint8_t { Launcher, System, Settings, Status, Notifications, Files, Actions, Startup, Wifi, ApplicationData, Count };
enum class DesktopCommand : uint8_t { Previous, Next, Left, Right, Up, Down, Confirm, Back, System, Home };
enum class DesktopIcon : uint8_t { Target, Range, Rate, Link, Reset, Wifi, Sensor, Motion, Receiver, Remote, Games, Settings, Files, Bell, Joystick, Axes, Radio, Adjust, Display, Store, Download, Code, Bluetooth, Pet, Music };
enum class DesktopAppKind : uint8_t { Application, Surface };
enum class DesktopAppCategory : uint8_t { System, Functional, Extension };
struct DesktopApplication {
  const char *id;
  const char *name; // At most four Unicode characters, validated at registration.
  DesktopIcon icon;
  uint16_t color;
  DesktopAppKind kind = DesktopAppKind::Application;
  ApplicationRenderHints rendering{};
  const char *icon_resource = nullptr;
  DesktopAppCategory category = DesktopAppCategory::Functional;
  bool uninstallable = false; // Capability, independent of the visual category.
};
struct DesktopPreferences {
  DesktopLayout layout = DesktopLayout::Honeycomb;
  bool show_labels = true;
  uint8_t rotation = 0; // Quarter turns, clockwise; shell and input share it.
  char startup_id[48]{}; // Empty starts at the desktop; otherwise a stable app ID.
  bool antialias = true;
  bool animations = true;
  bool auto_hide_status = false;
  DesktopWallpaper wallpaper = DesktopWallpaper::Solid;
  char wallpaper_path[256]{};
  bool customized = false; // Observation only; never part of the stored record.
  uint8_t text_size = 1; // Content text: smaller (90), standard (100), larger (115).
  bool honeycomb_lines = true;
};
struct DesktopPoint { float x = 0, y = 0; };
struct DesktopTile { float x, y, diameter; bool visible, clickable; float detail=1; };
struct DesktopStatus {
  enum class Severity : uint8_t { Normal, Attention, Critical, Conflict };
  bool wifi = false, platform = false, platform_available = false, peer = false;
  int battery_percent = -1;
  bool charging = false;
  const char *network = "未连接";
  const char *location = "桌面";
  Severity peer_severity = Severity::Normal;
  bool wifi_connecting = false;
  bool bluetooth = false;
  bool network_title = false; // Render the frame-owned IP/location on every view.
};
// Static product declarations; the shell owns navigation and rendering, while
// the input owner consumes leaf action IDs and executes the product operation.
enum class DesktopMenuLayout : uint8_t { List, Grid2x2, Direction };
enum class DesktopPreferenceAction : uint8_t {
  None, Layout, Labels, Rotation, Antialias, Animations, Startup,
  Grid, Single, Honeycomb, Rotate0, Rotate90, Rotate180, Rotate270,
  StatusBar, StatusAuto, StatusAlways,
  WallpaperSolid, WallpaperOcean, WallpaperAurora, WallpaperSunset, TextSize,
  Home3x3, Home3x4, Home3x5, Home4x3, Home4x4, Home4x5, Home5x3, Home5x4, Home5x5,
  HoneycombLines, Count
};
struct DesktopMenuItem {
  const char *id; const char *label; bool submenu=false;
  DesktopIcon icon=DesktopIcon::Settings;uint16_t color=0x630c;
  bool danger=false;
  DesktopPreferenceAction preference=DesktopPreferenceAction::None;
  // Optional product-owned 38x38 alpha mask; borrowed immutable lifetime.
  const uint8_t *glyph=nullptr;
  bool toggle=false;
  const char *caption=nullptr;
  constexpr DesktopMenuItem(const char *action,const char *name,bool child=false,
      DesktopIcon glyph=DesktopIcon::Settings,uint16_t tint=0x630c,bool destructive=false,
      DesktopPreferenceAction setting=DesktopPreferenceAction::None,const uint8_t *mask=nullptr,bool is_toggle=false,const char *detail=nullptr)
      :id(action),label(name),submenu(child),icon(glyph),color(tint),danger(destructive),preference(setting),glyph(mask),toggle(is_toggle),caption(detail){}
};
struct DesktopMenu {
  const char *id; const char *title; const DesktopMenuItem *items; size_t count;
  DesktopMenuLayout layout=DesktopMenuLayout::List;
  bool outline_icons=false;
  constexpr DesktopMenu():DesktopMenu(nullptr,nullptr,nullptr,0){}
  constexpr DesktopMenu(const char *menu_id,const char *name,const DesktopMenuItem *entries,size_t length,
      DesktopMenuLayout format=DesktopMenuLayout::List,bool outlines=false)
      :id(menu_id),title(name),items(entries),count(length),layout(format),outline_icons(outlines){}
};
struct DesktopMenuIdentity {
  char title[96]{};
  std::shared_ptr<const DesktopImage> image;
};
struct DesktopMenuValue {
  const char *id=nullptr;
  char text[40]{}; // A WiFi SSID is at most 32 bytes.
  // Keep account metadata out of every stack-resident shell copy. A published
  // identity is immutable so rendering retains the previous frame's identity.
  std::shared_ptr<const DesktopMenuIdentity> identity;
};
struct DesktopShell {
  static constexpr size_t capacity = 32;
  const DesktopApplication *apps = nullptr;
  size_t count = 0, focus = 0;
  // Borrowed boot-lifetime system route; nullptr disables settings navigation.
  const char *settings_id = "os.settings";
  const DesktopMenu *menus=nullptr;
  size_t menu_count=0;
  const DesktopMenu *active_menu=nullptr;
  const DesktopMenu *leaf_menu=nullptr;
  uint8_t leaf_row=0;
  uint32_t menu_configuration_generation=0;
  DesktopMenuValue menu_values[8]{};
  const char *action=nullptr;
  DesktopPreferences preferences{};
  uint8_t grid_columns=2,grid_rows=2;
  uint8_t honeycomb_slots[capacity]{}; // Computed only when the catalog changes.
  HomeWorkspace home;
  CompactWorkspace compact;
  size_t launcher_page_size() const { return preferences.layout==DesktopLayout::Grid?size_t(grid_columns)*grid_rows:1; }
  std::shared_ptr<const DesktopImage> wallpaper_image;
  DesktopSurface surface = DesktopSurface::Launcher;
  HomeNavigation home_navigation;
  EdgeNavigation edge_navigation;
  StatusNavigation status_navigation;
  DesktopSurface navigation_surface = DesktopSurface::Launcher;
  uint8_t row = 0;
  uint32_t unread[capacity]{}, todo[capacity]{};
  DesktopPoint camera{};
  bool touch_enabled = false, keys_enabled = true;
  bool preferences_changed = false;
  bool preferences_save_failed = false;
  bool surface_requested = false;
  std::shared_ptr<const DesktopSurfaceState> surface_state;
  // Composition publishes a new table; frames retain immutable bindings.
  std::shared_ptr<const DesktopSurfaceRegistry> surface_registry;
  // App-owned immutable state that must survive visits to another surface.
  std::shared_ptr<const DesktopSurfaceState> retained_surfaces[static_cast<size_t>(DesktopSurface::Count)];
  uint32_t surface_generation=0;
  int launch = -1;
  bool contact = false, blocked_contact = false, dragged = false;
  DesktopPoint start{}, last{}, camera_start{};
  uint8_t list_offset = 0;
  // Explicit input-owner clock; all animation state is copied with the frame.
  bool clock_ready = false, moving = false, page_drag = false;
  uint32_t now_ms = 0, motion_started = 0, frame_ms = 0;
  uint32_t contact_started_ms = 0;
  size_t target_focus = 0, source_page = 0, destination_page = 0;
  DesktopPoint camera_from{}, camera_target{};
  float page_offset = 0, offset_from = 0, offset_target = 0;
  int page_direction = 1;
  bool joystick_panning=false,joystick_armed=false;
  uint32_t joystick_ms=0;
  DesktopPoint joystick_velocity{};
  bool tick(uint32_t milliseconds);
  void navigate(size_t app, int direction = 0);
  bool grid_menu() const { return surface==DesktopSurface::Actions&&active_menu&&active_menu->layout==DesktopMenuLayout::Grid2x2; }
  bool paged_menu() const { return grid_menu()&&active_menu->count>4; }
  void settle_motion();
  // Analogue launcher camera: pan while held, attract to the nearest tile only
  // after returning to centre. Called exclusively by the existing input owner.
  bool joystick(int x,int y,uint32_t now);
  bool pan_honeycomb(DesktopPoint anchor,float dx,float dy);
  bool launcher_visible(size_t index) const {if constexpr(lunaros::graphics::metrics::native)return index<count;return index<count&&compact.visible(apps[index].id);}
  void reset_joystick();

  bool configure(const DesktopApplication *declarations, size_t size);
  bool replace_applications(const DesktopApplication *declarations, size_t size);
  bool configure_menus(const DesktopMenu *declarations,size_t size);
  size_t find_app(const char *id) const;
  bool activate(const char *id);
  bool restore_startup_selection(bool defer_launch);
  bool open_menu(const char *id);
  bool set_menu_value(const char *id,const char *text);
  bool set_menu_identity(const char *id,const char *title,std::shared_ptr<const DesktopImage> image);
  // The title is borrowed only during this call. A changed identity publishes
  // its own bounded title and image; earlier frame snapshots stay immutable.
  bool set_menu_identity(const char *id,std::string_view title,std::shared_ptr<const DesktopImage> image);
  const DesktopMenuIdentity *menu_identity(const char *id) const;
  const char *menu_value(const char *id) const;
  bool menu_selected(const DesktopMenuItem &) const;
  const char *take_action() {const char *value=action;action=nullptr;return value;}
  // A temporarily incomplete launcher must not erase a saved startup app.
  void set_preferences(DesktopPreferences value, bool preserve_missing_startup = false);
  void set_badge(size_t app, uint32_t messages, uint32_t pending);
  void select(size_t app);
  void cancel_contact();
  bool command(DesktopCommand command, bool system_source = false);
  bool activate_row(size_t item);
  bool apply_widget_action(lunaros::graphics::Action);
  bool pointer(bool pressed, bool valid, int x, int y, bool route_status=true);
  int take_launch();
  DesktopPoint point(size_t app) const;
  DesktopTile tile(size_t app) const;
  int launcher_top() const { return home.enabled?status_layout::height:preferences.auto_hide_status?0:status_layout::height; }
  DesktopPoint logical(int x, int y) const;
  const char *title() const;
  bool has_badge(size_t app) const;
private:
  void navigate_menu(size_t item);
  void nearest_focus();
};
// Native Launcher/status/navigation painter. Generic desktop widgets are
// projected by the OS graphics subsystem; the model has no backend dependency.
struct DesktopPainter {
  void *context = nullptr;
  void (*text)(void *, uint16_t *, int x, int y, const char *, int size, uint16_t color, bool antialias) = nullptr;
  int (*text_width)(void *, const char *, int size) = nullptr;
  // Relative visible ink box, supplied by the product font port.
  struct TextBounds { int left,top,width,height; };
  TextBounds (*text_bounds)(void *, const char *, int size) = nullptr;
  void (*clipped_text)(void *,uint16_t *,int x,int y,const char *,int size,uint16_t color,bool antialias,
                       int clip_x,int clip_y,int clip_width,int clip_height) = nullptr;
};
bool draw_launcher(const DesktopShell &, const DesktopStatus &, uint16_t *pixels,
                  DesktopPainter painter, bool indicator = true,
                  uint16_t *navigation_source = nullptr,
                  lunaros::graphics::Scene *renderer = nullptr);
DesktopPreferences load_desktop_preferences(const char *default_startup = nullptr);
bool save_desktop_preferences(DesktopPreferences value);
// App extensions declare their metadata and system menu placement.
size_t system_surfaces(const DesktopShell &, DesktopSurface *out,size_t capacity);
// Widget content identity belongs to the shell, independently of the painter
// backend. Queries do not mutate the shell or allocate an owning snapshot.
uint64_t shell_content_revision(const DesktopShell &);
} // namespace lunaredu::desktop
