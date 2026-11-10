#include "display/screen_overlay_surface.h"
#include "display/pipeline/geometry.h"
#include <cstring>
#include <limits>
#include <cstdlib>

namespace lunaredu::display {
ScreenOverlaySurface::~ScreenOverlaySurface() {
  if(surface_.active() || surface_.retained()) std::abort();
  reset_unstarted();
}
bool ScreenOverlaySurface::configure(std::initializer_list<ScreenRegion> regions,
                                    std::string &error) {
  if (storage_bound_ || surface_.active() || surface_.retained() || regions.size() > layers_.size() ||
      !surface_.memory().allocate || !surface_.memory().free) {
    error = "overlay_surface_configuration_invalid";
    return false;
  }
  if (!surface_.frame(0)) { error = "overlay_surface_frame_missing"; return false; }
  if (!composer_.configure(surface_.width(), surface_.height(),
      surface_.capabilities().native_format,
      {this, [](void *opaque, const ScreenRegion *regions, size_t count,
                    const uint16_t *pixels, uint16_t stride, std::string &failure) {
        auto &self=*static_cast<ScreenOverlaySurface *>(opaque);
        if(pixels!=self.borrowed_.pixels||stride!=self.borrowed_.stride){failure="overlay_frame_lease_invalid";return false;}
        if(!self.surface_.present(regions,count,self.borrowed_,failure))return false;
        self.borrowed_={};return true;
      }}, error)) return false;
  surface_.retain_overlay();storage_bound_=true;
  composer_.set_frame(surface_.frame(0));
  memory_=surface_.memory();
  const auto memory = memory_;
  size_t index = 0;
  for (const auto &region : regions) {
    // Validate before allocation; panel dimensions bound byte multiplication.
    if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
        region.width > surface_.width() || region.height > surface_.height() ||
        region.x > surface_.width() - region.width ||
        region.y > surface_.height() - region.height ||
        size_t(region.width) > std::numeric_limits<size_t>::max() / region.height / sizeof(uint32_t)) {
      error = "invalid_overlay_layer";
      reset_unstarted();
      return false;
    }
    const size_t bytes = size_t(region.width) * region.height * sizeof(uint32_t);
    if(!surface_.reserve_overlay(bytes,error)){reset_unstarted();return false;}
    allocated_bytes_+=bytes;
    auto *pixels = static_cast<uint32_t *>(memory.allocate(memory.context, bytes));
    if (!pixels) {
      error = "overlay_allocation_failed";
      reset_unstarted();
      return false;
    }
    layers_[index++] = pixels;
    std::memset(pixels, 0, bytes);
    if (!composer_.add_layer(region, pixels, error)) {
      reset_unstarted();
      return false;
    }
  }
  error.clear();
  return true;
}
void ScreenOverlaySurface::reset_unstarted() {
  if (surface_.retained() || surface_.active()) return;
  composer_ = {};
  rendering_ = false;
  borrowed_ = {};
  ownership_generation_=0;
  const auto memory = memory_;
  for (auto &layer : layers_) {
    if (layer && memory.free) memory.free(memory.context, layer);
    layer = nullptr;
  }
  surface_.release_overlay(allocated_bytes_);allocated_bytes_=0;memory_={};
  if(storage_bound_){surface_.release_overlay_binding();storage_bound_=false;}
}
bool ScreenOverlaySurface::wait_edit(std::string &error) {
  if (surface_.asynchronous()) return surface_.wait_all(error);
  error.clear();
  return true;
}
uint16_t *ScreenOverlaySurface::begin_frame(std::string &error) {
  if (!surface_.active()) return nullptr;
  if(!borrow_frame(error))return nullptr;
  rendering_ = true;
  return composer_.begin_frame();
}
bool ScreenOverlaySurface::borrow_frame(std::string &error) {
  if(ownership_generation_!=surface_.ownership_generation()){
    ownership_generation_=surface_.ownership_generation();
    borrowed_={};rendering_=false;composer_.request_full_refresh();
  }
  if(surface_.holds_frame(borrowed_)){error.clear();return true;}
  borrowed_={};rendering_=false;
  auto next=surface_.begin_frame(error);
  if(!next.pixels)return false;
  // Overlay-only frames preserve the last foreground raster when slots alternate.
  if(composer_.frame()&&composer_.frame()!=next.pixels)
    std::memcpy(next.pixels,composer_.frame(),size_t(surface_.width())*surface_.height()*sizeof(uint16_t));
  borrowed_=next;composer_.set_frame(next.pixels);return true;
}
bool ScreenOverlaySurface::present(ScreenRegion damage, std::string &error) {
  if (!surface_.active() || !frame_active()) return true;
  ScreenRegion aligned;
  // Expanded edges can intersect overlays outside the renderer's damage.
  if (!align_screen_region(damage, surface_.width(), surface_.height(),
      surface_.capabilities(), aligned, error) || !composer_.present(aligned, error)) return false;
  rendering_ = false;
  return true;
}
bool ScreenOverlaySurface::flush(std::string &error) {
  if (!surface_.active()) { error.clear(); return true; }
  if (rendering_ && surface_.holds_frame(borrowed_)) { error.clear(); return true; }
  rendering_=false;
  if(!composer_.overlays_pending()||!frame_active()){error.clear();return true;}
  return wait_edit(error) && borrow_frame(error) && composer_.flush_overlays(error);
}
bool ScreenOverlaySurface::clear(std::string &error) {
  if (!surface_.active()) { rendering_ = false; error.clear(); return true; }
  if (rendering_ && surface_.holds_frame(borrowed_)) { error = "display_frame_in_progress"; return false; }
  rendering_=false;
  return wait_edit(error) && borrow_frame(error) && composer_.clear(error);
}
}
