#pragma once
#include "display/internal/screen_session.h"

namespace lunaredu::display {
// Frame facade for renderers on an existing execution owner. All storage and
// transport belong to the same session used by ManagedScreen.
class ScreenSurface {
public:
  ScreenSurface() = default;
  ~ScreenSurface();
  ScreenSurface(const ScreenSurface &) = delete;
  ScreenSurface &operator=(const ScreenSurface &) = delete;
  bool prepare(ScreenBackend, std::string &, ScreenMemory = external_screen_memory(), ScreenStorageBudget = {});
  bool enable_worker(lunaros::KernelRuntime &, lunaros::ManagedTaskDefinition, std::string &);
  void release_unstarted();
  bool stop(std::string &);
  bool acquire(const char *, std::string &);
  bool release(std::string &);
  bool wait_all(std::string &);
  FrameCanvas begin_frame(std::string &);
  bool present(const ScreenRegion *, size_t, const FrameCanvas &, std::string &);
  template<class Control> bool bind_control(Control &control,const char *owner,std::string &error) {
    return session_.bind_control(control,owner,error);
  }
  bool ready() const;
  uint16_t width() const;
  uint16_t height() const;
  ScreenCapabilities capabilities() const;
  bool asynchronous() const;
  bool worker_failed() const;
  bool active() const;
  bool retained() const;
private:
  friend class ScreenOverlaySurface;
  ScreenMemory memory() const;
  bool holds_frame(const FrameCanvas &) const;
  uint64_t ownership_generation() const;
  void retain_overlay();
  void release_overlay_binding();
  bool reserve_overlay(size_t,std::string &);
  void release_overlay(size_t);
  uint16_t *frame(unsigned) const;
  detail::ScreenSession session_;
};
}
