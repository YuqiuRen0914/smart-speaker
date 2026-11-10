#pragma once
#include <array>
#include <mutex>
#include <new>
#include <cassert>
#include <cstdlib>

namespace lunaredu::display::detail {
// Single producer, single renderer. Copies happen outside the lock; the active
// snapshot is never overwritten. Pending work may be replaced by newer state.
// Producer calls begin/publish in pairs; consumer calls take/finish in pairs.
// Job types and coalescing recovery belong to the caller, not the OS.
template<class Job> class RenderMailbox {
 public:
  bool busy(){std::lock_guard<std::mutex> lock(mutex_);if(pending_>=0||active_>=0)return true;for(auto slot:slots_)if(slot==Writing||slot==Retiring)return true;return false;}
  bool has_pending(){std::lock_guard<std::mutex> lock(mutex_);return pending_>=0;}
  bool writing(){std::lock_guard<std::mutex> lock(mutex_);for(auto slot:slots_)if(slot==Writing)return true;return false;}
  void cancel_pending() {
    int slot;
    {std::lock_guard<std::mutex> lock(mutex_);if(pending_<0)return;
      slot=pending_;pending_=-1;slots_[slot]=Retiring;}
    retire(unsigned(slot));
    {std::lock_guard<std::mutex> lock(mutex_);slots_[slot]=Free;}
  }
  Job &begin() {
    std::lock_guard<std::mutex> lock(mutex_);
    bool found=false;
    for (unsigned i = 0; i < slots_.size(); ++i) {
      if (slots_[i] == Free) { slots_[i] = Writing; writing_ = i;found=true;break; }
    }
    if(!found)std::abort(); // Single producer must pair begin/publish, also in release builds.
    return jobs_[writing_];
  }
  template<class Coalesce> void publish(Coalesce on_coalesce) {
    int retired=-1;
    {std::lock_guard<std::mutex> lock(mutex_);
     if (pending_ >= 0) {
      on_coalesce(jobs_[writing_]); // The caller determines how skipped work is recovered.
      // A free slot must not retain an immutable frame/resource lease. A
      // finite producer pool can otherwise remain exhausted after I/O ends.
      retired=pending_;slots_[retired]=Retiring;
    }
    slots_[writing_] = Pending;
    pending_ = static_cast<int>(writing_);
    }
    if(retired>=0){retire(unsigned(retired));std::lock_guard<std::mutex> lock(mutex_);slots_[retired]=Free;}
  }
  Job *take() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_ < 0 || active_ >= 0) return nullptr;
    active_ = pending_;
    pending_ = -1;
    slots_[active_] = Rendering;
    return &jobs_[active_];
  }
  void finish() {
    int slot;
    {std::lock_guard<std::mutex> lock(mutex_);if(active_<0)return;
      slot=active_;active_=-1;slots_[slot]=Retiring;}
    retire(unsigned(slot));
    {std::lock_guard<std::mutex> lock(mutex_);slots_[slot]=Free;}
  }
 private:
  void retire(unsigned slot) {
    release(jobs_[slot],0);
  }
  template<class T> static auto release(T &job,int) -> decltype(job.release_payload_leases(),void()) {
    job.release_payload_leases();
  }
  template<class T> static void release(T &job,long) {
    // Reconstruct in its existing mailbox storage. A desktop snapshot can be
    // several KiB; assigning Job{} would create that temporary on a task stack.
    job.~T();
    ::new (static_cast<void *>(&job)) T();
  }
  enum Slot { Free, Writing, Pending, Rendering, Retiring };
  std::mutex mutex_;
  std::array<Job, 3> jobs_{};
  std::array<Slot, 3> slots_{};
  unsigned writing_ = 0;
  int pending_ = -1, active_ = -1;
};
} // namespace lunaredu::display
