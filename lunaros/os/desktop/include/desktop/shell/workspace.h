#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace lunaredu::desktop {
struct DesktopShell;
enum class HomeItemKind : uint8_t { Application, Widget };
struct HomeItem {
  std::string id;
  HomeItemKind kind=HomeItemKind::Application;
  uint8_t page=0,column=0,row=0,width=1,height=1;
};
struct HomeLayout {
  static constexpr unsigned max_pages=8,max_items=48;
  uint8_t columns=4,rows=4,pages=1;
  std::vector<HomeItem> items;
  std::vector<std::string> hidden;
};
struct HomeWidgetContent {
  char value[64]{},detail[80]{};
  uint16_t color=0x2945;
  bool available=true;
};
// App-owned static declarations. Capture is bounded, read-only and never starts
// a VM or hardware producer. OS invokes it only for visible desktop widgets.
enum class HomeWidgetRoute : uint8_t { Application, Settings };
struct HomeWidgetDefinition {
  const char *id=nullptr,*application=nullptr,*title=nullptr;
  uint8_t width=2,height=1;
  HomeWidgetContent (*capture)()=nullptr;
  uint32_t interval_ms=1000;
  // Zero means fixed height; alternate sizes are App-owned capabilities.
  uint8_t alternate_height=0;
  HomeWidgetRoute route=HomeWidgetRoute::Application;
};
struct HomeWidgetFrame { std::string id;HomeWidgetContent content;uint32_t captured_ms=0; };
enum class HomeMode : uint8_t { Desktop, AllApps, Edit, Widgets, ItemActions, Pages, Appearance, Uninstall, DrawerActions, PageActions };
struct HomeBox { int x=0,y=0,width=0,height=0; };
struct HomeDensity { const char *label;uint8_t columns,rows; };
inline constexpr std::array<HomeDensity,9> home_densities{{
  {"3 × 3",3,3},{"3 × 4",3,4},{"3 × 5",3,5},
  {"4 × 3",4,3},{"4 × 4",4,4},{"4 × 5",4,5},
  {"5 × 3",5,3},{"5 × 4",5,4},{"5 × 5",5,5}
}};
// Geometry is compiled for every supported density, not recalculated per frame.
HomeBox home_cell(unsigned columns,unsigned rows,bool status,unsigned column,unsigned row,unsigned width=1,unsigned height=1);
const HomeWidgetDefinition *system_home_widgets(size_t &count);
std::string encode_home_layout(const HomeLayout &);
bool decode_home_layout(const std::string &,HomeLayout &);
bool valid_home_layout(const HomeLayout &);
bool save_home_layout(const HomeLayout &);
bool load_home_layout(HomeLayout &,bool &found);

struct HomeWorkspace {
  bool enabled=false,dirty=false,save_failed=false;
  std::shared_ptr<const HomeLayout> layout;
  const HomeWidgetDefinition *widgets=nullptr;
  size_t widget_count=0;
  std::shared_ptr<const std::vector<HomeWidgetFrame>> frames;
  HomeMode mode=HomeMode::Desktop;
  uint8_t page=0,panel_page=0,return_page=0;
  int drawer_selected=-1;
  int selected=-1,drag_item=-1,pressed_item=-1;
  float drag_x=0,drag_y=0,offset=0,offset_from=0,offset_to=0;
  uint8_t source_page=0,destination_page=0;
  bool contact=false,dragging=false,swiping=false,animating=false,long_pressed=false;
  int start_x=0,start_y=0,last_x=0,last_y=0;
  uint32_t started=0,edge_started=0,motion_started=0,last_capture=0,save_attempt_ms=0;
  std::string error,pending_uninstall;
  void configure(DesktopShell &,const HomeWidgetDefinition *,size_t,const HomeLayout *saved=nullptr,bool read_failed=false,bool catalog_ready=true);
  void reconcile(const DesktopShell &,bool catalog_ready=true);
  bool tick(DesktopShell &,uint32_t);
  bool pointer(DesktopShell &,bool,bool,int,int);
  bool command(DesktopShell &,unsigned);
  void cancel();
  bool place(const HomeItem &,int replace=-1,bool swap=true);
  bool remove(size_t,bool hide=true);
  bool add_application(const char *,unsigned preferred_page);
  bool add_widget(const char *);
  bool density(unsigned,unsigned);
  bool add_page();
  bool delete_page(unsigned);
  bool move_page(unsigned,unsigned);
  bool arrange();
  void turn(DesktopShell &,int,bool animate=true);
  HomeBox box(const DesktopShell &,const HomeItem &,bool moving=true) const;
  HomeItem drop_target(int x,int y) const;
  int hit(const DesktopShell &,int,int) const;
  bool visible(unsigned) const;
  const HomeWidgetDefinition *definition(const char *) const;
  const HomeWidgetContent *content(const char *) const;
  bool persist(bool (*save)(const HomeLayout &));
};
}
