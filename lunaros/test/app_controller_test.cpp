#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/app/app_controller.h"
#include "lunaredu_lunaros/app/app_capability.h"
#include "kernel/services/service_registry_internal.h"

namespace {

int g_failures = 0;
std::vector<std::string> g_capabilities;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __func__ << ":" << __LINE__                              \
                << " check failed: " #condition << std::endl;                \
      ++g_failures;                                                            \
    }                                                                          \
  } while (false)

struct AppProbe {
  AppProbe(std::string app_id, std::vector<std::string> *event_log,
           std::vector<lunaredu::app::ApplicationStartResult> start_results = {})
      : id(std::move(app_id)), events(event_log),
        starts(std::move(start_results)) {}

  std::string id;
  std::vector<std::string> *events = nullptr;
  std::vector<lunaredu::app::ApplicationStartResult> starts;
  size_t start_index = 0;
  int stop_count = 0;
  std::vector<lunaredu::app::ApplicationEnterReason> enter_reasons;
};

lunaredu::app::ApplicationStartResult start_probe(void *context,
                                                   std::string &error) {
  auto &probe = *static_cast<AppProbe *>(context);
  probe.events->push_back("start:" + probe.id);
  const auto result =
      probe.start_index < probe.starts.size()
          ? probe.starts[probe.start_index++]
          : lunaredu::app::ApplicationStartResult::Started;
  if (result == lunaredu::app::ApplicationStartResult::Failed) {
    error = probe.id + "_failed";
  } else {
    error.clear();
  }
  return result;
}

lunaredu::app::ApplicationStartResult
start_probe_with_reason(void *context,
                        lunaredu::app::ApplicationEnterReason reason,
                        std::string &error) {
  auto &probe = *static_cast<AppProbe *>(context);
  probe.enter_reasons.push_back(reason);
  return start_probe(context, error);
}

void stop_probe(void *context, const char *reason) {
  auto &probe = *static_cast<AppProbe *>(context);
  ++probe.stop_count;
  probe.events->push_back("stop:" + probe.id + ":" +
                          (reason != nullptr ? reason : ""));
}

lunaredu::app::ApplicationDefinition
definition(AppProbe &probe,
           const lunaredu::lunaros::ResourceRequirement *resources = nullptr,
           size_t resource_count = 0,
           const lunaredu::app::ApplicationCapabilityRequirement *capabilities =
               nullptr,
           size_t capability_count = 0,
           const lunaredu::app::ApplicationServiceRequirement *services =
               nullptr,
           size_t service_count = 0) {
  lunaredu::app::ApplicationDefinition result;
  result.id = probe.id.c_str();
  result.context = &probe;
  result.start = start_probe;
  result.stop = stop_probe;
  result.manifest.resources = resources;
  result.manifest.resource_count = resource_count;
  result.manifest.capabilities = capabilities;
  result.manifest.capability_count = capability_count;
  result.manifest.services = services;
  result.manifest.service_count = service_count;
  return result;
}

void register_or_fail(lunaredu::app::AppController &controller,
                      const lunaredu::app::ApplicationDefinition &application) {
  std::string error;
  CHECK(controller.register_application(application, error));
  CHECK(error.empty());
}

void test_successful_switch_transfers_resource() {
  // The complete 16-entry catalog is owner storage, not a cross-executor stack
  // value. Keep its PSRAM footprint bounded while status and single-entry
  // snapshots retain their tighter stack budgets below.
  CHECK(sizeof(lunaredu::app::AppControllerSnapshot) <= 14 * 1024);
  CHECK(sizeof(lunaredu::app::AppControllerStatusSnapshot) <= 512);
  CHECK(sizeof(lunaredu::app::ApplicationSnapshot) <= 2 * 1024);
  std::vector<std::string> events;
  AppProbe first{"first", &events, {}};
  AppProbe second{"second", &events, {}};
  constexpr lunaredu::lunaros::ResourceRequirement screen[] = {
      {"presentation.foreground",
       lunaredu::lunaros::ResourceAccess::Exclusive},
  };

  lunaredu::app::AppController controller;
  register_or_fail(controller, definition(first, screen, 1));
  register_or_fail(controller, definition(second, screen, 1));
  std::string error;
  CHECK(controller.setup("first", error));
  CHECK(controller.switch_to("second", error));

  lunaredu::app::AppControllerStatusSnapshot status;
  controller.status_snapshot_into(status);
  CHECK(status.active_application_id == "second");
  CHECK(status.application_count == 2);
  lunaredu::app::ApplicationSnapshot application;
  CHECK(controller.application_snapshot_into(1, application));
  CHECK(application.id == "second");
  CHECK(application.active);
  CHECK(!controller.application_snapshot_into(2, application));

  const auto snapshot = controller.snapshot();
  CHECK(snapshot.state == lunaredu::app::AppControllerState::Running);
  CHECK(snapshot.active_application_id == "second");
  CHECK(first.stop_count == 1);
  CHECK(controller.resource_registry().owner_holds(
      "app:second", "presentation.foreground"));
  CHECK(!controller.resource_registry().owner_holds(
      "app:first", "presentation.foreground"));
  CHECK(events.size() == 3);
  CHECK(events[0] == "start:first");
  CHECK(events[1] == "stop:first:application_switch_prepare");
  CHECK(events[2] == "start:second");
}

