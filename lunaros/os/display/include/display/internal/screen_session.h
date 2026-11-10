#pragma once
#include "display/screen_configuration.h"
#include "display/transfer_queue.h"
#include "display/internal/frame_presenter.h"

namespace lunaredu::display { template<class> class ManagedScreen; class ScreenSurface; }
namespace lunaredu::display::detail {
// One non-template owner for frame storage, physical leases and submission.
// Public facades serialize prepare/start/stop on their existing owner.
struct RendererRetirement {
  void *context = nullptr;
  void (*request_stop)(void *) = nullptr;
  bool (*finished)(void *) = nullptr;
};
class ScreenSession {
 public:
  explicit ScreenSession(uint64_t (*clock_us)() = nullptr);
  ~ScreenSession();
  ScreenSession(const ScreenSession &) = delete;
  ScreenSession &operator=(const ScreenSession &) = delete;
  bool prepare(ScreenBackend, std::string &, ScreenMemory = external_screen_memory(),
      unsigned auxiliary_frames = 0, ScreenStorageBudget = {}, size_t mailbox_bytes = 0,
      unsigned primary_frames = 2);
  bool enable_worker(lunaros::KernelRuntime &, const lunaros::ManagedTaskDefinition &, std::string &);
  bool start(lunaros::KernelRuntime &, const char *, ScreenPipelineTasks,
      lunaros::ManagedTaskFunction, void *, RendererRetirement, std::string &);
  bool start_direct(const char *, std::string &);
  bool stop(std::string &);
  void release_unstarted();
  bool acquire(const char *, std::string &);
  bool release(std::string &);
  bool wait_all(std::string &);
  bool wait_idle(std::string &);
  bool bind_authority(ScreenAuthority, const char *, std::string &);
  template<class Control> bool bind_control(Control &control, const char *output, std::string &error) {
    return host_.bind_control(control, output, error);
  }
  FrameCanvas begin_frame(std::string &);
  FrameCanvas begin_frame(uint16_t,uint16_t,uint16_t,uint16_t,bool,std::string &);
  bool present(const FrameCanvas &, const ScreenRegion *, size_t, std::string &);
  FrameSubmission present(const FrameCanvas &, ScreenRegion, const FramePresentationKey &,
      bool, std::string &, const FrameDamageMap * = nullptr);
  bool ready() const;
  bool active() const;
  bool retained() const;
  bool asynchronous() const;
  bool worker_failed() const;
  ScreenLifecycle lifecycle() const;
  ScreenStorageSnapshot storage_snapshot() const;
  ScreenSessionSnapshot snapshot() const;
  uint16_t width() const;
  uint16_t height() const;
  ScreenCapabilities capabilities() const;
  uint16_t *active_frame() const;
  unsigned active_slot() const;
  uint16_t *auxiliary_frame(unsigned) const;
  unsigned frame_slot(const uint16_t *) const;
  bool recovery_pending() const;
  void begin_render();
  void consume_recovery();
  void invalidate_frame();
  void wake_renderer();
 private:
  template<class> friend class ::lunaredu::display::ManagedScreen;
  friend class ::lunaredu::display::ScreenSurface;
  bool wait(unsigned, std::string &);
  bool submit(unsigned, const ScreenRegion *, size_t, std::string &);
  FrameCanvas borrow(FrameCanvas, unsigned, std::string &);
  bool validate(const FrameCanvas &, std::string &) const;
  bool holds_frame(const FrameCanvas &) const;
  void retain_overlay();
  void release_overlay_binding();
  void consume(const FrameCanvas &);
  bool bind_presenter(std::string &);
  uint16_t *frame(unsigned) const;
  bool reserve_overlay(size_t, std::string &);
  void release_overlay(size_t);
  void publish_storage_status();
  void sample_renderer_stack();
  void free_storage();
  ScreenPipelineMetrics metrics_;
  ScreenFramePresenter presenter_;
  ScreenHost host_;
  ScreenBackend backend_{};
  ScreenLease lease_{};
  ScreenTransferQueue display_;
  ScreenMemory memory_{};
  ScreenStorageBudget budget_{};
  std::array<uint16_t *, 4> frames_{};
  void *mailbox_storage_ = nullptr;
  void (*destroy_mailbox_)(void *) = nullptr;
  size_t mailbox_bytes_ = 0, overlay_bytes_ = 0;
  size_t overlay_bindings_ = 0;
  unsigned primary_frames_ = 0, next_ = 0;
  uint64_t (*clock_)() = nullptr;
  uint64_t generation_ = 0;
  uint64_t ownership_generation_ = 0;
  std::array<uint64_t, 2> borrowed_{};
  std::array<FrameCanvas, 2> canvases_{};
  RendererRetirement retirement_{};
  lunaros::KernelRuntime *runtime_ = nullptr;
  std::array<char,lunaros::kManagedTaskIdBytes> renderer_id_{};
  lunaros::ManagedTaskHandle renderer_ = nullptr;
  std::atomic<bool> direct_active_{false};
  bool attempted_ = false, worker_attempted_ = false, retained_ = false;
  bool ready_ = false, stopped_ = false, worker_failed_ = false;
  std::atomic<bool> stopping_{false};
  std::atomic<ScreenLifecycle> observed_lifecycle_{ScreenLifecycle::Unconfigured};
  std::atomic<uint32_t> observed_frames_{0};
  std::atomic<uint32_t> observed_renderer_stack_{0};
  std::atomic<uint64_t> observed_frame_bytes_{0},observed_mailbox_bytes_{0},observed_retained_bytes_{0};
  std::atomic<uint64_t> observed_budget_bytes_{0},observed_reserved_bytes_{0};
};
}
