#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include "graphics/canvas_spec.h"
#include "graphics/raster_coherence.h"
#include "graphics/animation.h"
#include "lunaros_asset_icons_mask.h"
namespace lunaredu::lunaros::graphics {
struct Rect { int x, y, width, height; };
enum class Align { Left, Center, Right };
enum class FocusCommand:uint8_t { Next, Previous, Activate };
struct Action { uint64_t epoch=0; uint32_t id=0; int32_t value=0; };
struct Memory { size_t reserved=0, used=0, peak=0, largest_free=0; };
struct Update { bool changed=false; uint32_t next_ms=UINT32_MAX; };
struct RasterStats {
  uint32_t retained_bytes=0,frames=0,image_rebuilds=0,image_cache_hits=0,flushes=0;
  uint64_t pixels=0,build_time_us=0,raster_time_us=0;
  uint32_t build_max_us=0,raster_max_us=0;
  uint64_t output_copy_bytes=0,output_compared_bytes=0;
  uint64_t texture_bytes=0,texture_peak_bytes=0;
  uint32_t object_creates=0,object_reuses=0,cached_objects=0;
};
struct RasterBudget {
  size_t maximum_retained_bytes=2*1024*1024;
  size_t maximum_texture_bytes=1024*1024;
  size_t maximum_canvas_cache_bytes=64*1024;
};
struct RasterOutput {
  uint16_t *pixels=nullptr;
  size_t count=0;
  // Initialized, exclusively editable output. Comparison repairs rotation and
  // overlays as well as scene changes; no output pointer is retained.
  bool initialized=false;
  RasterCoherence *coherence=nullptr;
  display::FrameDamageMap *damage=nullptr;
};
// Input texels/coverage are copied during image() into owned RGB565, packed
// RGB565+A8 planes, or recolored A8. LVGL scales/clips the source-sized texture
// within the existing live/replacement capacity budget.
enum class ImageFit:uint8_t { Stretch, Contain, Cover };
struct ImageView {
  unsigned width=0,height=0,stride=0;
  const uint16_t *pixels=nullptr;
  const uint8_t *alpha=nullptr;
  uint16_t color=0xffff;
  bool rounded=false;
  // Nonzero revision identifies immutable source content. Changing geometry,
  // tint, sampling or revision rebuilds the owned texture; zero always copies.
  uint64_t revision=0;
  ImageFit fit=ImageFit::Stretch;
  int8_t sampling=-1; // -1: render preference, 0: nearest, 1: linear.
};
class CanvasPainter;
// All methods have the same UI/render owner. No task, LCD or VM is owned here.
// render() produces a complete RGB565 result in a borrowed output. An
// qualified output copies LVGL invalidation regions with official draw-buffer
// operations. Unqualified storage is repaired in full when different.
class Scene {
 public:
  using Emit = void (*)(void *, Action);
  Scene();
  ~Scene();
  Scene(const Scene &)=delete;
  Scene &operator=(const Scene &)=delete;
  // Configure before the first begin(). The canvas cannot change after setup.
  bool configure(CanvasSpec, RasterBudget budget={});
  CanvasSpec canvas() const;
  // Render-owner preference. Feedback uses the existing service timer, no task.
  void motion(bool enabled);
  // Content text presets: 90, 100 or 115 percent. Geometry and touch regions
  // remain unchanged. Repeating the current preference performs no work.
  bool text_scale(unsigned percent);
  bool begin(uint64_t epoch, uint16_t background, bool antialias,
             Emit emit=nullptr, void *context=nullptr);
  bool label(uint32_t id, Rect, const char *, unsigned size, uint16_t color,
             Align align=Align::Center,uint32_t parent=0,bool ellipsis=false);
  bool image(uint32_t id,Rect,ImageView,uint32_t parent=0);
  // Retained native RGB565 plot, painted directly into a budgeted Scene-owned
  // source texture. Callback coordinates use the declared rectangle's origin;
  // no output-frame pointer, callback or context is retained after the build.
  bool canvas(uint32_t id,Rect,uint16_t background,void *context,
              bool (*paint)(void *,CanvasPainter &),uint32_t parent=0);
  // Copies a source crop as A8; LVGL owns scaling and recoloring.
  bool mask(uint32_t id,Rect,assets::MaskView,assets::MaskBounds,uint16_t color,
            uint64_t revision,uint32_t parent=0,bool antialias=true);
  // A nonzero revision must include every source/tint input. Reuses only a
  // matching size and AA mode; false means the caller must build image().
  bool reuse_image(uint32_t id,Rect,uint64_t revision,uint32_t parent=0);
  bool button(uint32_t id, Rect, const char *, unsigned size, uint16_t fill,
              uint16_t ink, bool selected=false, bool enabled=true,uint32_t parent=0,Align align=Align::Center);
  // Passive cards keep full text contrast and never emit a click action.
  bool panel(uint32_t id,Rect,uint16_t fill,uint32_t parent=0,int radius=12);
  bool progress(uint32_t id,Rect,unsigned percent,uint16_t color,uint32_t parent=0);
  // Official spinner animation runs on service(), with no new timer owner.
  bool spinner(uint32_t id,Rect,uint16_t color,uint32_t duration_ms=960);
  bool container(uint32_t id,Rect,int scroll,uint32_t scroll_action=0,bool horizontal=false,uint32_t parent=0);
  // Ordinary page composition. Track storage belongs to the Scene slot;
  // callers supply children in order and select grow/cells by their IDs.
  bool flow(uint32_t id,Rect,bool horizontal,int gap,uint32_t parent=0);
  // Arrange children of an existing card/control without another container.
  bool flex_layout(uint32_t id,bool horizontal,int gap,int inset=0,Align main=Align::Left,int trailing_inset=-1);
  bool flow_item(uint32_t id,unsigned grow=0,unsigned order=UINT32_MAX);
  // Floating decoration, in the parent's border-box coordinates. It consumes
  // neither a layout slot nor scrollable content size.
  bool overlay(uint32_t id,Rect);
  bool grid(uint32_t id,Rect,unsigned columns,unsigned rows,int column_gap,int row_gap,uint32_t parent=0);
  bool grid_item(uint32_t id,unsigned column,unsigned row,unsigned column_span=1,unsigned row_span=1);
  // Opt-in official Group traversal for a bounded sequence of actionable
  // buttons. Render-owner only; input owners enqueue via SceneRuntime::focus.
  bool focus_order(const uint32_t *ids,size_t count,uint32_t initial=0);
  bool focus(FocusCommand);
  bool paragraph(uint32_t id,Rect,const char *,unsigned size,uint16_t color,uint32_t parent);
  bool toggle(uint32_t id,Rect,bool checked,uint16_t color,uint32_t parent=0);
  bool slider(uint32_t id,Rect,unsigned value,uint16_t color,bool enabled=true);
  bool textbox(uint32_t id,Rect,const char *,unsigned size,bool password);
  // Labels are copied into scene-owned storage; no map pointer escapes a build.
  bool keypad(uint32_t id,Rect,const char *const *labels,size_t count,unsigned columns,
              unsigned selected,int disabled_key=-1);
  bool render(RasterOutput);
  // Draw on raw App/Launcher storage using this Scene's existing LVGL display,
  // font pool and render owner. Does not build widgets or retain output pixels.
  bool paint_canvas(RasterOutput,bool antialias,void *context,
                    bool (*paint)(void *,CanvasPainter &));
  // A bounded subcanvas uses the same engine/fonts. Its stride and exclusive
  // storage are explicit; no temporary target may outlive this call.
  bool paint_canvas(CanvasSpec,RasterOutput,bool antialias,void *context,
                    bool (*paint)(void *,CanvasPainter &));
  // One bounded render-owner timeline, explicitly sought only while a fresh
  // presentation buffer is borrowed. It never owns a frame or another clock.
  bool animation_value(uint64_t key,uint32_t elapsed_ms,uint32_t duration_ms,
                       float &value,AnimationCurve curve=AnimationCurve::EaseOut);
  void cancel_animation();
  // OS-qualified tap only; global gesture cancellation happens before this.
  bool tap(uint64_t epoch, int x, int y);
  bool pointer(uint64_t epoch,bool pressed,int x,int y,bool valid=true);
  // Advance LVGL timers on the same owner. Never draws into a borrowed buffer.
  Update service();
  void clear();
  // Same render owner only. Drops widgets, timers/input and the private raster;
  // the next begin() recreates them. Shared LVGL pool/fonts stay bounded/reusable.
  void suspend();
  static Memory memory();
  RasterStats stats() const;
 private:
  void reset(bool retain_objects);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace lunaredu::lunaros::graphics
