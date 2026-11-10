#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <array>

namespace lunaredu::display {
struct ScreenPipelineSnapshot {
  uint32_t rendered = 0, completed = 0, errors = 0, transfer_us = 0, transfer_bytes = 0;
  uint32_t partial_frames = 0, unchanged_frames = 0, render_jobs = 0;
  uint64_t render_time_us = 0, transfer_time_us = 0;
  uint32_t render_max_us = 0, transfer_max_us = 0;
  uint64_t wait_time_us = 0, compare_time_us = 0;
  uint32_t wait_max_us = 0, compare_max_us = 0, compare_samples = 0;
  uint32_t renderer_stack_free = 0; // Optional product task diagnostic, bytes.
  uint64_t raster_time_us=0,compose_time_us=0,animation_time_us=0;
  uint32_t raster_samples=0,compose_samples=0,animation_samples=0;
  uint32_t raster_max_us=0,compose_max_us=0,animation_max_us=0;
  // Render, Wait, Compare, Raster, Compose, Animation, Transfer. These
  // reference budgets locate slow stages; they are not summed frame times.
  std::array<uint32_t,7> over_16_ms{},over_32_ms{};
  uint64_t baseline_copy_bytes=0;
  uint64_t compare_settled_task_us=0;
  uint32_t compare_profiled_samples=0;
};
enum class ScreenPipelinePhase { Render, Wait, Compare, Raster, Compose, Animation };

// Assemble one instance per physical display. rendered counts submitted frames;
// completed counts backend transfers/patches AFTER I/O completion. They coincide
// only for full-frame, one-transfer presentation. Source-copy ack is not I/O
// completion. Counters are cumulative, allocation-free and safe across owners;
// a live snapshot is not a transaction across independent stages. 32-bit counts
// wrap; callers must use unsigned deltas. Timing totals are 64-bit microseconds.
class ScreenPipelineMetrics {
 public:
  void presented(bool partial) {
    ++rendered_;
    if (partial) ++partial_frames_;
  }
  void unchanged() { ++unchanged_frames_; }
  void compared_task(uint32_t elapsed){compare_settled_task_us_.fetch_add(elapsed);++compare_profiled_samples_;}
  void baseline_copy(size_t bytes) {baseline_copy_bytes_.fetch_add(bytes);}
  void error() { ++errors_; }
  void transferred(bool succeeded, uint64_t elapsed_us, uint32_t bytes) {
    if (!succeeded) { error(); return; }
    ++completed_; transfer_bytes_.fetch_add(bytes);
    transfer_us_.fetch_add(uint32_t(elapsed_us));
    transfer_time_us_.fetch_add(elapsed_us);
    record_max(transfer_max_us_, elapsed_us);
    record_slow(6,elapsed_us);
  }
  void elapsed(ScreenPipelinePhase phase, uint64_t elapsed_us) {
    record_slow(static_cast<size_t>(phase),elapsed_us);
    switch (phase) {
      case ScreenPipelinePhase::Render:
        render_time_us_.fetch_add(elapsed_us); ++render_jobs_;
        record_max(render_max_us_, elapsed_us); break;
      case ScreenPipelinePhase::Wait:
        wait_time_us_.fetch_add(elapsed_us);
        record_max(wait_max_us_, elapsed_us); break;
      case ScreenPipelinePhase::Compare:
        compare_time_us_.fetch_add(elapsed_us); ++compare_samples_;
        record_max(compare_max_us_, elapsed_us); break;
      case ScreenPipelinePhase::Raster: raster_time_us_.fetch_add(elapsed_us);++raster_samples_;record_max(raster_max_us_,elapsed_us);break;
      case ScreenPipelinePhase::Compose: compose_time_us_.fetch_add(elapsed_us);++compose_samples_;record_max(compose_max_us_,elapsed_us);break;
      case ScreenPipelinePhase::Animation: animation_time_us_.fetch_add(elapsed_us);++animation_samples_;record_max(animation_max_us_,elapsed_us);break;
    }
  }
  ScreenPipelineSnapshot snapshot() const {
    ScreenPipelineSnapshot result{rendered_.load(), completed_.load(), errors_.load(),
      transfer_us_.load(), transfer_bytes_.load(), partial_frames_.load(),
      unchanged_frames_.load(), render_jobs_.load(), render_time_us_.load(),
      transfer_time_us_.load(), render_max_us_.load(), transfer_max_us_.load(),
      wait_time_us_.load(), compare_time_us_.load(), wait_max_us_.load(),
      compare_max_us_.load(), compare_samples_.load(), 0,
      raster_time_us_.load(),compose_time_us_.load(),animation_time_us_.load(),
      raster_samples_.load(),compose_samples_.load(),animation_samples_.load(),
      raster_max_us_.load(),compose_max_us_.load(),animation_max_us_.load()};
    for(size_t index=0;index<7;++index){
      result.over_16_ms[index]=over_16_ms_[index].load();
      result.over_32_ms[index]=over_32_ms_[index].load();
    }
    result.compare_settled_task_us=compare_settled_task_us_.load();result.compare_profiled_samples=compare_profiled_samples_.load();
    result.baseline_copy_bytes=baseline_copy_bytes_.load();
    return result;
  }
 private:
  std::atomic<uint64_t> baseline_copy_bytes_{0};
  void record_slow(size_t stage,uint64_t elapsed){
    if(elapsed>16000)++over_16_ms_[stage];
    if(elapsed>32000)++over_32_ms_[stage];
  }
  static void record_max(std::atomic<uint32_t> &target, uint64_t elapsed) {
    const uint32_t sample = elapsed > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max() : uint32_t(elapsed);
    auto previous = target.load();
    while (previous < sample && !target.compare_exchange_weak(previous, sample)) {}
  }
  std::atomic<uint32_t> rendered_{0}, completed_{0}, errors_{0};
  std::atomic<uint64_t> compare_settled_task_us_{0};
  std::atomic<uint32_t> compare_profiled_samples_{0};
  std::atomic<uint32_t> transfer_us_{0}, transfer_bytes_{0};
  std::atomic<uint32_t> partial_frames_{0}, unchanged_frames_{0}, render_jobs_{0};
  std::atomic<uint64_t> render_time_us_{0}, transfer_time_us_{0};
  std::atomic<uint32_t> render_max_us_{0}, transfer_max_us_{0};
  std::atomic<uint64_t> wait_time_us_{0}, compare_time_us_{0};
  std::atomic<uint32_t> wait_max_us_{0}, compare_max_us_{0}, compare_samples_{0};
  std::atomic<uint64_t> raster_time_us_{0},compose_time_us_{0},animation_time_us_{0};
  std::atomic<uint32_t> raster_samples_{0},compose_samples_{0},animation_samples_{0};
  std::atomic<uint32_t> raster_max_us_{0},compose_max_us_{0},animation_max_us_{0};
  std::array<std::atomic<uint32_t>,7> over_16_ms_{},over_32_ms_{};
};

class ScreenPipelineTiming {
 public:
  using Clock = uint64_t (*)();
  ScreenPipelineTiming(ScreenPipelineMetrics &metrics, ScreenPipelinePhase phase,
                       Clock clock)
      : metrics_(metrics), phase_(phase), clock_(clock), started_(clock()) {}
  ~ScreenPipelineTiming() {
    const auto end = clock_();
    metrics_.elapsed(phase_, end >= started_ ? end - started_ : 0);
  }
  ScreenPipelineTiming(const ScreenPipelineTiming &) = delete;
  ScreenPipelineTiming &operator=(const ScreenPipelineTiming &) = delete;
 private:
  ScreenPipelineMetrics &metrics_;
  ScreenPipelinePhase phase_;
  Clock clock_;
  uint64_t started_;
};
} // namespace lunaredu::display
