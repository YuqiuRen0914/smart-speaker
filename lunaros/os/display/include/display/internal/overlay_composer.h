#pragma once
#include "display/backend.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "display/pipeline/damage_set.h"
namespace lunaredu::display {
struct ScreenBatchPresenter {
  void *context = nullptr;
  bool (*present)(void *, const ScreenRegion *, size_t count,
                  const uint16_t *frame, uint16_t stride, std::string &) = nullptr;
};

// Composes small ARGB8888 overlays into reserved black areas of an RGB565
// frame, then submits bounded damage. Canvas allocation and drawing belong
// to the caller; this class owns only frame and transfer state.
class ScreenReservedOverlayComposer {
 public:
  bool configure(int width, int height, ScreenPixelFormat,
                 ScreenBatchPresenter, std::string &error);
  bool add_layer(ScreenRegion region, const uint32_t *pixels,
                 std::string &error);
  void set_frame(uint16_t *pixels) { frame_ = pixels; }
  uint16_t *frame() const { return frame_; }
  uint16_t *begin_frame();
  bool frame_active() const { return frame_active_; }
  void set_layer_visible(size_t index, bool visible);
  void mark_layer_dirty(size_t index);
  bool overlays_pending() const;
  void request_full_refresh();
  bool flush_overlays(std::string &error);
  bool present(ScreenRegion damage, std::string &error);
  bool clear(std::string &error);

 private:
  bool configure_frame(int width, int height, ScreenPixelFormat, std::string &error);
  struct Layer {
    ScreenRegion region{};
    const uint32_t *pixels = nullptr;
    bool visible = true;
    bool dirty = true;
  };
  void compose_layer(const Layer &layer);
  bool submit(const ScreenDamageSet &damage, std::string &error) const;

  std::array<Layer, kScreenBatchMaxRegions> layers_{};
  size_t layer_count_ = 0;
  int width_ = 0;
  int height_ = 0;
  ScreenPixelFormat format_ = ScreenPixelFormat::Rgb565;
  ScreenBatchPresenter presenter_;
  uint16_t *frame_ = nullptr;
  bool frame_active_ = false;
  bool full_refresh_pending_ = true;
};

}
