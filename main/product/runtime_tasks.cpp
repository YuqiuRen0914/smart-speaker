#include "lunaredu_lunaros/delivery/boot_health.h"
#include "lunaredu_lunaros/kernel/executor_tasks.h"
#include "lunaredu_lunaros/network/network_job_pump.h"
#include "product/product_runtime_internal.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "lunaredu_lunaros/device_link/device_link.h"
#include "lunaredu_lunaros/device_api/device_api.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/kernel/managed_task.h"
#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/network/network_commands.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "lunaredu_lunaros/kernel/service_registry.h"
#include "lunaredu_lunaros/delivery/update.h"
#include "sdkconfig.h"
#include "smart_speaker_runtime.h"
#include "smart_speaker_xiaozhi.h"

namespace smart_speaker::product::runtime_internal {
namespace {
using namespace lunaredu::lunaros;
ExecutorTaskGroup g_executors;
BootHealthGate g_boot_health{15000};

constexpr const char *kTag = "smart_speaker.tasks";
constexpr const char *kSpeakerServiceId = "device.hardware";
constexpr uint32_t kPlayerStack =
    CONFIG_LUNAREDU_SMART_SPEAKER_PLAYER_TASK_STACK_SIZE;
constexpr uint32_t kNetworkStack =
    CONFIG_LUNAREDU_SMART_SPEAKER_NETWORK_TASK_STACK_SIZE;
constexpr uint32_t kNetworkIoStack = 12288;

#if CONFIG_FREERTOS_UNICORE
constexpr int kNetworkCore = 0;
#else
constexpr int kNetworkCore = 1;
#endif

uint32_t task_network(void *) {
  static bool provisioning_reported = false;

  if (lunaredu::lunaros::service_available(
          lunaredu::lunaros::kNetworkRuntimeServiceId)) {
    lunaredu::lunaros::poll_network_on_owner();
    const bool provisioning = lunaredu::lunaros::network_provisioning_state() !=
                              lunaredu::lunaros::NetworkProvisioningState::Idle;
    if (provisioning && !provisioning_reported) {
      const auto snapshot = lunaredu::lunaros::network_snapshot();
      // Direct local-console output; never retain credentials in the
      // HTTP-readable log ring.
      printf("Speaker setup Wi-Fi: %s password: %s\n",
             snapshot.provisioning_ssid.c_str(),
             lunaredu::lunaros::network_local_provisioning_password().c_str());
    }
    provisioning_reported = provisioning;
    if (smart_speaker::take_provisioning_cancel_request()) {
      std::string error;
      if (!lunaredu::lunaros::request_network_stop_provisioning(error))
        LOGW(kTag, "cancel provisioning failed: %s", error.c_str());
    } else if (smart_speaker::take_provisioning_request()) {
      std::string error;
      (void)lunaredu::lunaros::request_network_start_provisioning(
          lunaredu::lunaros::NetworkProvisioningMode::SoftAp, true, error);
    }
#if CONFIG_LUNAREDU_SMART_SPEAKER_DEVELOPMENT_PLATFORM
      if (lunaredu::lunaros::service_available(lunaredu::lunaros::kDeviceLinkServiceId)) {
        std::string switch_error;
        if (lunaredu::lunaros::device_link_environment_restart_pending()) {
          (void)lunaredu::lunaros::request_device_link_environment_restart(switch_error);
        } else if (lunaredu::lunaros::device_link_environment() !=
                   lunaredu::lunaros::DeviceLinkEnvironment::Development) {
          (void)lunaredu::lunaros::request_device_link_environment(
              lunaredu::lunaros::DeviceLinkEnvironment::Development, switch_error);
        }
      }
#endif
#if !CONFIG_LUNAREDU_SMART_SPEAKER_DEVELOPMENT_PLATFORM
      if (lunaredu::lunaros::service_available(lunaredu::lunaros::kDeviceLinkServiceId)) {
        std::string error;
        if (lunaredu::lunaros::device_link_environment_restart_pending())
          (void)lunaredu::lunaros::request_device_link_environment_restart(error);
        else if (lunaredu::lunaros::device_link_environment() != lunaredu::lunaros::DeviceLinkEnvironment::Production)
          (void)lunaredu::lunaros::request_device_link_environment(lunaredu::lunaros::DeviceLinkEnvironment::Production, error);
      }
#endif
      lunaredu::lunaros::tick_device_link_runtime();
      lunaredu::lunaros::tick_ota_service();
      if (lunaredu::lunaros::service_available("assistant.xiaozhi")) {
        smart_speaker::tick_xiaozhi_service();
      }
    }
    return lunaredu::lunaros::kNetworkPollIntervalMs;
}

uint32_t task_main(void *) {
  poll_boot_health(g_boot_health, !ota_service_boot_pending() || (
                   g_executors.healthy() &&
                       service_available(kSpeakerServiceId) &&
                       smart_speaker::status_snapshot().audio_ready));

  if (lunaredu::lunaros::service_available(kSpeakerServiceId)) {
    smart_speaker::tick_runtime();
  }
  return 10;
}

} // namespace

bool start_executor_tasks(std::string &error) {
  using namespace lunaredu::lunaros;
  return g_executors.start(
      {{{.id = "speaker_main",
         .owner_service_id = "device.hardware",
         .role = "application",
         .stack_bytes = kPlayerStack,
         .priority = 5,
         .core_affinity = 0,
         .lifetime = ManagedTaskLifetime::Resident,
         .memory = ManagedTaskMemory::Internal},
        kMainServiceExecutor,
        task_main},
       {{.id = "speaker_network",
         .owner_service_id = kNetworkRuntimeServiceId,
         .role = "network",
         .stack_bytes = kNetworkStack,
         .priority = 3,
         .core_affinity = kNetworkCore,
         .lifetime = ManagedTaskLifetime::Resident,
         .memory = ManagedTaskMemory::Internal},
        kNetworkServiceExecutor,
        task_network},
       {{.id = "speaker_netio",
         .owner_service_id = kNetworkRuntimeServiceId,
         .role = "blocking_io",
         .stack_bytes = kNetworkIoStack,
         // TLS can compute for seconds. Keep it off the display/decoder core
         // and share idle priority so both watchdog idle hooks still run.
         // Audio output and application input retain their higher priorities.
         .priority = tskIDLE_PRIORITY,
         .core_affinity = 0,
         .lifetime = ManagedTaskLifetime::Resident,
         .memory = ManagedTaskMemory::Internal},
        kNetworkIoServiceExecutor,
        poll_network_job_pump,
        nullptr,
        enter_network_job_pump,
        leave_network_job_pump}},
      error);
}

} // namespace smart_speaker::product::runtime_internal
