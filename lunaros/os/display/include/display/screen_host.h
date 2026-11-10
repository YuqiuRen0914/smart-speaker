#pragma once
#include "display/backend.h"
namespace lunaredu::display {
struct ScreenLease {
  uint64_t generation = 0;
};

// Borrowed authorization port. The caller owns the authority and output name
// for the screen lifetime; the library does not know an OS or service registry.
struct ScreenAuthority {
  void *context = nullptr;
  bool (*acquire)(void *, const char *, const char *, uint64_t &, std::string &) = nullptr;
  bool (*holds)(void *, uint64_t, const char *) = nullptr;
  bool (*release)(void *, const char *, uint64_t, std::string &) = nullptr;
};

// One instance is assembled per physical screen by the product. All methods
// run on the display owner; cross-task callers enqueue work on that owner.
class ScreenHost {
 public:
  // Bind before acquisition. With an OS control authority, this host cannot
  // grant screen ownership independently.
  bool bind_authority(ScreenAuthority, const char *output, std::string &error);
  template<class Control> bool bind_control(Control &control, const char *output,
                                           std::string &error) {
    return bind_authority({&control,
      [](void *p, const char *output, const char *owner, uint64_t &id, std::string &error) {
        auto &control = *static_cast<Control *>(p);
        const auto state = control.observe(output, 0);
        const auto provider = state.provider;
        if (state.lease.id || state.pending.id) {
          error = "screen_retirement_required"; return false;
        }
        typename Control::Admission admission;
        typename Control::Lease lease;
        if (!control.admit(output, owner, 0, 0, 0, admission, error)) return false;
        if (!control.confirm(admission.requested.id, provider.id, 0, lease, error)) {
          std::string cleanup;
          if(!control.cancel(admission.requested.id,provider.id,cleanup))error += ";cancel:"+cleanup;
          return false;
        }
        id = lease.id;
        return true;
      }, [](void *p, uint64_t id, const char *owner) {
        return static_cast<Control *>(p)->holds(id, owner, 0);
      }, [](void *p, const char *output, uint64_t id, std::string &error) {
        auto &control = *static_cast<Control *>(p);
        return control.retire(id, control.observe(output, 0).provider.id, error);
      }}, output, error);
  }
  bool configure(ScreenBackend backend, std::string &error);
  bool acquire(const char *owner, ScreenLease &lease, std::string &error);
  bool present(ScreenLease lease, ScreenFrameView frame, ScreenRegion region,
               std::string &error);
  bool present_patch(ScreenLease lease, ScreenPatchView patch,
                     ScreenRegion destination, std::string &error);
  void frame_presented(ScreenLease,ScreenFrameView,const ScreenRegion *,size_t);
  bool release(ScreenLease lease, std::string &error);
  bool wait_idle(ScreenLease lease, std::string &error);
  bool ready() const {
    return backend_.present != nullptr || backend_.present_patch != nullptr;
  }
  uint16_t width() const { return backend_.width; }
  uint16_t height() const { return backend_.height; }
  ScreenCapabilities capabilities() const { return backend_.capabilities; }

 private:
  ScreenBackend backend_{};
  ScreenAuthority control_{};
  const char *control_output_ = nullptr;
  std::string owner_;
  uint64_t generation_ = 0;
  bool acquired_ = false;
};

} // namespace lunaredu::display
