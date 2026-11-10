#pragma once
#include "lunaredu_pet_ui.h"
#include "lunaredu_pet_frame_region.h"
#include "lunaredu_qr.h"
#include "lunaros_text_coverage_cache.h"
#include <array>
#include <tuple>
#include <vector>
namespace lunaredu::pet {
struct UiControl { UiAction action=UiAction::None; FrameRegion bounds{}; const char *label=""; bool toggle=false,checked=false; };
struct UiControls { std::array<UiControl,12> items{}; size_t count=0; };
struct UiDecoration {
  std::string shape,color;
  bool autonomous_appearance_enabled=true;
  bool connection_open=false,connection_busy=false;
  bool connection_next=false;
  std::string connection_title,connection_hint,connection_url;
  bool operator==(const UiDecoration &v) const {
    return std::tie(shape,color,autonomous_appearance_enabled,connection_open,connection_busy,connection_next,connection_title,connection_hint,connection_url)==
      std::tie(v.shape,v.color,v.autonomous_appearance_enabled,v.connection_open,v.connection_busy,v.connection_next,v.connection_title,v.connection_hint,v.connection_url);
  }
};
UiControls ui_controls(const UiSnapshot &,int width,int height,const UiDecoration &);
UiAction ui_hit(const UiControls &,int x,int y);
// Same layout drives drawing and hit testing, in the supplied RGB565 order.
class UiRenderer {
public:
  static FrameRegion character_region(const UiSnapshot &,int width,int height);
  static std::array<FrameRegion,2> fullscreen_overlay_bands(int width,int height);
  void paint(PetFrameBuffer,const UiSnapshot &,const UiDecoration &,const UiControls * = nullptr,
             const FrameRegion *clip = nullptr);
  void reset();
  void cache_text(bool enabled){cache_text_=enabled;if(!enabled)text_cache_.reset();}
  size_t text_cache_bytes()const{return text_cache_.bytes();}
private:
  lunaros::text_raster::TextCoverageCache text_cache_;
  bool cache_text_=true;
  lunaredu::lunaros::QrCode qr_;
  std::string qr_payload_;
  std::string caption_text_;
  std::vector<std::string> caption_lines_;
  std::vector<size_t> caption_line_ends_;
  int caption_width_=0;
  unsigned caption_font_=0;
};
} // namespace lunaredu::pet
