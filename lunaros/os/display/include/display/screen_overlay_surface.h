#pragma once
#include "display/screen_surface.h"
#include "display/pipeline/damage_set.h"
#include "display/internal/overlay_composer.h"
#include <initializer_list>

namespace lunaredu::display {
// Retained transparent layers over a ScreenSurface. Owns allocation, safe
// edits, damage alignment and transfer coordination; callers own layout and
// drawing and serialize access on their existing presentation executor.
class ScreenOverlaySurface {
public:
  explicit ScreenOverlaySurface(ScreenSurface &surface) : surface_(surface) {}
  ~ScreenOverlaySurface();
  ScreenOverlaySurface(const ScreenOverlaySurface &) = delete;
  ScreenOverlaySurface &operator=(const ScreenOverlaySurface &) = delete;
  bool configure(std::initializer_list<ScreenRegion>, std::string &error);
  void reset_unstarted();
  bool wait_edit(std::string &error);
  uint32_t *layer(size_t index) const { return index < layers_.size() ? layers_[index] : nullptr; }
  void set_layer_visible(size_t index, bool visible) { composer_.set_layer_visible(index, visible); }
  void mark_layer_dirty(size_t index) { composer_.mark_layer_dirty(index); }
  void request_full_refresh() { composer_.request_full_refresh(); }
  bool ready() const { return surface_.ready() && storage_bound_; }
  bool frame_active() const { return ready() && composer_.frame_active(); }
  uint16_t *frame() const { return ready() ? composer_.frame() : nullptr; }
  uint16_t *begin_frame(std::string &error);
  bool present(ScreenRegion, std::string &error);
  bool flush(std::string &error);
  bool clear(std::string &error);
private:
  ScreenSurface &surface_;
  FrameCanvas borrowed_;
  bool borrow_frame(std::string &);
  ScreenMemory memory_;
  size_t allocated_bytes_=0;
  bool storage_bound_=false;
  uint64_t ownership_generation_=0;
  ScreenReservedOverlayComposer composer_;
  std::array<uint32_t *, kScreenBatchMaxRegions> layers_{};
  // The foreground renderer owns the canvas from begin_frame to present;
  // overlay-only ticks may update layers but cannot submit this canvas yet.
  bool rendering_ = false;
};
}
