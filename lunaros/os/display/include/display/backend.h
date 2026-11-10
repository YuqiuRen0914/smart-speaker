#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace lunaredu::display {

// Screen coordinates are logical, after the product has applied panel rotation.
struct ScreenRegion {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

enum class ScreenPixelFormat : uint8_t {
  Rgb565,
  // Existing Pet frames use this layout. New views should use Rgb565; only
  // the product backend decides how either layout reaches its panel.
  Rgb565ByteSwapped,
};

// Physical display and driver properties. Coordinates remain logical after
// rotation; alignment is expressed in those coordinates.
struct ScreenCapabilities {
  ScreenPixelFormat native_format = ScreenPixelFormat::Rgb565;
  uint8_t x_alignment = 1;
  uint8_t y_alignment = 1;
  bool partial_updates = true;
  bool asynchronous_transfer = false;
  bool tear_sync = false;
  bool hardware_frame_swap = false;
  // Transport setup cost expressed as equivalent payload bytes, used only to
  // merge bounded damage regions. Backends may provide a measured estimate.
  unsigned window_overhead_bytes=64;
};

struct ScreenFrameView {
  const uint16_t *pixels = nullptr;
  int width = 0;
  int height = 0;
  int stride = 0;
  ScreenPixelFormat format = ScreenPixelFormat::Rgb565;
  // Whole-frame completion retains the originating canvas lease. Physical
  // driver calls may omit it; consumers must reject unqualified receipts.
  const void *issuer = nullptr;
  uint64_t generation = 0;
  unsigned slot = 0;
};

// A tightly scoped source tile. Its top-left pixel maps to the destination
// region; callers do not need a second full-screen buffer for small updates.
struct ScreenPatchView {
  const uint16_t *pixels = nullptr;
  int width = 0;
  int height = 0;
  int stride = 0;
  ScreenPixelFormat format = ScreenPixelFormat::Rgb565;
  bool part_of_frame = false; // Whole-frame completion is notified separately.
};

struct ScreenBackend {
  void *context = nullptr;
  uint16_t width = 0;
  uint16_t height = 0;
  // The callback must finish reading source pixels before returning. It may
  // retain a private DMA copy until wait_idle completes.
  bool (*present)(void *, ScreenFrameView, ScreenRegion, std::string &) = nullptr;
  bool (*wait_idle)(void *, std::string &) = nullptr;
  bool (*present_patch)(void *, ScreenPatchView, ScreenRegion,
                        std::string &) = nullptr;
  ScreenCapabilities capabilities{};
  // Optional reverse port for an on-demand capture. The display owner binds
  // its invalidation callback; drivers do not create a polling/render task.
  void (*bind_redraw)(void *, void *, void (*)(void *)) = nullptr;
  // Runs on the display owner only after every region and final wait_idle succeeded.
  void (*frame_presented)(void *,ScreenFrameView,const ScreenRegion *,size_t)=nullptr;
};

} // namespace lunaredu::display
