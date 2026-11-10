#include "display/internal/frame_presenter.h"
#include <algorithm>
namespace lunaredu::display {
bool ScreenFramePresenter::bind(uint16_t *first, uint16_t *second, uint16_t width, uint16_t height,
            FramePresenterPorts ports, std::string &error) {
    if (frames_[0] || !first || !second || first == second || !width || !height ||
        !ports.wait || !ports.submit || !ports.clock_us) {
      error = "frame_presenter_binding_invalid"; return false;
    }
    frames_[0] = first; frames_[1] = second;
    width_ = width; height_ = height; ports_ = ports;
    return true;
  }

FrameCanvas ScreenFramePresenter::begin(uint16_t width, uint16_t height, uint16_t color,
                    uint16_t origin_y, bool clear, std::string &error) {
    if (!frame() || !width || !height || width > width_ ||
        uint32_t(origin_y) + height > height_) {
      error = "frame_canvas_invalid"; return {};
    }
    if (!wait(slot_, error)) { failed(); return {}; }
    const bool partial = width != width_ || height != height_;
    if (partial) {
      // A partial repaint needs an acknowledged baseline. A previous async
      // transfer failure must not propagate unconfirmed pixels to another patch.
      if (!have_frame_ || recovering_ || !wait(slot_ ^ 1, error)) {
        if (error.empty()) error = "frame_baseline_unavailable";
        failed(); return {};
      }
      const auto *baseline=frames_[slot_ ^ 1];
      size_t copied=0;
      if(!clear) {copied=size_t(width_)*height_;std::copy_n(baseline,copied,frame());}
      else {
        const size_t top=size_t(origin_y)*width_;
        const size_t bottom=size_t(origin_y+height)*width_;
        const size_t tail=size_t(height_)*width_-bottom;
        std::copy_n(baseline,top,frame());
        std::copy_n(baseline+bottom,tail,frame()+bottom);
        copied=top+tail;
        if(width<width_)for(unsigned row=origin_y;row<unsigned(origin_y)+height;++row) {
          const size_t offset=size_t(row)*width_+width;
          std::copy_n(baseline+offset,width_-width,frame()+offset);copied+=width_-width;
        }
      }
      metrics_.baseline_copy(copied*sizeof(uint16_t));
    }
    auto *pixels = frame() + size_t(origin_y) * width_;
    if (clear) for (unsigned row = 0; row < height; ++row)
      std::fill_n(pixels + size_t(row) * width_, width, color);
    return {pixels, width, height, width_};
  }

FrameSubmission ScreenFramePresenter::present(ScreenRegion region, const FramePresentationKey &key,
                          bool incremental_allowed, std::string &error,const FrameDamageMap *map) {
    if (!frame() || region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
        int64_t(region.x) + region.width > width_ || int64_t(region.y) + region.height > height_) {
      error = "frame_region_invalid"; failed(); return FrameSubmission::Failed;
    }
    const bool incremental = incremental_allowed && !recovering_ && have_frame_ &&
        key.generation == baseline_generation_ && key.application == baseline_application_;
    const bool full_region = region.x == 0 && region.y == 0 && region.width == width_ && region.height == height_;
    if (!full_region && (!have_frame_ || recovering_ || key.generation != baseline_generation_ ||
                        key.application != baseline_application_)) {
      error = "frame_patch_baseline_invalid"; failed(); return FrameSubmission::Failed;
    }
    ScreenDamageSet batch;bool use_batch=false;
    if (incremental) {
      const bool baseline_valid = wait(slot_ ^ 1, error);
      FrameRefresh plan;
      const bool profiling=ports_.profiling_enabled&&ports_.task_runtime_us&&ports_.profiling_enabled();
      const uint32_t task_start=profiling?ports_.task_runtime_us():0;
      {
        ScreenPipelineTiming comparing{metrics_, ScreenPipelinePhase::Compare, ports_.clock_us};
        if(ports_.submit_regions){
          batch=plan_frame_refresh_batch(frames_[slot_^1],frame(),width_,height_,width_,baseline_valid,map,ports_.window_pixels);
          use_batch=true;plan.skip=batch.count()==0;
        }else plan = plan_frame_refresh(frames_[slot_ ^ 1], frame(), width_, height_, width_, baseline_valid,map);
      }
      if(profiling)metrics_.compared_task(uint32_t(ports_.task_runtime_us()-task_start));
      if (plan.skip) { metrics_.unchanged(); return FrameSubmission::Unchanged; }
      if(!use_batch)region = {plan.region.x, plan.region.y, plan.region.width, plan.region.height};
      // Failed previous I/O cannot supply a baseline, even if this retry fails.
      if (!baseline_valid) invalidate();
    }
    if (!(use_batch?ports_.submit_regions(ports_.context,slot_,batch.regions(),batch.count(),error):
        ports_.submit(ports_.context, slot_, region, error))) {
      failed(); return FrameSubmission::Failed;
    }
    metrics_.presented(use_batch?!batch.full():region.width < width_ || region.height < height_);
    // Retain identity independently of the submitted snapshot's lifetime.
    // Unchanged identities reuse owned storage instead of copying per frame.
    if (key.application != baseline_application_) baseline_application_ = key.application;
    baseline_generation_ = key.generation; have_frame_ = true; slot_ ^= 1;
    return FrameSubmission::Submitted;
  }

bool ScreenFramePresenter::wait(unsigned slot, std::string &error) {
    ScreenPipelineTiming waiting{metrics_, ScreenPipelinePhase::Wait, ports_.clock_us};
    return ports_.wait(ports_.context, slot, error);
  }
}
