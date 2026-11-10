#include "lunaredu_lunaros/app/audio_focus.h"
#include <cstring>
namespace lunaredu::lunaros {
AudioFocusBroker &audio_focus() { static AudioFocusBroker broker; return broker; }
ResidentAudioAccess::ResidentAudioAccess(AudioFocusBroker &broker)
    : lock_(broker.io_mutex_) {
  std::lock_guard<std::mutex> state(broker.mutex_);
  allowed_ = broker.state_.owner.empty() && !broker.state_.recovery_pending;
  if (!allowed_) lock_.unlock();
}
bool AudioFocusBroker::bind_resident(AudioResidentTransition transition, void *context,
                                    std::string &error) {
  std::unique_lock<std::mutex> operation(transition_mutex_, std::try_to_lock);
  if (!operation.owns_lock()) { error = "audio_focus_transition_busy"; return false; }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!transition || !state_.owner.empty() || state_.recovery_pending ||
      (transition_ && (transition_ != transition || context_ != context))) {
    error = "audio_resident_binding_conflict"; return false;
  }
  transition_ = transition; context_ = context; error.clear(); return true;
}
bool AudioFocusBroker::acquire(const char *owner, bool microphone, bool speaker,
                              AudioFocusLease &lease, std::string &error) {
  if (!owner || !*owner || std::strlen(owner) > 63 || (!microphone && !speaker) ||
      lease.generation != 0) { error = "invalid_audio_focus_request"; return false; }
  std::unique_lock<std::mutex> operation(transition_mutex_, std::try_to_lock);
  if (!operation.owns_lock()) { error = "audio_focus_transition_busy"; return false; }
  if (!recover_resident_locked(error)) return false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.owner.empty()) { error = "audio_focus_owned_by:" + state_.owner; return false; }
    if (++generation_ == 0) ++generation_;
    state_ = {owner, microphone, speaker, generation_};
  }
  // Publish denial, then wait for the last admitted driver operation. Future
  // resident operations cannot reopen or close the new owner's channels.
  { std::lock_guard<std::mutex> io(io_mutex_); }
  // The transition also clears resident codec/queue state.
  if (transition_ && !transition_(context_, true, error)) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      state_ = {};
      state_.recovery_pending = true;
    }
    std::string recovery_error;
    (void)recover_resident_locked(recovery_error);
    if (error.empty()) error = "audio_resident_suspend_failed";
    if (!recovery_error.empty()) error += "; recovery: " + recovery_error;
    return false;
  }
  lease.generation = generation_;
  error.clear(); return true;
}
bool AudioFocusBroker::release(AudioFocusLease &lease, std::string &error) {
  std::unique_lock<std::mutex> operation(transition_mutex_, std::try_to_lock);
  if (!operation.owns_lock()) { error = "audio_focus_transition_busy"; return false; }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lease.generation || lease.generation != state_.generation) {
      error = "stale_audio_focus_lease"; return false;
    }
    state_.recovery_pending = true;
  }
  if (transition_ && !transition_(context_, false, error)) {
    if (error.empty()) error = "audio_resident_resume_failed";
    return false;
  }
  { std::lock_guard<std::mutex> lock(mutex_); state_ = {}; lease = {}; }
  error.clear(); return true;
}
bool AudioFocusBroker::recover_resident_locked(std::string &error) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.recovery_pending) { error.clear(); return true; }
    if (!state_.owner.empty()) { error = "audio_focus_release_required"; return false; }
  }
  if (transition_ && !transition_(context_, false, error)) {
    if (error.empty()) error = "audio_resident_resume_failed";
    return false;
  }
  { std::lock_guard<std::mutex> lock(mutex_); state_ = {}; }
  error.clear(); return true;
}
bool AudioFocusBroker::recover_resident(std::string &error) {
  std::unique_lock<std::mutex> operation(transition_mutex_, std::try_to_lock);
  if (!operation.owns_lock()) { error = "audio_focus_transition_busy"; return false; }
  return recover_resident_locked(error);
}
AudioFocusSnapshot AudioFocusBroker::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_); return state_;
}
} // namespace lunaredu::lunaros
