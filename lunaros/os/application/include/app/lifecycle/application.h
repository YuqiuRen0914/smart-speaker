#pragma once
#include "kernel/execution_admission.h"
#include "app/lifecycle/application_preparation.h"

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "kernel/service.h"

namespace lunaredu::lunaros {

enum class ResourceAccess {
  Shared,
  Exclusive,
};

struct ResourceRequirement {
  const char *id = nullptr;
  ResourceAccess access = ResourceAccess::Exclusive;
};

} // namespace lunaredu::lunaros

namespace lunaredu::app {
class CapabilityRegistry;

// Every mutually-exclusive foreground owner is registered in the same
// controller, but only Application entries belong in a product launcher.
// Keeping this distinction in the domain model avoids encoding it indirectly
// through visibility or product-specific id conventions.
enum class ForegroundKind {
  Application,
  SystemSurface,
  Mode,
};

enum class ApplicationBackend {
  Native,
  MicroPython,
};

enum class ApplicationStartResult {
  Started,
  Pending,
  Failed,
};

// Why a user-facing application is being entered. LunarOS only communicates
// the entry reason; each application owns the behavior associated with it.
enum class ApplicationEnterReason {
  Boot,
  User,
  Service,
};

enum class ApplicationSystemUiPolicy {
  Standard,
  Immersive,
};

struct ApplicationCapabilityRequirement {
  const char *id = nullptr;
  bool required = true;
};

struct ApplicationServiceRequirement {
  const char *id = nullptr;
  // Required blocks entry and ends an active app if the service disappears.
  // Optional is reported in snapshots so the app can choose a fallback.
  bool required = true;
  // Hold an OS use for this foreground lifetime. Admission remains with the
  // service; an optional use permits an offline/disabled presentation fallback.
  bool use = false;
};

constexpr ApplicationServiceRequirement required_service(const char *id) {
  return {id, true};
}

constexpr ApplicationServiceRequirement optional_service(const char *id) {
  return {id, false};
}
constexpr ApplicationServiceRequirement used_service(const char *id) {return {id,false,true};}

struct ForegroundManifest {
  ForegroundKind kind = ForegroundKind::Application;
  const char *title = nullptr;
  const char *icon = nullptr;
  uint32_t launcher_accent_rgb = 0x38BDF8;
  const char *version = "1";
  // Optional App-owned settings destination. System surfaces and modes do not
  // contribute to the application-settings catalog.
  const char *settings_route = nullptr;
  ApplicationSystemUiPolicy system_ui = ApplicationSystemUiPolicy::Standard;
  // Optional unique navigation destination in this controller. Products without
  // a home remain valid; clients must not infer it from an id or foreground kind.
  bool is_home = false;
  const ApplicationCapabilityRequirement *capabilities = nullptr;
  size_t capability_count = 0;
  const ApplicationServiceRequirement *services = nullptr;
  size_t service_count = 0;
  const lunaros::ResourceRequirement *resources = nullptr;
  size_t resource_count = 0;
  // Exclusive handoff from background execution owners. App startup waits
  // until affected services have actually retired, including dependents.
  const char *const *execution_resources = nullptr;
  size_t execution_resource_count = 0;
};

// A Pending callback is polled again by AppController::tick() and must be
// idempotent. stop() may be called after any start attempt for cleanup.
// One entry contract for Apps, surfaces and modes. Pending retries and rollback
// retain the original entry reason.
using ApplicationStartCallback =
    ApplicationStartResult (*)(void *context, ApplicationEnterReason reason,
                               std::string &error);
// prepare() is a side-effect-free preflight. It runs after logical resources
// are reserved but before the previous foreground application is stopped.
// A rejected prepare leaves that application intact.
using ApplicationPrepareCallback =
    bool (*)(void *context, std::string &error);
using ApplicationTickCallback = void (*)(void *context);
using ApplicationStopCallback = void (*)(void *context, const char *reason);
// Failure keeps the previous foreground and its resources intact.
using ApplicationStopCheckedCallback = bool (*)(void *context, const char *reason, std::string &error);

// Definitions are copied into AppController. The id string only needs to be
// valid while register_foreground() is executing; context remains owned by
// the product and must outlive the controller registration.
struct ForegroundDefinition {
  const char *id = nullptr;
  ApplicationBackend backend = ApplicationBackend::Native;
  ForegroundManifest manifest;
  void *context = nullptr;
  ApplicationPrepareCallback prepare = nullptr;
  ApplicationStartCallback start = nullptr;
  ApplicationTickCallback tick = nullptr;
  // Bounded maintenance for declared capabilities, independent of foreground.
  // May observe/enqueue work; blocking work belongs on a service executor.
  ApplicationTickCallback capability_tick = nullptr;
  ApplicationStopCallback stop = nullptr;
  ApplicationStopCheckedCallback stop_checked = nullptr;
  // Declared at registration, independently of foreground activation.
  // Boot composition is serialized. Only register providers/consumers here;
  // do not reset/seal the catalog or change its access policy. AppController
  // validates the manifest first and rolls back this callback's additions on
  // failure, preserving declarations made by other applications.
  bool (*declare_capabilities)(CapabilityRegistry &, std::string &error) = nullptr;
  // Borrowed for the registration lifetime. Read-only diagnostics do not run
  // preparation; an explicit entry can retry a failed preparation.
  const ApplicationPreparation *preparation = nullptr;
  // Covers resource handoff and asynchronous entry. Cleanup still retains
  // ownership after this deadline until stop_checked confirms retirement.
  uint32_t startup_timeout_ms = 30000;
  lunaredu::lunaros::ExecutionContract execution;
  lunaredu::lunaros::ExecutionContract capability_execution;
};


} // namespace lunaredu::app
