#pragma once
#include <cstdint>
#include <mutex>
#include <string>

namespace lunaredu::lunaros {
// The existing duplex I2S bridge is one indivisible resource. Focus changes
// control access, never carry PCM. A resident must stop all in-flight I/O
// before its suspend callback succeeds. Callbacks may read snapshot(), but
// must not recursively change focus. No thread or timer is allocated.
// Resume prepares resident state while I/O admission remains closed; workers
// regain admission only after the callback succeeds.
struct AudioFocusLease { uint64_t generation = 0; };
struct AudioFocusSnapshot {
  std::string owner;
  bool microphone = false;
  bool speaker = false;
  uint64_t generation = 0;
  bool recovery_pending = false;
};
using AudioResidentTransition = bool (*)(void *, bool suspended, std::string &error);
class ResidentAudioAccess;
class AudioFocusBroker {
public:
  bool bind_resident(AudioResidentTransition transition, void *context, std::string &error);
  bool acquire(const char *owner, bool microphone, bool speaker,
               AudioFocusLease &lease, std::string &error);
  bool release(AudioFocusLease &lease, std::string &error);
  // release failures retain the lease and deny resident I/O until retried.
  // Failed acquire compensation is broker-owned: an owner tick may retry it,
  // and the next acquire also retries before granting a new lease.
  bool recover_resident(std::string &error);
  AudioFocusSnapshot snapshot() const;
private:
  friend class ResidentAudioAccess;
  std::mutex io_mutex_;
  mutable std::mutex mutex_;
  std::mutex transition_mutex_;
  AudioFocusSnapshot state_;
  AudioResidentTransition transition_ = nullptr;
  void *context_ = nullptr;
  uint64_t generation_ = 0;
  bool recover_resident_locked(std::string &error);
};
AudioFocusBroker &audio_focus();
// Guard each bounded resident I/O operation, including teardown callbacks.
// Handoff waits for the current operation and rejects stale I/O afterward.
class ResidentAudioAccess {
public:
  explicit ResidentAudioAccess(AudioFocusBroker &broker = audio_focus());
  explicit operator bool() const { return allowed_; }
private:
  std::unique_lock<std::mutex> lock_;
  bool allowed_ = false;
};
} // namespace lunaredu::lunaros
