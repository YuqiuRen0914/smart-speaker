#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <atomic>
#include <cstdlib>
#include "display/screen_status.h"
#include "display/frame.h"

#include "display/screen_host.h"
#include "display/pipeline/damage_set.h"
#include "kernel/managed_task.h"
#include "diagnostics/runtime_work_profile.h"

namespace lunaredu::lunaros { class KernelRuntime; }

namespace lunaredu::display {

// A two-frame display owner. The renderer supplies complete, stable frame
// buffers; the worker owns each submitted buffer until wait() succeeds.
// The backend still decides how pixels reach the panel.
class ScreenTransferQueue {
 public:
  ~ScreenTransferQueue() { if (ready()) std::abort(); }
  bool start(lunaros::KernelRuntime &runtime, ScreenHost &host,
             const lunaros::ManagedTaskDefinition &task,
             uint16_t *first, uint16_t *second, uint16_t width,
             uint16_t height, ScreenPixelFormat format, std::string &error);
  bool ready() const { return task_ != nullptr; }
  bool active() const { return active_.load(std::memory_order_acquire); }
  bool acquire(const char *owner, std::string &error);
  bool present(unsigned buffer, const ScreenRegion *regions, size_t count,
               std::string &error, FrameCanvas canvas = {});
  bool wait(unsigned buffer, std::string &error);
  bool wait_all(std::string &error);
  bool drain(std::string &error);
  bool release(std::string &error);
  bool stop(std::string &error);
  ScreenTransferSnapshot snapshot() const {
    return {observed_request_.load(),control_timeouts_.load(),late_replies_.load(),
        mismatched_replies_.load(),frame_timeouts_.load()};
  }

 private:
  enum class Command : uint8_t { Acquire, Present, Release, Drain, Stop };
  // Queue items own their payload; a timed-out caller cannot mutate a request.
  struct Work {
    Command command;
    uint8_t buffer = 0;
    uint64_t request = 0;
    uint64_t queued_us = 0;
    std::array<char, lunaros::kManagedTaskOwnerBytes> owner{};
  };
  struct ControlReply {
    uint64_t request = 0;
    bool succeeded = false;
    std::array<char, 128> error{};
  };
  struct Slot {
    uint16_t *pixels = nullptr;
    FrameCanvas canvas{};
    void *completed = nullptr;
    std::array<ScreenRegion, kScreenBatchMaxRegions> regions{};
    size_t region_count = 0;
    bool in_flight = false;
    bool succeeded = true;
    std::string error;
  };
  static void run(void *context);
  void worker();
  bool dispatch(Command command, std::string &error, const char *owner = nullptr);
  bool settle_control(std::string &error);
  void process(Work work);

  system::RuntimeWorkProfile<3> present_profile_;
  uint64_t queued_profile_us_=0,max_queued_profile_us_=0;
  uint64_t profile_bytes_=0;
  uint32_t profile_regions_=0,profile_completed_=0,profile_failed_=0;
  ScreenHost *host_ = nullptr;
  std::array<Slot, 2> slots_{};
  void *queue_ = nullptr;
  void *control_replies_ = nullptr;
  lunaros::ManagedTaskHandle task_ = nullptr;
  lunaros::KernelRuntime *runtime_ = nullptr;
  std::array<char, lunaros::kManagedTaskIdBytes> task_id_{};
  std::atomic<bool> finished_{false};
  bool stopping_ = false, stop_queued_ = false;
  ScreenLease lease_{};
  uint64_t next_request_ = 0, pending_request_ = 0;
  Command pending_command_ = Command::Drain;
  bool pending_timed_out_ = false;
  std::atomic<uint64_t> observed_request_{0};
  std::atomic<uint32_t> control_timeouts_{0},late_replies_{0},mismatched_replies_{0},frame_timeouts_{0};
  std::atomic<bool> active_{false};
  std::array<char, lunaros::kManagedTaskOwnerBytes> pending_owner_{};
  uint16_t width_ = 0;
  uint16_t height_ = 0;
  ScreenPixelFormat format_ = ScreenPixelFormat::Rgb565;
};

} // namespace lunaredu::display
