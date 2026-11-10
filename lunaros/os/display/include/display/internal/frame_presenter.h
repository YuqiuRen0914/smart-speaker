#pragma once
#include "display/pipeline/frame_refresh.h"
#include "display/screen_host.h"
#include "display/pipeline/pipeline_metrics.h"
#include <string>
#include "display/frame.h"

namespace lunaredu::display {
struct FramePresenterPorts {
  void *context = nullptr;
  bool (*wait)(void *, unsigned slot, std::string &) = nullptr;
  bool (*submit)(void *, unsigned slot, ScreenRegion, std::string &) = nullptr;
  uint64_t (*clock_us)() = nullptr;
  bool (*profiling_enabled)()=nullptr;
  uint32_t (*task_runtime_us)()=nullptr;
  bool (*submit_regions)(void *,unsigned,const ScreenRegion *,size_t,std::string &)=nullptr;
  unsigned window_pixels=32;
};
// Renderer-owned coordinator. Buffers and callbacks are borrowed from the
// resident pipeline; no pixel allocation, worker, page or application policy lives here.
class ScreenFramePresenter {
 public:
  explicit ScreenFramePresenter(ScreenPipelineMetrics &metrics) : metrics_(metrics) {}
  ScreenFramePresenter(const ScreenFramePresenter &) = delete;
  ScreenFramePresenter &operator=(const ScreenFramePresenter &) = delete;
bool bind(uint16_t *first, uint16_t *second, uint16_t width, uint16_t height,
            FramePresenterPorts ports, std::string &error);
  void reset() {frames_[0]=frames_[1]=nullptr;ports_={};width_=height_=0;have_frame_=recovery_pending_=recovering_=false;baseline_application_.clear();baseline_generation_=0;}
  unsigned slot() const { return slot_; }
  uint16_t *frame() const { return frames_[slot_]; }
  bool recovery_pending() const { return recovery_pending_; }
  void begin_render() { recovering_ = recovery_pending_; }
  // Consume only when the caller actually paints a full recovery frame.
  // A subsequent wait/submission failure sets the request again.
  void consume_recovery() { recovery_pending_ = false; }
  void invalidate() {
    have_frame_ = false; recovery_pending_ = true;
  }
FrameCanvas begin(uint16_t width, uint16_t height, uint16_t color,
                    uint16_t origin_y, bool clear, std::string &error);
FrameSubmission present(ScreenRegion region, const FramePresentationKey &key,
                          bool incremental_allowed, std::string &error,const FrameDamageMap *map=nullptr);
 private:
bool wait(unsigned slot, std::string &error);
  void failed() { metrics_.error(); invalidate(); }
  ScreenPipelineMetrics &metrics_;
  FramePresenterPorts ports_;
  uint16_t *frames_[2]{};
  uint16_t width_ = 0, height_ = 0;
  unsigned slot_ = 0;
  bool have_frame_ = false, recovery_pending_ = false, recovering_ = false;
  uint64_t baseline_generation_ = 0;
  std::string baseline_application_;
};
} // namespace lunaredu::display
