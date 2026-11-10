#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lunaredu::desktop {
struct DesktopShell;
// Stable IDs only: the regular hex geometry and category sectors remain OS
// compile-time data. Hidden shortcuts are still installed applications.
struct CompactLayout {
  std::vector<std::string> order,hidden;
};
enum class CompactMode : uint8_t { Desktop,Edit,Menu,ItemActions,AllApps,AppActions,Appearance,Uninstall };
bool valid_compact_layout(const CompactLayout &);
std::string encode_compact_layout(const CompactLayout &);
bool decode_compact_layout(const std::string &,CompactLayout &);
bool save_compact_layout(const CompactLayout &);
bool load_compact_layout(CompactLayout &,bool &found);
struct CompactWorkspace {
  bool enabled=false,dirty=false,save_failed=false;
  std::shared_ptr<const CompactLayout> layout;
  CompactMode mode=CompactMode::Desktop;
  CompactMode uninstall_return=CompactMode::ItemActions;
  unsigned page=0,row=0;
  std::string selected,pending_uninstall,error;
  bool contact=false,dragging=false,long_pressed=false;
  int start_x=0,start_y=0,last_x=0,last_y=0,pressed_app=-1;
  float camera_x=0,camera_y=0;
  uint32_t started=0,save_attempt_ms=0;
  uint32_t edge_started=0,edge_step_ms=0;
  void configure(DesktopShell &,const CompactLayout *saved=nullptr,bool read_failed=false,bool catalog_ready=true);
  void reconcile(DesktopShell &,bool catalog_ready=true);
  void assign_slots(DesktopShell &) const;
  bool visible(const char *) const;
  bool hide(DesktopShell &,const char *);
  bool restore(DesktopShell &,const char *);
  bool reorder(DesktopShell &,const char *,const char *);
  void cancel();
  bool tick(DesktopShell &,uint32_t);
  bool pointer(DesktopShell &,bool,bool,int,int);
  bool command(DesktopShell &,unsigned);
  bool persist(bool (*save)(const CompactLayout &));
  void choose(DesktopShell &,unsigned);
};
}
