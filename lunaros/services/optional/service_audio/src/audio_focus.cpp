#include "lunaros_audio_focus.h"
#include "control/output_control.h"
#include <cstring>

namespace lunaredu::audio {
namespace {
bool fail(std::string &error, const char *reason) { error = reason; return false; }
}
AudioFocusBroker &local_audio_focus() { static AudioFocusBroker broker; return broker; }
bool AudioFocusBroker::configure(lunaros::OutputControl &control, const char *output, AudioDevicePorts device,
                                std::string &error) {
  std::unique_lock<std::mutex> operation(transition_mutex_, std::try_to_lock);
  if (!operation.owns_lock()) return fail(error, "audio_focus_transition_busy");
  const auto provider = control.observe(output, 0).provider;
  if (!device.quiesce || !control.ready(output, provider.id) || provider.maximum_lease_ms || provider.preemptible)
    return fail(error, "invalid_audio_control_provider");
  std::lock_guard<std::mutex> lock(mutex_);
  if (closing_ || !state_.owner.empty() || state_.recovery_pending ||
      (control_ && (control_ != &control || std::strcmp(control_output_, output) ||
                    device_.context != device.context || device_.quiesce != device.quiesce)))
    return fail(error, "audio_control_binding_conflict");
  if (!control.attach_provider(output, provider.id, this, error)) return false;
  control_ = &control; control_output_ = output; provider_ = provider.id;
  device_ = device; configured_ = true; error.clear(); return true;
}
ResidentAudioAccess::ResidentAudioAccess(AudioFocusBroker &broker, ResidentAudioLane lane)
    : lock_(lane == ResidentAudioLane::Capture ? broker.capture_io_mutex_ : broker.io_mutex_) {
  std::lock_guard<std::mutex> state(broker.mutex_);
  allowed_ = broker.configured_ && !broker.closing_ && !broker.resident_transition_ &&
      broker.state_.owner.empty() && !broker.state_.recovery_pending;
  if (!allowed_) lock_.unlock();
}
bool AudioFocusBroker::attach_resident(AudioResidentTransition transition, void *context,
                                     std::string &error) {
  std::unique_lock<std::mutex> operation(transition_mutex_, std::try_to_lock);
  if (!operation.owns_lock()) return fail(error, "audio_focus_transition_busy");
  std::lock_guard<std::mutex> lock(mutex_);
  if (!configured_ || closing_ || !transition || !state_.owner.empty() || state_.recovery_pending ||
      (transition_ && (transition_ != transition || context_ != context)))
    return fail(error, "audio_resident_binding_conflict");
  transition_ = transition; context_ = context; error.clear(); return true;
}
bool AudioFocusBroker::detach_resident(AudioResidentTransition transition, void *context,
                                     std::string &error) {
  std::unique_lock<std::mutex> operation(transition_mutex_, std::try_to_lock);
  if (!operation.owns_lock()) return fail(error, "audio_focus_transition_busy");
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!transition_ && !state_.recovery_pending && !state_.owner.size()) { error.clear(); return true; }
    if (!state_.owner.empty() || state_.recovery_pending || closing_ ||
        transition_ != transition || context_ != context)
      return fail(error, "audio_resident_retirement_required");
    resident_transition_ = true;
  }
  { std::lock_guard<std::mutex> io(io_mutex_); }
  { std::lock_guard<std::mutex> io(capture_io_mutex_); }
  std::lock_guard<std::mutex> lock(mutex_);
  transition_ = nullptr; context_ = nullptr; resident_transition_ = false;
  error.clear(); return true;
}
bool AudioFocusBroker::acquire(const char *owner, bool microphone, bool speaker,
                              AudioFocusLease &lease, std::string &error) {
  if (!owner || !*owner || std::strlen(owner) > 63 || (!microphone && !speaker) || lease.generation)
    return fail(error, "invalid_audio_focus_request");
  std::unique_lock<std::mutex> operation(transition_mutex_, std::try_to_lock);
  if (!operation.owns_lock()) return fail(error, "audio_focus_transition_busy");
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!configured_ || closing_) return fail(error, "audio_focus_unavailable");
    if (!state_.owner.empty()) return fail(error, "audio_focus_owned");
  }
  if (!recover_resident_locked(error)) return false;
  lunaros::OutputControl::Admission admission;
  if (!control_->admit(control_output_, owner, 0, 0, 0, admission, error)) return false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_id_ = admission.requested.id;
    state_ = {owner, microphone, speaker, pending_id_};
  }
  // Publish denial before draining; neither candidate nor retiring lease is usable.
  { std::lock_guard<std::mutex> io(io_mutex_); }
  { std::lock_guard<std::mutex> io(capture_io_mutex_); }
  if ((transition_ && !transition_(context_, true, error)) || !device_.quiesce(device_.context, error)) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      state_.owner.clear(); state_.recovery_pending = true;
    }
    std::string recovery;
    (void)recover_resident_locked(recovery);
    if (error.empty()) error = "audio_resident_suspend_failed";
    if (!recovery.empty()) error += "; recovery: " + recovery;
    return false;
  }
  lunaros::OutputControl::Lease granted;
  if (!control_->confirm(pending_id_, provider_, 0, granted, error)) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.owner.clear(); state_.recovery_pending = true;
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_id_ = 0; lease.generation = granted.id;
  }
  error.clear(); return true;
}
bool AudioFocusBroker::release(AudioFocusLease &lease, std::string &error) {
  std::unique_lock<std::mutex> operation(transition_mutex_, std::try_to_lock);
  if (!operation.owns_lock()) return fail(error, "audio_focus_transition_busy");
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lease.generation || lease.generation != state_.generation || state_.owner.empty() || pending_id_)
      return fail(error, "stale_audio_focus_lease");
    if (!control_->begin_retirement(lease.generation, provider_, error)) return false;
    state_.recovery_pending = true;
  }
  // Caller retires its producer; Audio confirms deferred physical completion.
  if (!device_.quiesce(device_.context, error)) {
    if (error.empty()) error = "audio_foreground_stop_pending";
    return false;
  }
  if (transition_ && !transition_(context_, false, error)) {
    if (error.empty()) error = "audio_resident_resume_failed";
    return false;
  }
  if (!control_->retire(lease.generation, provider_, error)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = {}; lease = {}; error.clear(); return true;
}
bool AudioFocusBroker::recover_resident_locked(std::string &error) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.recovery_pending) { error.clear(); return true; }
    if (!state_.owner.empty()) return fail(error, "audio_focus_release_required");
  }
  if (transition_ && !transition_(context_, false, error)) {
    if (error.empty()) error = "audio_resident_resume_failed";
    return false;
  }
  if (pending_id_ && !control_->cancel(pending_id_, provider_, error)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  pending_id_ = 0; state_ = {}; error.clear(); return true;
}
bool AudioFocusBroker::recover_resident(std::string &error) {
  std::unique_lock<std::mutex> operation(transition_mutex_, std::try_to_lock);
  if (!operation.owns_lock()) return fail(error, "audio_focus_transition_busy");
  return recover_resident_locked(error);
}
bool AudioFocusBroker::stop(bool (*stop_backend)(void *, std::string &), void *context,
                          std::string &error) {
  if (!stop_backend) return fail(error, "invalid_audio_stop");
  std::unique_lock<std::mutex> operation(transition_mutex_, std::try_to_lock);
  if (!operation.owns_lock()) return fail(error, "audio_focus_transition_busy");
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.owner.empty() || state_.recovery_pending)
      return fail(error, "audio_focus_retirement_required");
    closing_ = true;
  }
  { std::lock_guard<std::mutex> io(io_mutex_); }
  { std::lock_guard<std::mutex> io(capture_io_mutex_); }
  if (!stop_backend(context, error)) {
    if (error.empty()) error = "audio_backend_stop_pending";
    return false;
  }
  if (control_ && !control_->detach_provider(control_output_, provider_, this, error)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  transition_ = nullptr; context_ = nullptr; control_ = nullptr;
  control_output_ = provider_ = nullptr; configured_ = closing_ = false;
  state_ = {}; device_ = {}; error.clear(); return true;
}
AudioFocusSnapshot AudioFocusBroker::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto result = state_; result.configured = configured_; result.closing = closing_; return result;
}
} // namespace lunaredu::audio
