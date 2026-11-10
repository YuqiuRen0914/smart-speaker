#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <array>

namespace lunaredu::lunaros {

inline constexpr size_t kDeviceActivityCapacity = 32;
inline constexpr size_t kDeviceActivityIdentifierBytes = 128;
inline constexpr size_t kDeviceActivityMessageBytes = 512;
inline constexpr size_t kDeviceActivityResultBytes = 4096;

enum class DeviceActivityState : uint8_t {
  Accepted,
  Running,
  Succeeded,
  Failed,
  Canceled,
  TimedOut,
};

const char *device_activity_state_name(DeviceActivityState state);
bool device_activity_terminal(DeviceActivityState state);

struct DeviceActivitySnapshot {
  std::string activity_id;
  std::string request_id;
  std::string capability_id;
  std::string operation_id;
  DeviceActivityState state = DeviceActivityState::Accepted;
  std::string accepted_at;
  std::string started_at;
  std::string updated_at;
  std::string finished_at;
  int progress_percent = 0;
  std::string progress_message;
  std::string result_json;
  std::string error_code;
  std::string error_message;
  bool cancelable = false;
  bool details_truncated = false, result_omitted = false;
  uint32_t revision = 0;
  uint32_t ingress_request_sequence = 0;
  uint64_t trace_id = 0;
};

void format_device_request_id(char *out, size_t capacity, uint32_t sequence);
struct DeviceActivityObservation {
  std::array<char, 32> activity_id{}, request_id{};
  std::array<char, 64> capability_id{}, operation_id{}, error_code{};
  DeviceActivityState state = DeviceActivityState::Accepted;
  uint64_t trace_id = 0;
  uint32_t revision = 0;
  bool truncated = false;
};
struct DeviceActivityObservationSummary {
  size_t total = 0, returned = 0;
  uint32_t revision_before = 0, revision_after = 0;
  bool changed = false;
};
using DeviceActivityObservationVisitor = void (*)(const DeviceActivityObservation &, void *);

using DeviceActivityVisitor =
    void (*)(const DeviceActivitySnapshot &activity, void *context);

// Device-owned, bounded activity history shared by every transport and App
// runtime. It records protocol-visible intent and terminal feedback; business
// implementations still own their work and call transition methods explicitly.
class DeviceActivityRegistry {
public:
  DeviceActivityRegistry();
  ~DeviceActivityRegistry();
  DeviceActivityRegistry(const DeviceActivityRegistry &) = delete;
  DeviceActivityRegistry &operator=(const DeviceActivityRegistry &) = delete;

  void reset();
  // Protect nonterminal activities. Empty means rejected (invalid/full/exhausted).
  // Identifier limit 128 bytes, messages 512, terminal result 4096.
  // Only the oldest terminal history may be replaced. Oversized terminal result
  // bodies are omitted, never cut into invalid JSON, and explicitly reported.
  std::string accept(const char *capability_id, const char *operation_id,
                     const char *message, bool cancelable = false);
  bool start(const char *activity_id, const char *message = nullptr);
  bool progress(const char *activity_id, int percent, const char *message);
  bool succeed(const char *activity_id, const char *result_json = nullptr,
               const char *message = nullptr);
  bool fail(const char *activity_id, const char *error_code,
            const char *error_message);
  bool cancel(const char *activity_id, const char *message = nullptr);
  bool get(const char *activity_id, DeviceActivitySnapshot &snapshot) const;
  void visit(DeviceActivityVisitor visitor, void *context) const;
  uint32_t revision() const;
  // Explicitly links returned work from its trusted adapter. Existing links cannot be stolen.
  bool correlate(const char *activity_id, const char *capability_id, const char *operation_id,
                 uint32_t request_sequence, uint64_t trace_id);
  // Copies one bounded summary at a time; never copies result bodies or the full history.
  DeviceActivityObservationSummary observe(size_t offset, size_t limit,
      DeviceActivityObservationVisitor visitor, void *context) const;

private:
  struct Impl;
  Impl *impl_ = nullptr;
};

} // namespace lunaredu::lunaros
