#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <string_view>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "diagnostics/elapsed_work_report.h"
#endif

#include "app/lifecycle/application_snapshot.h"
#include "app/lifecycle/app_capability.h"
#include "app/lifecycle/application_launch.h"
#include "app/execution/application_context.h"

#ifndef CONFIG_LUNAREDU_LUNAROS_RESOURCE_LEASE_CAPACITY
#define CONFIG_LUNAREDU_LUNAROS_RESOURCE_LEASE_CAPACITY 32
#endif

namespace lunaredu::app {

static_assert(CONFIG_LUNAREDU_LUNAROS_RESOURCE_LEASE_CAPACITY >=
                  2 * kApplicationResourceCapacity,
              "resource lease capacity must cover active and pending "
              "foreground requirements");

class AppController {
public:
  explicit AppController(lunaros::ApplicationContext &device);
  ~AppController();
  AppController(const AppController &) = delete;
  AppController &operator=(const AppController &) = delete;

  bool seal(std::string &error) { return capabilities_.seal_capabilities(error); }
  CapabilityRegistry &capabilities() { return capabilities_; }
  lunaros::KernelRuntime &runtime() { return device_.runtime; }
  const lunaredu::lunaros::KernelRuntime &runtime() const { return device_.runtime; }
  bool register_foreground(const ForegroundDefinition &foreground,
                           std::string &error);
  // Borrowed App owner, valid while its composition and registration live.
  void *foreground_context(const char *id) const;
  bool setup(const std::string &initial_application_id, std::string &error);
  void tick();
  bool enqueue_launch(const char *source, const char *id, ApplicationLaunch &, std::string &);
  bool launch_snapshot(const char *source, ApplicationLaunch, ApplicationLaunchSnapshot &, std::string &) const;
  bool cancel_launch(const char *source, ApplicationLaunch, std::string &);
  // Borrow only for this call. Registered Apps own all retained transition
  // metadata; queued commands retain their own request text before dispatch.
  bool switch_to(std::string_view application_id, std::string &error);
  bool foreground_current(const char *id) const;
  bool switch_to(std::string_view application_id,
                 ApplicationEnterReason reason, std::string &error);
  bool stop(const char *reason);
  bool setup_started() const { return setup_started_; }


  // Runtime owners should reuse caller-provided storage. AppControllerSnapshot
  // intentionally contains the complete fixed-capacity application catalog;
  // returning it by value inside a deeply nested UI tick can consume several
  // kilobytes of task stack even when copy elision is available.
  void status_snapshot_into(AppControllerStatusSnapshot &result) const;
  bool application_snapshot_into(size_t index,
                                 ApplicationSnapshot &result) const;
  void snapshot_into(AppControllerSnapshot &result) const;
  uint32_t transition_id() const { return transition_id_; }
  // Explicit service/resource-owner facade. Callers cannot mutate or inspect
  // the registry implementation directly.
  bool acquire_resources(const std::string &owner_id,
                         const lunaros::ResourceRequirement *requirements,
                         size_t requirement_count, std::string &error);
  void release_resources(const std::string &owner_id);
  bool resource_owner_holds(const std::string &owner_id,
                            const std::string &resource_id) const;

private:
  friend class AppControllerView;
  struct LaunchRecord {
    uint64_t generation=0;
    std::string source;
    ApplicationLaunchSnapshot snapshot;
    uint32_t expected_transition=0;
    bool cancelling=false;
  };
  std::array<LaunchRecord,4> launches_{};
  uint64_t next_launch_generation_=0;
  void poll_launches();
public:
  bool reset_composition(std::string &error);
private:
  void reset_declarations();
  enum class ResourceLeaseState {
    Reserved,
    Active,
  };

  class ResourceRegistry {
  public:
    bool can_reserve(const std::string &owner_id,
                     const lunaros::ResourceRequirement *requirements,
                     size_t requirement_count,
                     const char *replacing_owner_id,
                     std::string &error) const;
    bool reserve(const std::string &owner_id,
                 const lunaros::ResourceRequirement *requirements,
                 size_t requirement_count,
                 const char *replacing_owner_id,
                 std::string &error);
    bool commit(const std::string &owner_id, const char *replacing_owner_id,
                std::string &error);
    bool acquire(const std::string &owner_id,
                 const lunaros::ResourceRequirement *requirements,
                 size_t requirement_count, std::string &error);
    void cancel(const std::string &owner_id);
    void release(const std::string &owner_id);
    bool owner_holds(const std::string &owner_id,
                     const std::string &resource_id) const;

  private:
    struct Lease {
      std::string resource_id;
      std::string owner_id;
      lunaros::ResourceAccess access = lunaros::ResourceAccess::Exclusive;
      ResourceLeaseState state = ResourceLeaseState::Reserved;
    };

    void remove_owner(const std::string &owner_id, bool remove_active,
                      bool remove_reserved);

    std::array<Lease, CONFIG_LUNAREDU_LUNAROS_RESOURCE_LEASE_CAPACITY> leases_{};
    size_t lease_count_ = 0;
  };

