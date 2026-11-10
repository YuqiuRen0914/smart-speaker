#pragma once
#include "diagnostics/runtime_work_profile.h"
#include "display/internal/screen_session.h"
#include "display/internal/render_mailbox.h"
#include "display/pipeline/frame_cadence.h"
#include <atomic>


namespace lunaredu::display {
enum class ScreenExecution { Workers, Caller };
// Borrowed callbacks run only on the existing render owner.
struct RenderServicePorts {
  void *context = nullptr;
  bool (*service)(void *) = nullptr;
  uint32_t (*next_wake_ms)(void *) = nullptr;
};
// A Job is an immutable App-owned presentation. OS owns the mailbox, frame
// storage, execution and redraw signalling. The callback borrows a job only
// until it returns, including failed/obsolete-frame retirement.
template<class Job> class ManagedScreen {
 public:
  using Paint = void (*)(void *, Job &);
  struct RenderDispatch {
    void *context = nullptr;
    void (*request)(void *) = nullptr;
  };
  explicit ManagedScreen(uint64_t (*clock)(), RenderDispatch dispatch = {});
  ~ManagedScreen();
  bool render_service(RenderServicePorts ports);
  bool observe_transfer(void *context, void (*observer)(void *, ScreenPatchView, ScreenRegion));
  bool observe_presented(void *context,void (*observer)(void *,ScreenFrameView,const ScreenRegion *,size_t));
  // An unchanged canvas is verified against the acknowledged panel baseline.
  // Notify logical presentation owners without inventing a physical transfer.
  bool observe_unchanged(void *context, void (*observer)(void *, FrameCanvas));
  bool setup(lunaros::KernelRuntime *runtime, ScreenBackend backend,
      const char *owner, ScreenPipelineTasks tasks, Paint paint, void *context,
      std::string &error, unsigned auxiliary_frames = 0,
      ScreenExecution execution = ScreenExecution::Workers, ScreenStorageBudget budget = {});
  template<class Capture, class Coalesce> void publish(Capture capture, Coalesce coalesce);
  // Cooperatively retire rendering and transport before releasing frame storage.
  bool stop(std::string &error);
  // Existing owner closes frame admission and drains rendering/physical I/O.
  // False retains the pause and every borrowed buffer for a bounded retry.
  bool quiesce(std::string &error);
  void resume();
  bool bind_authority(ScreenAuthority authority,const char *output,std::string &error);
  template<class Control> bool bind_control(Control &control,const char *output,std::string &error) {
    if(lifecycle()!=ScreenLifecycle::Unconfigured){error="screen_already_configured";return false;}
    return session_.bind_control(control,output,error);
  }
  bool ready() const;
  ScreenLifecycle lifecycle() const;
  ScreenSessionSnapshot snapshot() const;
  uint64_t now_us() const;
  template<class Work> decltype(auto) timed(ScreenPipelinePhase phase,Work work);
  uint16_t width() const;
  uint16_t height() const;
  FrameCanvas begin_frame(uint16_t width,uint16_t height,uint16_t color,uint16_t y,bool clear,std::string &error);
  uint16_t *active_frame() const;
  unsigned active_slot() const;
  uint64_t ownership_generation() const {return session_.ownership_generation_;}
  uint16_t *auxiliary_frame(unsigned index) const;
  unsigned frame_slot(const uint16_t *pixels) const;
  bool wait_idle(std::string &error);
  FrameSubmission present(const FrameCanvas &canvas,ScreenRegion region,const FramePresentationKey &key,bool incremental,std::string &error,const FrameDamageMap *map=nullptr);
  bool recovery_pending() const;
  void begin_render();
  void consume_recovery();
  void invalidate_frame();
  void request_redraw();
  bool take_redraw();
  bool accepts_frame();
  // Called by the supplied render owner. Tests can control when queued work
  // executes without replacing the mailbox or its coalescing policy.
  bool service_render();
  void wake();
 private:
  void log_failure([[maybe_unused]] const char *phase,[[maybe_unused]] const std::string &error);
#ifdef ESP_PLATFORM
  uint64_t last_failure_log_us_=0;
#endif
  void service_timed();
  bool transfer(ScreenPatchView patch, ScreenRegion region, std::string &error);
  bool render_next();
  bool pace_frame();
  detail::ScreenSession session_;
  system::RuntimeWorkProfile<1> render_profile_;
  ScreenPipelineSnapshot render_profile_baseline_{};
  uint64_t render_profile_pacing_baseline_=0;
  detail::RenderMailbox<Job> *mailbox_ = nullptr;
  std::mutex producer_mutex_;
  // Nonblocking even when an owner callback asks to retire its own work.
  class RenderGuard {
   public:
    explicit RenderGuard(std::atomic_flag &flag):flag_(flag),owned_(!flag.test_and_set(std::memory_order_acquire)){}
    ~RenderGuard(){if(owned_)flag_.clear(std::memory_order_release);}
    explicit operator bool() const {return owned_;}
   private:
    std::atomic_flag &flag_;bool owned_;
  };
  std::atomic_flag render_busy_=ATOMIC_FLAG_INIT;
  std::atomic<bool> paused_{false};
  std::atomic<bool> retiring_{false};
  std::atomic<bool> stop_requested_{false}, renderer_finished_{false};
  ScreenFrameCadence frame_cadence_;
  std::atomic<bool> full_redraw_{false};
  std::atomic<uint32_t> coalesced_jobs_{0},render_budget_misses_{0},transfer_budget_misses_{0};
  std::atomic<uint64_t> pacing_wait_us_{0};
  ScreenBackend backend_{};
  uint64_t (*clock_)();
  RenderDispatch dispatch_;
  RenderServicePorts service_;
  void *observer_context_ = nullptr;
  void (*observer_)(void *, ScreenPatchView, ScreenRegion) = nullptr;
  void *presented_context_=nullptr;
  void (*presented_observer_)(void *,ScreenFrameView,const ScreenRegion *,size_t)=nullptr;
  void *unchanged_context_ = nullptr;
  void (*unchanged_observer_)(void *, FrameCanvas) = nullptr;
  Paint paint_ = nullptr;
  void *context_ = nullptr;
  std::atomic<bool> ready_{false}; // Admission publishes readiness to the render owner.
  bool caller_execution_ = false;
  std::atomic<bool> redraw_{false};
};
}

// Template definitions must remain visible to Job consumers.
#include "display/internal/managed_screen_impl.tpp"
