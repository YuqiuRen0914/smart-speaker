#include "activity.h"
#include <cstring>
namespace lunaredu::lunaros::provisioning {
namespace {
using Phase = ProvisioningActivityPhase;
bool terminal(Phase p) { return p == Phase::Succeeded || p == Phase::Failed || p == Phase::Cancelled; }
bool allowed(const char *caller) {
  if (!caller) return false;
  for (const char *id : {"settings", "pet", "assistant", "product"}) if (!std::strcmp(caller, id)) return true;
  return false;
}
}
ActivityOwner::ActivityOwner(ActivityPorts ports, uint64_t generation) : ports_(ports), generation_(generation) {}
void ActivityOwner::update() {
  if (!snapshot_.activity.generation || snapshot_.phase == Phase::Succeeded || snapshot_.phase == Phase::Cancelled) return;
  const auto observed = ports_.observe(ports_.context);
  snapshot_.error.clear();
  snapshot_.provisioning = observed.state;
  snapshot_.url = observed.url; snapshot_.password = observed.password;
  if (observed.state.session != session_ || !observed.state.started) {
    snapshot_.phase = Phase::Cancelled; snapshot_.provisioning = {}; snapshot_.url.clear(); snapshot_.password.clear(); return;
  }
  if (cancelling_) {
    snapshot_.phase = !owned_ || observed.state.state == ProvisioningState::Idle ? Phase::Cancelled : Phase::Cancelling;
  } else if (observed.state.state == ProvisioningState::Stopping) {
    snapshot_.phase = Phase::Cancelling;
  } else if (observed.state.state == ProvisioningState::Idle) {
    const auto &progress = observed.state.progress;
    snapshot_.phase = progress.attempt != initial_attempt_ && progress.phase == "connected" && !progress.ip.empty()
        ? Phase::Succeeded : observed.state.expired || !observed.state.error.empty() ? Phase::Failed : Phase::Cancelled;
    if (observed.state.expired) snapshot_.error = "provisioning_window_expired";
    else snapshot_.error = observed.state.error;
  } else if (!observed.state.error.empty()) {
    snapshot_.phase = Phase::Failed;
    snapshot_.error = observed.state.error;
  } else {
    snapshot_.phase = observed.state.state == ProvisioningState::Preparing ? Phase::Preparing : Phase::Waiting;
    snapshot_.error = observed.state.progress.error; // A credential retry may continue in the same window.
  }
  if (terminal(snapshot_.phase)) { snapshot_.url.clear(); snapshot_.password.clear(); }
}
bool ActivityOwner::authorize(const char *caller, ProvisioningActivityHandle handle, std::string &error) const {
  if (!allowed(caller)) { error = "provisioning_activity_caller_denied"; return false; }
  if (!handle.generation || handle.generation != snapshot_.activity.generation) { error = "stale_provisioning_activity"; return false; }
  if (caller_ != caller) { error = "provisioning_activity_owner_mismatch"; return false; }
  error.clear(); return true;
}
bool ActivityOwner::begin(const char *caller, ProvisioningActivityHandle &handle, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_); error.clear();
  if (!allowed(caller)) { error = "provisioning_activity_caller_denied"; return false; }
  update();
  const auto current = ports_.observe(ports_.context);
  const bool retained = snapshot_.phase != Phase::Cancelled && session_ == current.state.session && current.state.state != ProvisioningState::Idle;
  if (snapshot_.activity.generation && (!terminal(snapshot_.phase) || retained)) {
    if (caller_ != caller) { error = "provisioning_activity_busy"; return false; }
    handle = snapshot_.activity; return true;
  }
  if (generation_ == UINT64_MAX) { error = "provisioning_activity_generation_exhausted"; return false; }
  const auto before = ports_.observe(ports_.context);
  initial_attempt_ = before.state.state != ProvisioningState::Idle && before.state.progress.phase != "connected" ? 0 : before.state.progress.attempt;
  uint64_t session = 0; bool owned = false;
  if (!ports_.claim(ports_.context, session, owned, error)) return false;
  snapshot_ = {}; snapshot_.activity.generation = ++generation_; snapshot_.phase = Phase::Preparing;
  session_ = session; owned_ = owned; cancelling_ = false; caller_ = caller;
  handle = snapshot_.activity; update(); return true;
}
bool ActivityOwner::read(const char *caller, ProvisioningActivityHandle handle, ProvisioningActivitySnapshot &out, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!authorize(caller, handle, error)) return false;
  update(); out = snapshot_; return true;
}
bool ActivityOwner::cancel(const char *caller, ProvisioningActivityHandle handle, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!authorize(caller, handle, error)) return false;
  update(); if (snapshot_.phase == Phase::Succeeded || snapshot_.phase == Phase::Cancelled || cancelling_) return true;
  if (owned_ && !ports_.release(ports_.context, session_, error)) return false;
  cancelling_ = true; snapshot_.phase = Phase::Cancelling; update(); return true;
}
}
