#pragma once
#include <cstdint>
#include <mutex>
#include <string>

namespace lunaredu::lunaros { class OutputControl; }
namespace lunaredu::audio {
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
  bool configured = false;
  bool closing = false;
};
using AudioResidentTransition = bool (*)(void *, bool suspended, std::string &error);
struct AudioDevicePorts {
  void *context = nullptr;
  // Succeeds only after both physical I/O lanes and submitted PCM are idle.
  // False retains ownership. Must be bounded and idempotent on retry.
  bool (*quiesce)(void *, std::string &) = nullptr;
};
class ResidentAudioAccess;
enum class ResidentAudioLane { Playback, Capture };
class AudioFocusBroker {
public:
  bool configure(lunaros::OutputControl &control, const char *output, AudioDevicePorts device,
                    std::string &error);
  bool attach_resident(AudioResidentTransition transition, void *context, std::string &error);
  bool detach_resident(AudioResidentTransition, void *context, std::string &error);
  // Audio owner closes admission, drains both I/O lanes and calls the bounded
  // backend stop. Failure retains all borrowed bindings for a later retry.
  bool stop(bool (*stop_backend)(void *, std::string &), void *context, std::string &error);
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
  std::mutex capture_io_mutex_;
  mutable std::mutex mutex_;
  std::mutex transition_mutex_;
  AudioFocusSnapshot state_;
  AudioDevicePorts device_;
  AudioResidentTransition transition_ = nullptr;
  void *context_ = nullptr;
  uint64_t pending_id_ = 0;
  lunaros::OutputControl *control_ = nullptr;
  const char *control_output_ = nullptr;
  const char *provider_ = nullptr;
  bool configured_ = false;
  bool closing_ = false;
  bool resident_transition_ = false;
  bool recover_resident_locked(std::string &error);
};
// One bridge-owned physical audio device. The Audio lifecycle pins its Core
// provider until stop succeeds and releases all borrowed bindings. Independent
// physical devices inject a separately configured broker.
AudioFocusBroker &local_audio_focus();
// Guard each bounded resident I/O operation, including teardown callbacks.
// Handoff waits for the current operation and rejects stale I/O afterward.
class ResidentAudioAccess {
public:
  explicit ResidentAudioAccess(AudioFocusBroker &broker = local_audio_focus(),
                               ResidentAudioLane lane = ResidentAudioLane::Playback);
  explicit operator bool() const { return allowed_; }
private:
  std::unique_lock<std::mutex> lock_;
  bool allowed_ = false;
};
} // namespace lunaredu::audio
