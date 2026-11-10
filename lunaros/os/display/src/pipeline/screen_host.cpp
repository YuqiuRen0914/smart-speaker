#include "display/screen_host.h"


namespace lunaredu::display {

bool ScreenHost::bind_authority(ScreenAuthority control,
                              const char *output, std::string &error) {
  if (acquired_ || !output || !output[0] ||
      (!control.context || !control.acquire || !control.holds || !control.release) ||
      (control_.context && (control_.context != control.context ||
                   std::string(control_output_) != output))) {
    error = "screen_control_binding_conflict";
    return false;
  }
  control_ = control;
  control_output_ = output;
  error.clear();
  return true;
}

bool ScreenHost::configure(ScreenBackend backend, std::string &error) {
  if (acquired_) {
    error = "screen_in_use";
    return false;
  }
  if (backend.width == 0 || backend.height == 0 ||
      (backend.present == nullptr && backend.present_patch == nullptr) ||
      backend.capabilities.x_alignment == 0 ||
      backend.capabilities.y_alignment == 0) {
    error = "invalid_screen_backend";
    return false;
  }
  backend_ = backend;
  error.clear();
  return true;
}

bool ScreenHost::acquire(const char *owner, ScreenLease &lease,
                         std::string &error) {
  lease = {};
  if (!ready()) {
    error = "screen_unavailable";
    return false;
  }
  if (owner == nullptr || owner[0] == '\0') {
    error = "invalid_screen_owner";
    return false;
  }
  if (acquired_) {
    error = "screen_in_use";
    return false;
  }
  if (control_.context) {
    uint64_t granted = 0;
    if (!control_.acquire(control_.context, control_output_, owner, granted, error))
      return false;
    generation_ = granted;
  } else if (++generation_ == 0) ++generation_;
  acquired_ = true;
  owner_ = owner;
  lease.generation = generation_;
  error.clear();
  return true;
}

void ScreenHost::frame_presented(ScreenLease lease,ScreenFrameView frame,const ScreenRegion *regions,size_t count){
  if(acquired_ && lease.generation==generation_ && count && backend_.frame_presented)
    backend_.frame_presented(backend_.context,frame,regions,count);
}

bool ScreenHost::present(ScreenLease lease, ScreenFrameView frame,
                         ScreenRegion region, std::string &error) {
  if (!acquired_ || lease.generation == 0 ||
      lease.generation != generation_ ||
      (control_.context && !control_.holds(control_.context, generation_, owner_.c_str()))) {
    error = "stale_screen_lease";
    return false;
  }
  if (frame.pixels == nullptr || frame.width != backend_.width ||
      frame.height != backend_.height || frame.stride < frame.width ||
      (frame.format != ScreenPixelFormat::Rgb565 &&
       frame.format != ScreenPixelFormat::Rgb565ByteSwapped)) {
    error = "invalid_screen_frame";
    return false;
  }
  if (region.x < 0 || region.y < 0 || region.width < 0 ||
      region.height < 0 || region.x > frame.width ||
      region.y > frame.height || region.width > frame.width - region.x ||
      region.height > frame.height - region.y) {
    error = "invalid_screen_region";
    return false;
  }
  if (region.width == 0 || region.height == 0) {
    error.clear();
    return true;
  }
  if ((!backend_.capabilities.partial_updates &&
       (region.x != 0 || region.y != 0 || region.width != frame.width ||
        region.height != frame.height)) ||
      region.x % backend_.capabilities.x_alignment != 0 ||
      region.y % backend_.capabilities.y_alignment != 0 ||
      (region.x + region.width != frame.width &&
       region.width % backend_.capabilities.x_alignment != 0) ||
      (region.y + region.height != frame.height &&
       region.height % backend_.capabilities.y_alignment != 0)) {
    error = "unsupported_screen_region";
    return false;
  }
  if (backend_.present == nullptr) {
    error = "screen_frame_unsupported";
    return false;
  }
  if (!backend_.present(backend_.context, frame, region, error)) {
    if (error.empty()) error = "screen_present_failed";
    return false;
  }
  error.clear();
  return true;
}

bool ScreenHost::present_patch(ScreenLease lease, ScreenPatchView patch,
                               ScreenRegion destination, std::string &error) {
  if (!acquired_ || lease.generation == 0 || lease.generation != generation_ ||
      (control_.context && !control_.holds(control_.context, generation_, owner_.c_str()))) {
    error = "stale_screen_lease";
    return false;
  }
  if (patch.pixels == nullptr || patch.width < 0 || patch.height < 0 ||
      patch.stride < patch.width || patch.width != destination.width ||
      patch.height != destination.height ||
      (patch.format != ScreenPixelFormat::Rgb565 &&
       patch.format != ScreenPixelFormat::Rgb565ByteSwapped)) {
    error = "invalid_screen_patch";
    return false;
  }
  if (destination.x < 0 || destination.y < 0 || destination.width < 0 ||
      destination.height < 0 || destination.x > backend_.width ||
      destination.y > backend_.height ||
      destination.width > backend_.width - destination.x ||
      destination.height > backend_.height - destination.y) {
    error = "invalid_screen_region";
    return false;
  }
  if (destination.width == 0 || destination.height == 0) {
    error.clear();
    return true;
  }
  if ((!backend_.capabilities.partial_updates &&
       (destination.x != 0 || destination.y != 0 ||
        destination.width != backend_.width ||
        destination.height != backend_.height)) ||
      destination.x % backend_.capabilities.x_alignment != 0 ||
      destination.y % backend_.capabilities.y_alignment != 0 ||
      (destination.x + destination.width != backend_.width &&
       destination.width % backend_.capabilities.x_alignment != 0) ||
      (destination.y + destination.height != backend_.height &&
       destination.height % backend_.capabilities.y_alignment != 0)) {
    error = "unsupported_screen_region";
    return false;
  }
  if (backend_.present_patch == nullptr) {
    error = "screen_patch_unsupported";
    return false;
  }
  if (!backend_.present_patch(backend_.context, patch, destination, error)) {
    if (error.empty()) error = "screen_present_failed";
    return false;
  }
  error.clear();
  return true;
}

bool ScreenHost::wait_idle(ScreenLease lease, std::string &error) {
  if (!acquired_ || lease.generation == 0 ||
      lease.generation != generation_) {
    error = "stale_screen_lease";
    return false;
  }
  if (backend_.wait_idle != nullptr &&
      !backend_.wait_idle(backend_.context, error)) {
    if (error.empty()) error = "screen_wait_idle_failed";
    return false;
  }
  error.clear();
  return true;
}

bool ScreenHost::release(ScreenLease lease, std::string &error) {
  if (!wait_idle(lease, error)) return false;
  if (control_.context && !control_.release(control_.context, control_output_, generation_, error)) return false;
  acquired_ = false;
  owner_.clear();
  error.clear();
  return true;
}

} // namespace lunaredu::display
