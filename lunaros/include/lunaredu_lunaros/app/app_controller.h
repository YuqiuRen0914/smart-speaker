#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>
#include <string>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#include "lunaredu_lunaros/app/application.h"

#ifndef CONFIG_LUNAREDU_LUNAROS_FOREGROUND_CAPACITY
#define CONFIG_LUNAREDU_LUNAROS_FOREGROUND_CAPACITY 16
#endif

#ifndef CONFIG_LUNAREDU_LUNAROS_APPLICATION_CAPABILITY_CAPACITY
#define CONFIG_LUNAREDU_LUNAROS_APPLICATION_CAPABILITY_CAPACITY 8
#endif

#ifndef CONFIG_LUNAREDU_LUNAROS_APPLICATION_SERVICE_CAPACITY
#define CONFIG_LUNAREDU_LUNAROS_APPLICATION_SERVICE_CAPACITY 4
#endif

#ifndef CONFIG_LUNAREDU_LUNAROS_APPLICATION_RESOURCE_CAPACITY
#define CONFIG_LUNAREDU_LUNAROS_APPLICATION_RESOURCE_CAPACITY 4
#endif

namespace lunaredu::app {

// A bounded registry is intentional on ESP32, but the bound is runtime
// headroom rather than a product application-count contract. Products may use
// the same controller for launcher applications, system surfaces, and modes.
constexpr size_t kForegroundCapacity =
    CONFIG_LUNAREDU_LUNAROS_FOREGROUND_CAPACITY;
constexpr size_t kApplicationCapacity = kForegroundCapacity;
constexpr size_t kApplicationCapabilityCapacity =
    CONFIG_LUNAREDU_LUNAROS_APPLICATION_CAPABILITY_CAPACITY;
constexpr size_t kApplicationServiceCapacity =
    CONFIG_LUNAREDU_LUNAROS_APPLICATION_SERVICE_CAPACITY;
constexpr size_t kApplicationResourceCapacity =
    CONFIG_LUNAREDU_LUNAROS_APPLICATION_RESOURCE_CAPACITY;

static_assert(lunaros::kResourceLeaseCapacity >=
                  2 * kApplicationResourceCapacity,
              "resource lease capacity must cover active and pending "
              "foreground requirements");

enum class AppControllerState {
  Stopped,
  Switching,
  Running,
  Error,
};

enum class AppTransitionPhase {
  Idle,
  StartingTarget,
  RestoringPrevious,
};

const char *app_transition_phase_name(AppTransitionPhase phase);

struct ApplicationSnapshot {
  std::string id;
  std::string title;
  std::string icon;
  uint32_t launcher_accent_rgb = 0x38BDF8;
  std::string version;
  ForegroundKind kind = ForegroundKind::Application;
  ApplicationBackend backend = ApplicationBackend::Native;
  ApplicationVisibility visibility = ApplicationVisibility::Launcher;
  ApplicationSystemUiPolicy system_ui = ApplicationSystemUiPolicy::Standard;
  bool available = true;
  std::string unavailable_reason;
  size_t capability_count = 0;
  struct Capability {
    std::string id;
    bool required = true;
    bool available = false;
  };
  std::array<Capability, kApplicationCapabilityCapacity> capabilities;
  size_t service_count = 0;
  struct Service {
    // Application registrations are immutable after setup(), so snapshots can
    // reference the stable id instead of copying dozens of strings onto the
    // LunarOS owner task stack.
    const char *id = nullptr;
    bool required = true;
    bool available = false;
  };
  std::array<Service, kApplicationServiceCapacity> services;
  size_t resource_count = 0;
  struct Resource {
    std::string id;
    lunaros::ResourceAccess access = lunaros::ResourceAccess::Exclusive;
  };
  std::array<Resource, kApplicationResourceCapacity> resources;
  bool active = false;
  bool pending = false;
};

// Small, cross-executor control-plane view. Keep this type independent from
// the fixed-capacity application catalog so HTTP/network tasks never need to
// place the complete catalog on their stack just to read transition state.
struct AppControllerStatusSnapshot {
  AppControllerState state = AppControllerState::Stopped;
  std::string active_application_id;
  std::string pending_application_id;
  std::string previous_application_id;
  std::string last_error;
  uint32_t transition_id = 0;
  AppTransitionPhase transition_phase = AppTransitionPhase::Idle;
  size_t application_count = 0;
};

// Full catalog snapshots are intended for long-lived owner storage (for
// example the desktop shell's PSRAM cache) and host-side tests. Device contracts use
// AppControllerStatusSnapshot plus one ApplicationSnapshot at a time.
struct AppControllerSnapshot : AppControllerStatusSnapshot {
  std::array<ApplicationSnapshot, kApplicationCapacity> applications;
};

const char *app_controller_state_name(AppControllerState state);

class AppController {
public:
  bool register_foreground(const ForegroundDefinition &foreground,
                           std::string &error);
  bool register_application(const ApplicationDefinition &application,
                            std::string &error) {
    return register_foreground(application, error);
  }
  bool setup(const std::string &initial_application_id, std::string &error);
  void tick();
  bool switch_to(const std::string &application_id, std::string &error);
  bool switch_to(const std::string &application_id,
                 ApplicationEnterReason reason, std::string &error);
  void stop(const char *reason);

