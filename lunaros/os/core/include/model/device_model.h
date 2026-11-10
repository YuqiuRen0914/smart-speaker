#pragma once
#include "control/output_control.h"

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <atomic>

#include "model/device_activity.h"

namespace lunaredu::lunaros {

inline constexpr size_t kDeviceCapabilityCapacity = 64;

struct DeviceCapabilityDescriptor {
  const char *id = nullptr;
  const char *const *presence_capabilities = nullptr;
  size_t presence_capability_count = 0;
};

using DeviceCapabilityVisitor =
    void (*)(const DeviceCapabilityDescriptor &descriptor, void *context);

// Transport-neutral meaning of a device operation. HTTP verbs, JSON codecs,
// status codes and request-body readers are projections and must not leak into
// this model.
enum class DeviceOperationKind : uint8_t {
  Query,
  Command,
  ResourceRead,
  ResourceWrite,
  Upload,
  Stream,
};

enum class DeviceOperationExecution : uint8_t {
  OwnerExecutor,
  // The implementation synchronizes its own state. Blocking transport work
  // stays on the invoking task and must not monopolize a control executor.
  Caller,
};

enum class DeviceDiagnosticAccess : uint8_t {
  None,
  // Available on a private LAN to obtain the short-lived serial challenge session.
  Bootstrap,
  // Requires the active session token obtained from the physical serial code.
  Session,
};

// Explicit permission for delayed PlatformLink execution. Unspecified operations
// remain local even when their JSON body happens to be small.
enum class DeviceRemoteAccess : uint8_t { None, Query, Command };

struct DeviceOperationSpec {
  const char *id = nullptr;
  DeviceOperationKind kind = DeviceOperationKind::Query;
  bool mutating = false;
  // OwnerExecutor requires a LunarOS executor ID. Caller requires nullptr
  // and an internally synchronized implementation, independent of transport.
  const char *owner_executor = nullptr;
  const char *request_schema = nullptr;
  const char *response_schema = nullptr;
  DeviceOperationExecution execution = DeviceOperationExecution::OwnerExecutor;
  // True only when the owner can observe a cancellation request and stop the
  // accepted work. Operation kind alone never implies cancel support.
  bool cancellable = false;
  DeviceDiagnosticAccess diagnostic_access = DeviceDiagnosticAccess::None;
  DeviceRemoteAccess remote_access = DeviceRemoteAccess::None;
};

constexpr bool device_operation_execution_valid(const DeviceOperationSpec &op) {
  return op.execution == DeviceOperationExecution::Caller
      ? op.owner_executor == nullptr
      : op.execution == DeviceOperationExecution::OwnerExecutor &&
        op.owner_executor != nullptr && op.owner_executor[0] != '\0';
}

struct DeviceModuleSpec {
  const char *id = nullptr;
  const char *kind = nullptr;
  const char *capability_id = nullptr;
  const DeviceOperationSpec *operations = nullptr;
  size_t operation_count = 0;
};

// Device-generated identity for one ingress call. It is never an authority token.
struct DeviceOperationCorrelation {
  uint32_t request_sequence = 0;
  uint64_t trace_id = 0;
  uint32_t trace_generation = 0;
};

inline constexpr size_t kDeviceModuleCapacity = 48;

using DeviceModuleSpecVisitor =
    void (*)(const DeviceModuleSpec &module, void *context);

// Composition transaction: reserve the declaration, commit dependent model
// or projection data, then publish the declaration/activation. A failed
// callback leaves the capability unchanged.

// The model index is immutable after sealing and safe to consume from UI,
// serial, local HTTP or future USB projections. It is owned by a runtime
// context; no process-wide model index exists.
// Operation copies are allocated per trusted module at registration time;
// later registrations preserve previously borrowed operation pointers.
class DeviceModelRegistry {
public:
  DeviceModelRegistry();
  ~DeviceModelRegistry();
  DeviceModelRegistry(const DeviceModelRegistry &) = delete;
  DeviceModelRegistry &operator=(const DeviceModelRegistry &) = delete;

  void reset();
  void visit(DeviceModuleSpecVisitor visitor, void *context) const;
  const DeviceModuleSpec *find_module(const char *module_id) const;
  const DeviceOperationSpec *find_operation(const char *module_id,
                                             const char *operation_id) const;
  bool register_module(const DeviceModuleSpec &module,
                       std::string *error = nullptr);
  bool seal(std::string *error = nullptr);
  bool sealed() const;

private:
  int find_module_locked(const char *module_id) const;
  struct Impl;
  Impl *impl_ = nullptr;
};

// Availability is a model fact, independent of the transport used to observe
// it. Static capabilities are installed before sealing; runtime readiness may
// activate or deactivate an already declared capability afterwards. Like the
// model index, capability state belongs to a runtime context.
class DeviceCapabilityRegistry {
public:
  DeviceCapabilityRegistry();
  ~DeviceCapabilityRegistry();
  DeviceCapabilityRegistry(const DeviceCapabilityRegistry &) = delete;
  DeviceCapabilityRegistry &operator=(const DeviceCapabilityRegistry &) = delete;

  void reset();
  bool declare_capability(const DeviceCapabilityDescriptor &descriptor,
                          std::string *error = nullptr);
  bool seal(std::string *error = nullptr);
  bool sealed() const;
  bool declared(const char *id) const;
  bool activate(const DeviceCapabilityDescriptor &descriptor,
                std::string *error = nullptr);
  bool deactivate(const char *id);
  bool active(const char *id) const;
  uint32_t revision() const;
  void visit(DeviceCapabilityVisitor visitor, void *context) const;

  // The synchronous commit must be bounded, must not reenter this registry,
  // and must leave its own state unchanged on failure. Core validates capacity
  // and identity first, then publishes declaration/activation only on success.
  using DeviceCapabilityCommit = bool (*)(void *, std::string *);
  bool commit_contribution(const DeviceCapabilityDescriptor &descriptor,
                             bool active, DeviceCapabilityCommit commit,
                             void *context, std::string *error);
private:
  int find_locked(const char *id) const;
  bool can_declare_locked(const DeviceCapabilityDescriptor &descriptor,
                          std::string *error) const;
  bool declare_locked(const DeviceCapabilityDescriptor &descriptor,
                      std::string *error);
  struct Slot {
    const DeviceCapabilityDescriptor *descriptor = nullptr;
    bool active = false;
  };
  struct Impl;
  Impl *impl_ = nullptr;
};

// Runtime-scoped model and capability registries are grouped so product
// composition can pass one explicit owner into the transport projection
// registry without exposing process-wide state.
struct DeviceRuntimeContext {
  DeviceModelRegistry models;
  DeviceCapabilityRegistry capabilities;
  DeviceActivityRegistry activities;
  OutputControl control;

  uint32_t allocate_request_sequence() {
    uint32_t previous = request_sequence_.load(std::memory_order_relaxed);
    while (previous != UINT32_MAX) {
      if (request_sequence_.compare_exchange_weak(previous, previous + 1,
              std::memory_order_relaxed)) return previous + 1;
    }
    return 0; // Never reuse a request identity within this device context.
  }

  bool reset(std::string &error) {
    if (!control.reset(error)) return false;
    models.reset();
    capabilities.reset();
    activities.reset();
    error.clear(); return true;
  }
private:
  std::atomic<uint32_t> request_sequence_{0};
};

} // namespace lunaredu::lunaros