  struct RegisteredApplication {
    std::string id;
    std::string title;
    std::string icon;
    uint32_t launcher_accent_rgb = 0x38BDF8;
    std::string version;
    std::string settings_route;
    ForegroundKind kind = ForegroundKind::Application;
    ApplicationBackend backend = ApplicationBackend::Native;
    ApplicationSystemUiPolicy system_ui = ApplicationSystemUiPolicy::Standard;
    bool is_home = false;
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
      bool use = false;
      lunaros::ServiceUse token;
    };
    std::array<Service, kApplicationServiceCapacity> services;
    size_t resource_count = 0;
    struct Resource {
      std::string id;
      lunaros::ResourceAccess access = lunaros::ResourceAccess::Exclusive;
    };
    std::array<Resource, kApplicationResourceCapacity> resources;
    struct ExecutionResource { std::string id; lunaros::ServiceResourceWindow window; };
    std::array<ExecutionResource, kApplicationResourceCapacity> execution_resources;
    size_t execution_resource_count = 0;
    std::string resource_owner_id;
    void *context = nullptr;
    ApplicationPrepareCallback prepare = nullptr;
    ApplicationStartCallback start = nullptr;
    ApplicationTickCallback tick = nullptr;
    ApplicationTickCallback capability_tick = nullptr;
    ApplicationStopCallback stop = nullptr;
    ApplicationStopCheckedCallback stop_checked = nullptr;
    const ApplicationPreparation *preparation = nullptr;
    uint32_t startup_timeout_ms=30000;
    lunaros::ExecutionContract execution;
    lunaros::ExecutionLease execution_lease, capability_execution_lease;
    // Availability is a derived view. Cache it until one of its inputs
    // changes instead of rescanning all capability, service, and resource
    // requirements for every launcher/status snapshot.
    mutable bool availability_cache_valid = false;
    mutable uint32_t availability_capability_revision = 0;
    mutable uint32_t availability_service_revision = 0;
    mutable uint32_t availability_resource_revision = 0;
    mutable bool availability_cached_value = false;
    mutable std::string availability_cached_reason;
  };

  RegisteredApplication *find_application(std::string_view application_id);
  const RegisteredApplication *
  find_application(std::string_view application_id) const;
  bool application_available(const RegisteredApplication &application,
                             std::string &reason) const;
  void copy_resource_requirements(
      const RegisteredApplication &application,
      std::array<lunaros::ResourceRequirement,
                 kApplicationResourceCapacity> &requirements) const;
  bool stop_application(RegisteredApplication &application,
                        const char *reason);
  bool publish_application_uses(RegisteredApplication &,std::string &);
  bool acquire_application_execution(RegisteredApplication &,std::string &);
  bool before_service_restart(const char *service_id, std::string &error);
  bool after_service_restart(const char *service_id, bool restarted,
                             std::string &error);
  void fail_target_and_restore(const std::string &error);
  void finish_failed_restore(const std::string &error);
  void advance_transition();
  void finish_transition(AppControllerState state, bool clear_error=false);

  std::array<RegisteredApplication, kApplicationCapacity> applications_;
  size_t application_count_ = 0;
  // Only declared capability owners participate in this high-frequency pass.
  // Definitions have stable addresses until the boot-only reset boundary.
  std::array<RegisteredApplication *, kApplicationCapacity> capability_ticks_{};
  size_t capability_tick_count_ = 0;
  RegisteredApplication *active_application_ = nullptr;
  AppControllerState state_ = AppControllerState::Stopped;
  std::string last_error_;
  uint32_t transition_id_ = 0;
  bool setup_started_ = false;
  ApplicationEnterReason active_enter_reason_ = ApplicationEnterReason::User;
  struct Transition {
    RegisteredApplication *target=nullptr, *previous=nullptr;
    AppTransitionPhase phase=AppTransitionPhase::Idle;
    ApplicationEnterReason target_reason=ApplicationEnterReason::User, previous_reason=ApplicationEnterReason::User;
    int64_t started_us=0;
    std::string failure;
  } transition_;
  struct MaintenanceRestore {
    std::string app_id, service_id;
    void clear() { app_id.clear(); service_id.clear(); }
  } maintenance_restore_;
  ResourceRegistry resource_registry_;
  uint32_t resource_revision_ = 0;
  CapabilityRegistry capabilities_;
#ifdef ESP_PLATFORM
  system::ElapsedWorkTiming<5> tick_work_;
  system::ElapsedWorkReporter<5> tick_reporter_;
#endif
  lunaros::ApplicationContext &device_;
};

// Owns an AppController in external RAM on ESP targets. Products still choose
// which foreground applications to register and when to start them.
class AppControllerStorage {
public:
  AppControllerStorage() = default;
  ~AppControllerStorage();
  AppControllerStorage(const AppControllerStorage &) = delete;
  AppControllerStorage &operator=(const AppControllerStorage &) = delete;

  bool ensure(lunaros::ApplicationContext &device, std::string &error);
  void reset();
  AppController *get() const { return controller_; }
  AppController *operator->() const { return controller_; }

private:
  AppController *controller_ = nullptr;
};

} // namespace lunaredu::app
