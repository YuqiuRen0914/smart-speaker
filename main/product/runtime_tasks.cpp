#include "lunaros_platform_link_service_id.h"
#include "lunaros_network_service_id.h"
#include "lunaros_provisioning_policy.h"
#include "lunaros_provisioning.h"
#include "runtime/product_runtime.h"
#include "kernel/executor_tasks.h"
#include "product/product_runtime_internal.h"
#include "product/speaker_desktop.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <optional>
#include <string>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "lunaros_platform_link.h"
#include "device_api/device_api.h"
#include <esp_log.h>
#include "kernel/managed_task.h"
#include "lunaros_network.h"
#include "lunaros_network_jobs.h"
#include "kernel/service_executor.h"
#include "kernel/service.h"
#include "lunaredu_ota.h"
#include "sdkconfig.h"
#include "smart_speaker_runtime.h"
#include "lunaredu_music_player.h"


namespace smart_speaker::product::runtime_internal {
namespace {
using namespace lunaredu::lunaros;
std::optional<ExecutorTaskGroup> g_executors;

constexpr const char *kTag = "smart_speaker.tasks";
constexpr const char *kSpeakerServiceId = "device.hardware";
constexpr uint32_t kPlayerStack =
    CONFIG_LUNAREDU_SMART_SPEAKER_PLAYER_TASK_STACK_SIZE;
constexpr uint32_t kNetworkStack =
    CONFIG_LUNAREDU_SMART_SPEAKER_NETWORK_TASK_STACK_SIZE;

#if CONFIG_FREERTOS_UNICORE
constexpr int kNetworkCore = 0;
#else
constexpr int kNetworkCore = 1;
#endif

uint32_t task_network(void *context) {
  auto &runtime = *static_cast<ProductRuntime *>(context);
  static bool provisioning_reported = false;
  static NetworkServicePollState network_poll_state;

  if (runtime.runtime().service_available(
          lunaredu::lunaros::kNetworkRuntimeServiceId)) {
    poll_network_services(runtime.context(), network_poll_state,
                          kNetworkPollIntervalMs);
    const bool provisioning = lunaredu::lunaros::network_fast_snapshot().local_access !=
                              lunaredu::lunaros::NetworkLocalAccessState::Idle;
    if (provisioning && !provisioning_reported) {
      const auto snapshot = lunaredu::lunaros::network_snapshot();
      // Direct local-console output; never retain credentials in the
      // HTTP-readable log ring.
      printf("Speaker setup Wi-Fi: %s password: %s\n",
             snapshot.local_access_ssid.c_str(),
             lunaredu::lunaros::provisioning_local_password().c_str());
    }
    provisioning_reported = provisioning;
#if CONFIG_LUNAREDU_SMART_SPEAKER_DEVELOPMENT_PLATFORM
      if (runtime.runtime().service_available(lunaredu::lunaros::kPlatformLinkServiceId)) {
        std::string switch_error;
        if (lunaredu::lunaros::platform_link_environment_restart_pending()) {
          (void)lunaredu::lunaros::request_platform_link_environment_restart(switch_error);
        } else if (lunaredu::lunaros::platform_link_environment() !=
                   lunaredu::lunaros::PlatformLinkEnvironment::Development) {
          (void)lunaredu::lunaros::request_platform_link_environment(
              lunaredu::lunaros::PlatformLinkEnvironment::Development, switch_error);
        }
      }
#endif
#if !CONFIG_LUNAREDU_SMART_SPEAKER_DEVELOPMENT_PLATFORM
      if (runtime.runtime().service_available(lunaredu::lunaros::kPlatformLinkServiceId)) {
        std::string error;
        if (lunaredu::lunaros::platform_link_environment_restart_pending())
          (void)lunaredu::lunaros::request_platform_link_environment_restart(error);
        else if (lunaredu::lunaros::platform_link_environment() != lunaredu::lunaros::PlatformLinkEnvironment::Production)
          (void)lunaredu::lunaros::request_platform_link_environment(lunaredu::lunaros::PlatformLinkEnvironment::Production, error);
      }
#endif
    }
    if(runtime.runtime().service_available(kSpeakerServiceId))smart_speaker::poll_library_refresh();
    return lunaredu::lunaros::kNetworkPollIntervalMs;
}

uint32_t task_main(void *context) {
  auto &runtime = *static_cast<ProductRuntime *>(context);
  const bool ready = g_executors && g_executors->healthy() &&
      runtime.runtime().service_available(kSpeakerServiceId) &&
      lunaredu::music::music_activity().audio_ready;
  const bool desktop_ready=smart_speaker::tick_desktop(runtime);
  runtime.poll(ready&&desktop_ready);

  if (runtime.runtime().service_available(kSpeakerServiceId)) {
    smart_speaker::tick_runtime();
  }
  return 10;
}

} // namespace

bool start_executor_tasks(ProductRuntime &runtime, std::string &error) {
  using namespace lunaredu::lunaros;
  g_executors.emplace(runtime.runtime());
  return g_executors->start(
      {{{.id = "speaker_main",
         .owner_service_id = "device.hardware",
         .role = "application",
         .stack_bytes = kPlayerStack,
         .priority = 5,
         .core_affinity = 0,
         .lifetime = ManagedTaskLifetime::Resident,
         .memory = ManagedTaskMemory::Internal},
        kMainServiceExecutor,
        task_main, &runtime},
       {{.id = "speaker_network",
         .owner_service_id = kNetworkRuntimeServiceId,
         .role = "network",
         .stack_bytes = kNetworkStack,
         .priority = 3,
         .core_affinity = kNetworkCore,
         .lifetime = ManagedTaskLifetime::Resident,
         .memory = ManagedTaskMemory::Internal},
        kNetworkServiceExecutor,
        task_network, &runtime}},
      error);
}

} // namespace smart_speaker::product::runtime_internal
