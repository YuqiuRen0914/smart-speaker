#include "display/pipeline/damage_set.h"
#include "display/internal/overlay_composer.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <lvgl/draw/lv_draw_utils.h>
#include <lvgl/draw/lv_color.h>

#include "screen_geometry_internal.h"

namespace lunaredu::display {

using namespace screen_detail;

bool ScreenReservedOverlayComposer::configure(int width, int height,
    ScreenPixelFormat format, ScreenBatchPresenter presenter, std::string &error) {
  if (!presenter.present) { error = "invalid_overlay_composer"; return false; }
  if (!configure_frame(width, height, format, error)) return false;
  presenter_ = presenter;
  return true;
}

bool ScreenReservedOverlayComposer::configure_frame(
    int width, int height, ScreenPixelFormat format, std::string &error) {
  if (width <= 0 || height <= 0 || width > UINT16_MAX || height > UINT16_MAX) {
    error = "invalid_overlay_composer";
    return false;
  }
  width_ = width;
  height_ = height;
  format_ = format;
  presenter_ = {};
  layer_count_ = 0;
  frame_ = nullptr;
  frame_active_ = false;
  full_refresh_pending_ = true;
  error.clear();
  return true;
}

bool ScreenReservedOverlayComposer::add_layer(
    ScreenRegion region, const uint32_t *pixels, std::string &error) {
  if (layer_count_ == layers_.size() || pixels == nullptr ||
      region.width <= 0 || region.height <= 0 ||
      !valid_region(region, width_, height_)) {
    error = "invalid_overlay_layer";
    return false;
  }
  layers_[layer_count_++] = {region, pixels, true, true};
  error.clear();
  return true;
}

uint16_t *ScreenReservedOverlayComposer::begin_frame() {
  if (frame_ == nullptr) return nullptr;
  frame_active_ = true;
  return frame_;
}

void ScreenReservedOverlayComposer::set_layer_visible(size_t index,
                                                       bool visible) {
  if (index >= layer_count_ || layers_[index].visible == visible) return;
  layers_[index].visible = visible;
  layers_[index].dirty = true;
}

void ScreenReservedOverlayComposer::mark_layer_dirty(size_t index) {
  if (index < layer_count_) layers_[index].dirty = true;
}

bool ScreenReservedOverlayComposer::overlays_pending() const {
  for (size_t index = 0; index < layer_count_; ++index)
    if (layers_[index].dirty) return true;
  return false;
}

void ScreenReservedOverlayComposer::request_full_refresh() {
  full_refresh_pending_ = true;
  for (size_t index = 0; index < layer_count_; ++index)
    layers_[index].dirty = true;
}

void ScreenReservedOverlayComposer::compose_layer(const Layer &layer) {
  const auto region = layer.region;
  for (int y = 0; y < region.height; ++y) {
    auto *destination = frame_ +
        static_cast<size_t>(region.y + y) * width_ + region.x;
    std::fill_n(destination, region.width, uint16_t{0});
    if (!layer.visible) continue;
    const auto *source = layer.pixels + static_cast<size_t>(y) * region.width;
    for (int x = 0; x < region.width; ++x) {
      const uint32_t argb = source[x];
      const unsigned alpha = argb >> 24;
      if (alpha == 0) continue;
      const unsigned red = ((argb >> 16) & 0xffU) * alpha / 255U;
      const unsigned green = ((argb >> 8) & 0xffU) * alpha / 255U;
      const unsigned blue = (argb & 0xffU) * alpha / 255U;
      destination[x] = lv_color_to_u16(lv_color_make(uint8_t(red),uint8_t(green),uint8_t(blue)));
    }
    if(format_==ScreenPixelFormat::Rgb565ByteSwapped)
      lv_draw_rgb565_swap(destination,uint32_t(region.width));
  }
}

bool ScreenReservedOverlayComposer::submit(const ScreenDamageSet &damage,
                                            std::string &error) const {
  if (damage.count() == 0) { error.clear(); return true; }
  std::string detail;
  if (!presenter_.present(presenter_.context, damage.regions(), damage.count(),
                         frame_, static_cast<uint16_t>(width_), detail)) {
    error = "display_regions_present_failed:" + detail;
    return false;
  }
  error.clear();
  return true;
}

bool ScreenReservedOverlayComposer::flush_overlays(std::string &error) {
  if (!frame_active_ || frame_ == nullptr) return true;
  ScreenDamageSet damage;
  if (!damage.configure(width_, height_, error)) return false;
  for (size_t index = 0; index < layer_count_; ++index) {
    auto &layer = layers_[index];
    if (!layer.dirty) continue;
    compose_layer(layer);
    if (!damage.add(layer.region)) {
      error = "display_region_out_of_bounds";
      return false;
    }
  }
  if (!submit(damage, error)) return false;
  for (size_t index = 0; index < layer_count_; ++index)
    layers_[index].dirty = false;
  return true;
}

bool ScreenReservedOverlayComposer::present(ScreenRegion region,
                                             std::string &error) {
  if (!frame_active_ || frame_ == nullptr) return true;
  if (!valid_region(region, width_, height_)) {
    error = "display_region_out_of_bounds";
    return false;
  }
  ScreenDamageSet source_damage;
  ScreenDamageSet transfer_damage;
  if (!source_damage.configure(width_, height_, error) ||
      !transfer_damage.configure(width_, height_, error) ||
      !source_damage.add(region)) {
    error = "display_region_out_of_bounds";
    return false;
  }
  for (size_t index = 0; index < layer_count_; ++index) {
    const auto &layer = layers_[index];
    if (full_refresh_pending_ || layer.dirty ||
        source_damage.intersects(layer.region))
      compose_layer(layer);
  }
  if (full_refresh_pending_) {
    transfer_damage.fill();
  } else {
    if (!transfer_damage.add(region)) {
      error = "display_region_out_of_bounds";
      return false;
    }
    for (size_t index = 0; index < layer_count_; ++index)
      if (layers_[index].dirty &&
          !transfer_damage.add(layers_[index].region)) {
        error = "display_region_out_of_bounds";
        return false;
      }
  }
  if (!submit(transfer_damage, error)) return false;
  full_refresh_pending_ = false;
  for (size_t index = 0; index < layer_count_; ++index)
    layers_[index].dirty = false;
  return true;
}

bool ScreenReservedOverlayComposer::clear(std::string &error) {
  frame_active_ = false;
  request_full_refresh();
  if (frame_ == nullptr) { error.clear(); return true; }
  std::memset(frame_, 0,
              static_cast<size_t>(width_) * height_ * sizeof(uint16_t));
  ScreenDamageSet full;
  if(!full.configure(width_,height_,error))return false;
  full.fill();return submit(full,error);
}

} // namespace lunaredu::display