  // Runtime owners should reuse caller-provided storage. AppControllerSnapshot
  // intentionally contains the complete fixed-capacity application catalog;
  // returning it by value inside a deeply nested UI tick can consume several
  // kilobytes of task stack even when copy elision is available.
  void status_snapshot_into(AppControllerStatusSnapshot &result) const;
  bool application_snapshot_into(size_t index,
                                 ApplicationSnapshot &result) const;
  void snapshot_into(AppControllerSnapshot &result) const;
  AppControllerSnapshot snapshot() const;
  const char *active_application_id() const {
    return active_application_ != nullptr ? active_application_->id.c_str()
                                          : nullptr;
  }
  uint32_t transition_id() const { return transition_id_; }
  lunaros::ResourceRegistry &resource_registry() { return resource_registry_; }
  const lunaros::ResourceRegistry &resource_registry() const {
    return resource_registry_;
  }

private:
  struct RegisteredApplication {
    std::string id;
    std::string title;
    std::string icon;
    uint32_t launcher_accent_rgb = 0x38BDF8;
    std::string version;
    ForegroundKind kind = ForegroundKind::Application;
    ApplicationBackend backend = ApplicationBackend::Native;
    ApplicationVisibility visibility = ApplicationVisibility::Launcher;
    ApplicationSystemUiPolicy system_ui = ApplicationSystemUiPolicy::Standard;
    size_t capability_count = 0;
    struct Capability {
      std::string id;
      bool required = true;
    };
    std::array<Capability, kApplicationCapabilityCapacity> capabilities;
    size_t service_count = 0;
    struct Service {
      std::string id;
      bool required = true;
    };
    std::array<Service, kApplicationServiceCapacity> services;
    size_t resource_count = 0;
    struct Resource {
      std::string id;
      lunaros::ResourceAccess access = lunaros::ResourceAccess::Exclusive;
    };
    std::array<Resource, kApplicationResourceCapacity> resources;
    std::string resource_owner_id;
    void *context = nullptr;
    ApplicationPrepareCallback prepare = nullptr;
    ApplicationStartCallback start = nullptr;
    ApplicationStartWithReasonCallback start_with_reason = nullptr;
    ApplicationTickCallback tick = nullptr;
    ApplicationStopCallback stop = nullptr;
  };

  RegisteredApplication *find_application(const std::string &application_id);
  const RegisteredApplication *
  find_application(const std::string &application_id) const;
  bool application_available(const RegisteredApplication &application,
                             std::string &reason) const;
  void copy_resource_requirements(
      const RegisteredApplication &application,
      std::array<lunaros::ResourceRequirement,
                 kApplicationResourceCapacity> &requirements) const;
  void stop_application(RegisteredApplication &application,
                        const char *reason);
  void fail_target_and_restore(const std::string &error);
  void finish_failed_restore(const std::string &error);
  void advance_transition();

  std::array<RegisteredApplication, kApplicationCapacity> applications_;
  size_t application_count_ = 0;
  RegisteredApplication *active_application_ = nullptr;
  RegisteredApplication *pending_application_ = nullptr;
  RegisteredApplication *previous_application_ = nullptr;
  AppControllerState state_ = AppControllerState::Stopped;
  AppTransitionPhase transition_phase_ = AppTransitionPhase::Idle;
  std::string last_error_;
  uint32_t transition_id_ = 0;
  bool setup_started_ = false;
  ApplicationEnterReason active_enter_reason_ = ApplicationEnterReason::User;
  ApplicationEnterReason pending_enter_reason_ = ApplicationEnterReason::User;
  ApplicationEnterReason previous_enter_reason_ = ApplicationEnterReason::User;
  lunaros::ResourceRegistry resource_registry_;
};

} // namespace lunaredu::app
