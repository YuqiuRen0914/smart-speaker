#pragma once
#include "desktop/ui/status_bar.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace lunaredu::desktop {
// Drawing-owner cache of the opaque, compiled status band. Borrowed frame
// storage never escapes. A zero font revision bypasses unready/mutable fonts.
class StatusBarRasterCache {
 public:
  struct Memory {void *(*allocate)(size_t)=nullptr;void (*release)(void *)=nullptr;};
  struct Stats {uint32_t hits=0,misses=0,bypasses=0;size_t bytes=0;};
  StatusBarRasterCache();
  explicit StatusBarRasterCache(Memory memory):memory_(memory){}
  ~StatusBarRasterCache(){reset();}
  StatusBarRasterCache(const StatusBarRasterCache &)=delete;
  StatusBarRasterCache &operator=(const StatusBarRasterCache &)=delete;
  void reset();
  // Native text is clipped by LVGL to the opaque band. Revision0 and oversized
  // cache keys bypass storage, but still draw through the same native path.
  bool paint(lunaros::graphics::Scene &,lunaros::graphics::CanvasSpec,const char *,
             const DesktopStatus &,uint16_t *,bool,const char *,uint32_t font_revision);
  bool paint_hidden(lunaros::graphics::Scene &,lunaros::graphics::CanvasSpec,uint16_t *,bool);
  Stats stats()const{return stats_;}
 private:
  struct Key {
    std::array<char,128> title{},detail{};
    const void *renderer=nullptr;
    uint32_t font=0;
    unsigned flags=0;
    int battery=-1;
    bool operator==(const Key &)const;
  } key_;
  static bool prepare(Key &,const void *,const char *,const DesktopStatus &,bool,const char *,uint32_t);
  Memory memory_;
  uint16_t *pixels_=nullptr;
  bool attempted_=false,valid_=false,hidden_=false;
  Stats stats_;
};
}
