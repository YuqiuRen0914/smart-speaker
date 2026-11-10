#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "lunaredu_lunaros/app/resource_registry.h"
#include "lunaredu_lunaros/kernel/service_registry.h"

namespace lunaredu::app {

// Every mutually-exclusive foreground owner is registered in the same
// controller, but only Application entries belong in a product launcher.
// Keeping this distinction in the domain model avoids encoding it indirectly
// through visibility or product-specific id conventions.
enum class ForegroundKind {
  Application,
  SystemSurface,
  Mode,
};

constexpr const char *foreground_kind_name(ForegroundKind kind) {
  switch (kind) {
  case ForegroundKind::Application:
    return "application";
  case ForegroundKind::SystemSurface:
    return "system_surface";
  case ForegroundKind::Mode:
    return "mode";
  }
  return "application";
}

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
};

constexpr const char *application_enter_reason_name(
    ApplicationEnterReason reason) {
  switch (reason) {
  case ApplicationEnterReason::Boot:
    return "boot";
  case ApplicationEnterReason::User:
    return "user";
  }
  return "user";
}

enum class ApplicationVisibility {
  Launcher,
  Hidden,
  System,
};

enum class ApplicationSystemUiPolicy {
  Standard,
  Immersive,
};

constexpr const char *application_visibility_name(
    ApplicationVisibility visibility) {
  switch (visibility) {
  case ApplicationVisibility::Launcher:
    return "launcher";
  case ApplicationVisibility::Hidden:
    return "hidden";
  case ApplicationVisibility::System:
    return "system";
  }
  return "hidden";
}

constexpr const char *application_system_ui_policy_name(
    ApplicationSystemUiPolicy policy) {
  switch (policy) {
  case ApplicationSystemUiPolicy::Standard:
    return "standard";
  case ApplicationSystemUiPolicy::Immersive:
    return "immersive";
  }
  return "standard";
}

struct ApplicationCapabilityRequirement {
  const char *id = nullptr;
  bool required = true;
};

struct ApplicationServiceRequirement {
  const char *id = nullptr;
  bool required = true;
};

struct ForegroundManifest {
  ForegroundKind kind = ForegroundKind::Application;
  const char *title = nullptr;
  const char *icon = nullptr;
  uint32_t launcher_accent_rgb = 0x38BDF8;
  const char *version = "1";
  ApplicationVisibility visibility = ApplicationVisibility::Launcher;
  ApplicationSystemUiPolicy system_ui = ApplicationSystemUiPolicy::Standard;
  const ApplicationCapabilityRequirement *capabilities = nullptr;
  size_t capability_count = 0;
  const ApplicationServiceRequirement *services = nullptr;
  size_t service_count = 0;
  const lunaros::ResourceRequirement *resources = nullptr;
  size_t resource_count = 0;
};

// Compatibility name for existing products while callers migrate to the
// foreground terminology.
using ApplicationManifest = ForegroundManifest;

constexpr const char *application_backend_name(ApplicationBackend backend) {
  switch (backend) {
  case ApplicationBackend::Native:
    return "native";
  case ApplicationBackend::MicroPython:
    return "micropython";
  }
  return "unknown";
}

// A Pending callback is polled again by AppController::tick() and must be
// idempotent. stop() may be called after any start attempt for cleanup.
using ApplicationStartCallback =
    ApplicationStartResult (*)(void *context, std::string &error);
// Incremental replacement for ApplicationStartCallback. Products can migrate
// one application at a time without forcing system surfaces and hidden modes
// to interpret application entry semantics.
using ApplicationStartWithReasonCallback =
    ApplicationStartResult (*)(void *context, ApplicationEnterReason reason,
                               std::string &error);
// prepare() is a side-effect-free preflight. It runs after logical resources
// are reserved but before the previous foreground application is stopped.
// A rejected prepare leaves that application intact.
using ApplicationPrepareCallback =
    bool (*)(void *context, std::string &error);
using ApplicationTickCallback = void (*)(void *context);
using ApplicationStopCallback = void (*)(void *context, const char *reason);

// Definitions are copied into AppController. The id string only needs to be
// valid while register_application() is executing; context remains owned by
// the product and must outlive the controller registration.
struct ForegroundDefinition {
  const char *id = nullptr;
  ApplicationBackend backend = ApplicationBackend::Native;
  ForegroundManifest manifest;
  void *context = nullptr;
  ApplicationPrepareCallback prepare = nullptr;
  ApplicationStartCallback start = nullptr;
  ApplicationStartWithReasonCallback start_with_reason = nullptr;
  ApplicationTickCallback tick = nullptr;
  ApplicationStopCallback stop = nullptr;
  // Declared at registration, independently of foreground activation.
  // Boot composition is serialized. Only register providers/consumers here;
  // do not reset/seal the catalog or change its access policy. AppController
  // validates the manifest first and rolls back this callback's additions on
  // failure, preserving declarations made by other applications.
  bool (*declare_capabilities)(std::string &error) = nullptr;
};

using ApplicationDefinition = ForegroundDefinition;

} // namespace lunaredu::app
