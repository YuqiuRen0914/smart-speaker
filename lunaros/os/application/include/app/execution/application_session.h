#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include "app/execution/draw_list.h"
#include "app/execution/application_device.h"
namespace lunaredu::lunaros::apps {
enum class SessionObservationPhase : uint8_t {
  Opening, Opened, OpenFailed, CallStarted, CallReturned, CallFailed,
  HostStarted, HostReturned, HostFailed, Closing, Closed
};
struct SessionObservation {
  SessionObservationPhase phase;
  const char *operation;
  const char *code = nullptr;
};
struct SessionStorage {
  void *context = nullptr;
  int (*read)(void *, uint8_t *, size_t) = nullptr;
  bool (*write)(void *, const uint8_t *, size_t) = nullptr;
  bool (*cancelled)(void *) = nullptr;
  void *cancel_context = nullptr;
  // Optional synchronous observation on the existing VM owner. No guest text,
  // payload, pointer or exception contents. Context outlives session teardown.
  void (*observe)(void *, const SessionObservation &) = nullptr;
  void *observation_context = nullptr;
  SessionDevice device{};
  // Explicit isolated save validation: instantiate but do not execute init,
  // tick, stop or device effects. The validator remains independently metered.
  bool validation_only=false;
};
// Candidate bytes may change for validation; cancellation, observation and
// authority always come from the live session. Validation cannot write saves
// or replace the session's device binding.
SessionStorage session_validation_storage(const SessionStorage &owner,SessionStorage candidate);
// Shared backend/session contract for independently launched applications.
// The caller owns admission, input and storage callback lifetime.
class ApplicationSession {
public:
  virtual ~ApplicationSession() = default;
  virtual bool open(const uint8_t *, size_t, SessionStorage, uint32_t,
                    std::string &) = 0;
  virtual bool event(int, int, int, int, std::string &) = 0;
  virtual bool checkpoint(std::string &) = 0;
  virtual bool validate_save(SessionStorage, std::string &) = 0;
  // Failure retains the session and callback contexts for a later retry.
  virtual bool close(std::string &) = 0;
  virtual bool active() const = 0;
  virtual std::shared_ptr<const DrawList> frame() const = 0;
};
}