void test_missing_capability_preserves_foreground() {
  g_capabilities.clear();
  std::vector<std::string> events;
  AppProbe first{"first", &events, {}};
  AppProbe second{"second", &events, {}};
  constexpr lunaredu::app::ApplicationCapabilityRequirement required[] = {
      {"audio.input", true},
  };

  lunaredu::app::AppController controller;
  register_or_fail(controller, definition(first));
  register_or_fail(controller, definition(second, nullptr, 0, required, 1));
  std::string error;
  CHECK(controller.setup("first", error));
  CHECK(!controller.switch_to("second", error));
  CHECK(error == "missing_capability:audio.input");
  CHECK(controller.snapshot().active_application_id == "first");
  CHECK(first.stop_count == 0);
}

void test_external_resource_conflict_preserves_foreground() {
  std::vector<std::string> events;
  AppProbe first{"first", &events, {}};
  AppProbe second{"second", &events, {}};
  constexpr lunaredu::lunaros::ResourceRequirement camera[] = {
      {"camera.capture", lunaredu::lunaros::ResourceAccess::Exclusive},
  };

  lunaredu::app::AppController controller;
  std::string error;
  CHECK(controller.resource_registry().acquire("service:vision", camera, 1,
                                                error));
  register_or_fail(controller, definition(first));
  register_or_fail(controller, definition(second, camera, 1));
  CHECK(controller.setup("first", error));
  CHECK(!controller.switch_to("second", error));
  CHECK(error == "resource_conflict:camera.capture:service:vision");
  CHECK(controller.snapshot().active_application_id == "first");
  CHECK(first.stop_count == 0);
}

void test_missing_service_preserves_foreground() {
  lunaredu::lunaros::reset_service_registry_internal();
  std::vector<std::string> events;
  AppProbe first{"first", &events, {}};
  AppProbe second{"second", &events, {}};
  constexpr lunaredu::app::ApplicationServiceRequirement required[] = {
      {"assistant.xiaozhi", true},
  };

  lunaredu::app::AppController controller;
  register_or_fail(controller, definition(first));
  register_or_fail(controller,
                   definition(second, nullptr, 0, nullptr, 0, required, 1));
  std::string error;
  CHECK(controller.setup("first", error));
  CHECK(!controller.switch_to("second", error));
  CHECK(error == "missing_service:assistant.xiaozhi");
  CHECK(controller.snapshot().active_application_id == "first");
  CHECK(first.stop_count == 0);
}

void test_failed_start_restores_previous_application() {
  std::vector<std::string> events;
  AppProbe first{"first", &events, {}};
  AppProbe second{"second", &events,
                  {lunaredu::app::ApplicationStartResult::Failed}};
  constexpr lunaredu::lunaros::ResourceRequirement screen[] = {
      {"presentation.foreground",
       lunaredu::lunaros::ResourceAccess::Exclusive},
  };

  lunaredu::app::AppController controller;
  register_or_fail(controller, definition(first, screen, 1));
  register_or_fail(controller, definition(second, screen, 1));
  std::string error;
  CHECK(controller.setup("first", error));
  CHECK(!controller.switch_to("second", error));
  CHECK(error == "second_failed");

  const auto snapshot = controller.snapshot();
  CHECK(snapshot.state == lunaredu::app::AppControllerState::Running);
  CHECK(snapshot.active_application_id == "first");
  CHECK(snapshot.last_error == "second_failed");
  CHECK(controller.resource_registry().owner_holds(
      "app:first", "presentation.foreground"));
  CHECK(events.size() == 5);
  CHECK(events[1] == "stop:first:application_switch_prepare");
  CHECK(events[2] == "start:second");
  CHECK(events[3] == "stop:second:application_start_failed");
  CHECK(events[4] == "start:first");
}

