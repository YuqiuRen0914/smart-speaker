#include "display/adapters/spi_lcd.h"
#include "lunaredu_device_spi_lcd_surface.h"
#include <esp_heap_caps.h>
#include <cstring>
#include <limits>
#include <lvgl/draw/lv_draw_utils.h>

namespace lunaredu::display {
using namespace lunaredu::device;
SpiLcdAdapter::~SpiLcdAdapter() { if (scratch_) heap_caps_free(scratch_); }
bool SpiLcdAdapter::prepare_surface(const SpiLcdSurfaceRequirements &requirements,std::string &error){
  if(requirements.initialize&&!device_spi_lcd_surface_setup()){
    error=requirements.setup_error;return false;
  }
  DeviceSpiLcdPanel panel;
  if(!requirements.width||!requirements.height||!device_spi_lcd_surface_get_panel(&panel)||
      panel.width!=requirements.width||panel.height!=requirements.height){
    error=requirements.dimensions_error;return false;
  }
  return prepare(panel,requirements.transfer,requirements.capabilities,error);
}
bool SpiLcdAdapter::prepare(const DeviceSpiLcdPanel &panel, SpiLcdTransfer mode,
    display::ScreenCapabilities capabilities, std::string &error) {
  if (prepared_) {
    if (panel.width != width_ || panel.height != height_ ||
        panel.controller != controller_ || mode != mode_ ||
        panel.rgb565_panel_byte_swap != panel_byte_swap_ ||
        capabilities.native_format!=capabilities_.native_format ||
        capabilities.x_alignment!=capabilities_.x_alignment || capabilities.y_alignment!=capabilities_.y_alignment ||
        capabilities.partial_updates!=capabilities_.partial_updates ||
        capabilities.asynchronous_transfer!=capabilities_.asynchronous_transfer ||
        capabilities.tear_sync!=capabilities_.tear_sync || capabilities.hardware_frame_swap!=capabilities_.hardware_frame_swap ||
        capabilities.window_overhead_bytes!=capabilities_.window_overhead_bytes) {
      error = "screen_backend_reconfiguration_required"; return false;
    }
    error.clear(); return true;
  }
  if (!panel.width || !panel.height || size_t(panel.width) >
      std::numeric_limits<size_t>::max() / panel.height / sizeof(uint16_t)) {
    error = "screen_panel_unavailable"; return false;
  }
  if (mode == SpiLcdTransfer::Packed) {
    scratch_ = static_cast<uint16_t *>(heap_caps_malloc(
        size_t(panel.width) * panel.height * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!scratch_) { error = "screen_staging_allocation_failed"; return false; }
  }
  width_ = panel.width; height_ = panel.height; controller_ = panel.controller;
  panel_byte_swap_ = panel.rgb565_panel_byte_swap; mode_ = mode; capabilities_ = capabilities; prepared_ = true;
  error.clear(); return true;
}
display::ScreenBackend SpiLcdAdapter::backend() {
  return {this, width_, height_, present, wait, present_patch, capabilities_,
    [](void *,void *context,void (*request)(void *)){device_spi_lcd_surface_bind_redraw(context,request);}};
}
bool SpiLcdAdapter::wait(void *, std::string &error) {
  return device_spi_lcd_surface_wait_idle(&error);
}
bool SpiLcdAdapter::present(void *context, display::ScreenFrameView frame,
    display::ScreenRegion region, std::string &error) {
  auto &port = *static_cast<SpiLcdAdapter *>(context);
  if (!port.prepared_ || !frame.pixels || frame.stride < frame.width ||
      frame.width != port.width_ || frame.height != port.height_ ||
      frame.stride > UINT16_MAX || region.x < 0 || region.y < 0 ||
      region.width <= 0 || region.height <= 0 ||
      region.x > frame.width - region.width || region.y > frame.height - region.height) {
    error = "invalid_screen_frame_region"; return false;
  }
  const auto *source = frame.pixels + size_t(region.y) * frame.stride + region.x;
  return present_patch(context,{source,region.width,region.height,frame.stride,frame.format,true},region,error);
}
bool SpiLcdAdapter::present_patch(void *context,ScreenPatchView patch,ScreenRegion region,std::string &error){
  auto &port=*static_cast<SpiLcdAdapter *>(context);
  if(port.active_&&!port.active_(port.guard_context_)){error="display_lease_inactive";return false;}
  if(!port.prepared_||!patch.pixels||patch.width!=region.width||patch.height!=region.height||
      patch.stride<patch.width||patch.stride>UINT16_MAX||region.x<0||region.y<0||
      region.width<=0||region.height<=0||region.x>int(port.width_)-region.width||
      region.y>int(port.height_)-region.height){error="invalid_screen_patch_region";return false;}
  const auto *source=patch.pixels;
  if (port.mode_ == SpiLcdTransfer::Strided)
    return device_spi_lcd_surface_draw_rgb565_strided_bitmap(region.x, region.y,
        region.width, region.height, source, patch.stride,
        patch.format == display::ScreenPixelFormat::Rgb565ByteSwapped, &error);
  const bool canonical = !port.panel_byte_swap_;
  const bool convert = (patch.format == display::ScreenPixelFormat::Rgb565) != canonical;
  for (int row = 0; row < region.height; ++row) {
    auto *to = port.scratch_ + size_t(row) * region.width;
    const auto *from = source + size_t(row) * patch.stride;
    std::memcpy(to, from, size_t(region.width) * sizeof(uint16_t));
  }
  if(convert)lv_draw_rgb565_swap(port.scratch_,uint32_t(region.width)*region.height);
  return device_spi_lcd_surface_draw_rgb565_bitmap(region.x, region.y,
      region.width, region.height, port.scratch_, !canonical, &error);
}
} // namespace lunaredu::display
