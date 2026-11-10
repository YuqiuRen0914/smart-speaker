#include "pairing_activity.h"
#include <algorithm>
#include <cstring>
namespace lunaredu::lunaros::platform_link {
namespace {
using Phase = PlatformLinkPairingActivityPhase;
bool terminal(Phase p) { return p == Phase::Succeeded || p == Phase::Failed || p == Phase::Cancelled; }
bool bound(const PlatformLinkBindingStatus &s) { return s.state == PlatformLinkBindingState::Bound && !s.persistence_pending && !s.revocation_requested; }
bool allowed(const char *caller) {
  if (!caller) return false;
  for (const char *id : {"settings", "pet", "assistant", "product"}) if (!std::strcmp(caller, id)) return true;
  return false;
}
}
PairingActivityOwner::PairingActivityOwner(PairingActivityPorts ports, uint64_t generation) : ports_(ports), generation_(generation) {}
void PairingActivityOwner::update() {
  if (!snapshot_.activity.generation || snapshot_.phase == Phase::Cancelled) return;
  const auto observed = ports_.observe(ports_.context);
  if (snapshot_.phase == Phase::Succeeded && bound(observed.state.binding) &&
      observed.state.binding.binding_epoch == snapshot_.pairing.binding.binding_epoch) return;
  snapshot_.error.clear();
  snapshot_.pairing = observed.state;
  const auto expiry = observed.state.expires_at_ms;
  snapshot_.remaining_ms = expiry > observed.now_ms ? uint32_t(std::min<uint64_t>(uint64_t(expiry)-uint64_t(observed.now_ms),UINT32_MAX)) : 0;
  if (observed.session != session_) {
    snapshot_.phase = Phase::Cancelled; snapshot_.pairing = {}; snapshot_.remaining_ms = 0; return;
  }
  if (bound(observed.state.binding)) snapshot_.phase = Phase::Succeeded;
  else if (cancelling_) snapshot_.phase = !owned_ || (!observed.pending && observed.state.code.empty()) ? Phase::Cancelled : Phase::Cancelling;
  else if (observed.pending) snapshot_.phase = Phase::Preparing;
  else if (!observed.state.error.empty()) { snapshot_.phase = Phase::Failed; snapshot_.error = observed.state.error; }
  else if (expiry && !snapshot_.remaining_ms) { snapshot_.phase = Phase::Failed; snapshot_.error = "pairing_code_expired"; }
  else if (!observed.state.code.empty()) snapshot_.phase = Phase::Waiting;
  else snapshot_.phase = Phase::Cancelled;
  if (terminal(snapshot_.phase)) { snapshot_.pairing.code.clear(); snapshot_.pairing.bind_url.clear(); snapshot_.remaining_ms = 0; }
}
bool PairingActivityOwner::authorize(const char *caller, PlatformLinkPairingActivityHandle handle, std::string &error) const {
  if (!allowed(caller)) { error = "pairing_activity_caller_denied"; return false; }
  if (!handle.generation || handle.generation != snapshot_.activity.generation) { error = "stale_pairing_activity"; return false; }
  if (caller_ != caller) { error = "pairing_activity_owner_mismatch"; return false; }
  error.clear(); return true;
}
bool PairingActivityOwner::begin(const char *caller, PlatformLinkPairingActivityHandle &handle, std::string &error, bool refresh) {
  std::lock_guard<std::mutex> lock(mutex_); error.clear();
  if (!allowed(caller)) { error = "pairing_activity_caller_denied"; return false; }
  update();
  const auto current = ports_.observe(ports_.context);
  const bool retained = snapshot_.phase != Phase::Cancelled && session_ == current.session && (current.pending || !current.state.code.empty());
  if (snapshot_.activity.generation && (!terminal(snapshot_.phase) || retained)) {
    if (caller_ != caller) { error = "pairing_activity_busy"; return false; }
    if (refresh && (snapshot_.phase == Phase::Waiting || snapshot_.phase == Phase::Failed)) {
      const bool expired = current.state.expires_at_ms > 0 && current.state.expires_at_ms <= current.now_ms;
      if (!owned_ && !expired) { error = "pairing_activity_read_only"; return false; }
      uint64_t session = session_; bool owned = owned_;
      if (!ports_.claim(ports_.context, true, session, owned, error)) return false;
      if (session != session_) { error = "stale_pairing_session"; return false; }
      owned_ = owned;
      snapshot_.phase = Phase::Preparing;
    }
    handle = snapshot_.activity; return true;
  }
  if (generation_ == UINT64_MAX) { error = "pairing_activity_generation_exhausted"; return false; }
  const auto before = ports_.observe(ports_.context);
  uint64_t session = before.session; bool owned = false;
  if (!bound(before.state.binding) && !ports_.claim(ports_.context, false, session, owned, error)) return false;
  snapshot_ = {}; snapshot_.activity.generation = ++generation_; snapshot_.phase = Phase::Preparing;
  session_ = session; owned_ = owned; cancelling_ = false; caller_ = caller;
  handle = snapshot_.activity; update(); return true;
}
bool PairingActivityOwner::read(const char *caller, PlatformLinkPairingActivityHandle handle, PlatformLinkPairingActivitySnapshot &out, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!authorize(caller, handle, error)) return false;
  update(); out = snapshot_; return true;
}
bool PairingActivityOwner::cancel(const char *caller, PlatformLinkPairingActivityHandle handle, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!authorize(caller, handle, error)) return false;
  update(); if (snapshot_.phase == Phase::Succeeded || snapshot_.phase == Phase::Cancelled || cancelling_) return true;
  if (owned_ && !ports_.release(ports_.context, session_, error)) return false;
  cancelling_ = true; snapshot_.phase = Phase::Cancelling; update(); return true;
}
}