void test_pending_failure_restores_previous_application() {
  std::vector<std::string> events;
  AppProbe first{"first", &events, {}};
  AppProbe second{
      "second", &events,
      {lunaredu::app::ApplicationStartResult::Pending,
       lunaredu::app::ApplicationStartResult::Failed}};
  AppProbe third{"third", &events, {}};

  lunaredu::app::AppController controller;
  register_or_fail(controller, definition(first));
  register_or_fail(controller, definition(second));
  register_or_fail(controller, definition(third));
  std::string error;
  CHECK(controller.setup("first", error));
  CHECK(controller.switch_to("second", error));
  CHECK(controller.snapshot().transition_phase ==
        lunaredu::app::AppTransitionPhase::StartingTarget);
  CHECK(!controller.switch_to("third", error));
  CHECK(error == "application_switch_in_progress");

  controller.tick();
  const auto snapshot = controller.snapshot();
  CHECK(snapshot.state == lunaredu::app::AppControllerState::Running);
  CHECK(snapshot.active_application_id == "first");
  CHECK(snapshot.last_error == "second_failed");
}

void test_rollback_failure_enters_error_state() {
  std::vector<std::string> events;
  AppProbe first{
      "first", &events,
      {lunaredu::app::ApplicationStartResult::Started,
       lunaredu::app::ApplicationStartResult::Failed}};
  AppProbe second{"second", &events,
                  {lunaredu::app::ApplicationStartResult::Failed}};

  lunaredu::app::AppController controller;
  register_or_fail(controller, definition(first));
  register_or_fail(controller, definition(second));
  std::string error;
  CHECK(controller.setup("first", error));
  CHECK(!controller.switch_to("second", error));
  const auto snapshot = controller.snapshot();
  CHECK(snapshot.state == lunaredu::app::AppControllerState::Error);
  CHECK(snapshot.active_application_id.empty());
  CHECK(snapshot.last_error ==
        "second_failed;rollback_failed:first_failed");
}

void test_shared_and_exclusive_resource_leases() {
  lunaredu::lunaros::ResourceRegistry resources;
  constexpr lunaredu::lunaros::ResourceRequirement shared[] = {
      {"audio.output", lunaredu::lunaros::ResourceAccess::Shared},
  };
  constexpr lunaredu::lunaros::ResourceRequirement exclusive[] = {
      {"audio.output", lunaredu::lunaros::ResourceAccess::Exclusive},
  };
  std::string error;
  CHECK(resources.acquire("service:a", shared, 1, error));
  CHECK(resources.acquire("service:b", shared, 1, error));
  CHECK(!resources.acquire("service:c", exclusive, 1, error));
  CHECK(error == "resource_conflict:audio.output:service:a");
  resources.release("service:a");
  resources.release("service:b");
  CHECK(resources.acquire("service:c", exclusive, 1, error));
}

void test_foreground_kinds_and_registry_headroom() {
  CHECK(lunaredu::app::kForegroundCapacity > 12);
  std::vector<std::string> events;
  std::vector<AppProbe> probes;
  probes.reserve(13);
  lunaredu::app::AppController controller;
  std::string error;
  for (size_t index = 0; index < 13; ++index) {
    probes.emplace_back("foreground-" + std::to_string(index), &events);
    auto foreground = definition(probes.back());
    if (index == 6) {
      foreground.manifest.kind =
          lunaredu::app::ForegroundKind::SystemSurface;
    } else if (index >= 8) {
      foreground.manifest.kind = lunaredu::app::ForegroundKind::Mode;
    }
    CHECK(controller.register_foreground(foreground, error));
    CHECK(error.empty());
  }

  lunaredu::app::ApplicationSnapshot surface;
  CHECK(controller.application_snapshot_into(6, surface));
  CHECK(surface.kind == lunaredu::app::ForegroundKind::SystemSurface);
  lunaredu::app::ApplicationSnapshot mode;
  CHECK(controller.application_snapshot_into(12, mode));
  CHECK(mode.kind == lunaredu::app::ForegroundKind::Mode);
}

void test_boot_and_user_entry_reasons_reach_application() {
  std::vector<std::string> events;
  AppProbe home{"home", &events, {}};
  AppProbe target{"target", &events, {}};

  auto home_definition = definition(home);
  auto target_definition = definition(target);
  home_definition.start = nullptr;
  home_definition.start_with_reason = start_probe_with_reason;
  target_definition.start = nullptr;
  target_definition.start_with_reason = start_probe_with_reason;

  lunaredu::app::AppController controller;
  register_or_fail(controller, home_definition);
  register_or_fail(controller, target_definition);
  std::string error;
  CHECK(controller.setup("home", error));
  CHECK(home.enter_reasons.size() == 1);
  CHECK(home.enter_reasons[0] ==
        lunaredu::app::ApplicationEnterReason::User);

  CHECK(controller.switch_to(
      "target", lunaredu::app::ApplicationEnterReason::Boot, error));
  CHECK(target.enter_reasons.size() == 1);
  CHECK(target.enter_reasons[0] ==
        lunaredu::app::ApplicationEnterReason::Boot);

  CHECK(controller.switch_to("home", error));
  CHECK(home.enter_reasons.size() == 2);
  CHECK(home.enter_reasons[1] ==
        lunaredu::app::ApplicationEnterReason::User);
}

void test_registration_is_atomic() {
  using namespace lunaredu::app;
  reset_capabilities();
  static const char *callers[] = {"consumer"};
  static const CapabilityDefinition existing{
      "existing", "provider", 1, 1, 1,
      [](void *, const CapabilityCall &, std::string &) { return true; },
      nullptr, nullptr, callers, 1};
  static const CapabilityDefinition added{
      "added", "candidate", 1, 1, 1, existing.invoke,
      nullptr, nullptr, callers, 1};
  static const CapabilityDependency missing[] = {{"missing", 1, true}};
  std::string error;
  CHECK(register_capability(existing, error));
  AppController controller;
  ForegroundDefinition application;
  application.id = "candidate";
  static int declarations = 0;
  application.declare_capabilities = [](std::string &e) {
    ++declarations;
    return register_capability(added, e);
  };
  const ApplicationServiceRequirement invalid_services[] = {{nullptr, true}};
  application.manifest.services = invalid_services;
  application.manifest.service_count = 1;
  CHECK(!controller.register_foreground(application, error));
  CHECK(error == "invalid_application_service");
  CHECK(declarations == 0);
  CHECK(controller.snapshot().application_count == 0);
  CapabilityDefinition found;
  CHECK(!find_capability("added", "consumer", found));
  CHECK(find_capability("existing", "consumer", found));

  application.manifest = {};
  application.declare_capabilities = [](std::string &e) {
    if (!register_capability(existing, e) || !register_capability(added, e) ||
        !register_capability_consumer({"candidate", missing, 1}, e)) return false;
    e = "partial_declaration_failed";
    return false;
  };
  CHECK(!controller.register_foreground(application, error));
  CHECK(error == "partial_declaration_failed");
  CHECK(controller.snapshot().application_count == 0);
  CHECK(!find_capability("added", "consumer", found));
  CHECK(find_capability("existing", "consumer", found));
  // A leaked consumer would conflict with this retry and fail sealing.
  application.declare_capabilities = [](std::string &e) {
    return register_capability(added, e) &&
           register_capability_consumer({"candidate", nullptr, 0}, e);
  };
  CHECK(controller.register_foreground(application, error));
  CHECK(controller.snapshot().application_count == 1);
  CHECK(find_capability("added", "consumer", found));
  CHECK(seal_capabilities(error));
  reset_capabilities();
}

} // namespace

namespace lunaredu::lunaros {

bool device_capability_active(const char *id) {
  return id != nullptr &&
         std::find(g_capabilities.begin(), g_capabilities.end(), id) !=
             g_capabilities.end();
}

} // namespace lunaredu::lunaros

int main() {
  {
    lunaredu::app::AppController controller;
    lunaredu::app::ForegroundDefinition application;
    application.id="declaring";
    application.declare_capabilities=[](std::string &error){error="declaration_failed";return false;};
    std::string error;
    CHECK(!controller.register_foreground(application,error));
    CHECK(error=="declaration_failed");
    CHECK(controller.snapshot().application_count==0);
    static int declarations=0;
    application.declare_capabilities=[](std::string &){++declarations;return true;};
    CHECK(controller.register_foreground(application,error));
    CHECK(declarations==1);
    CHECK(controller.snapshot().active_application_id.empty());
  }

  test_registration_is_atomic();
  test_successful_switch_transfers_resource();
  test_missing_capability_preserves_foreground();
  test_external_resource_conflict_preserves_foreground();
  test_missing_service_preserves_foreground();
  test_failed_start_restores_previous_application();
  test_pending_failure_restores_previous_application();
  test_rollback_failure_enters_error_state();
  test_shared_and_exclusive_resource_leases();
  test_foreground_kinds_and_registry_headroom();
  test_boot_and_user_entry_reasons_reach_application();
  if (g_failures != 0) {
    std::cerr << g_failures << " LunarOS checks failed" << std::endl;
    return 1;
  }
  std::cout << "11 LunarOS application/resource checks passed" << std::endl;
  return 0;
}
