#include "lunaros_provisioning_policy.h"
#include "lunaros_network_policy.h"
#include "runtime/product_runtime.h"
#include "lunaredu_local_web_portal.h"
#include "lunaredu_ota.h"
#include "smart_speaker_audio.h"
#include "lunaros_diagnostics.h"

#include "sdkconfig.h"
#if CONFIG_LUNAROS_ENABLE_MICROPYTHON
#include "lunaros_micropython_service_id.h"
#endif
#include <string>

#include "device_api/device_operation.h"
#include <esp_log.h>
#include "kernel/runtime.h"

#include "product/product_runtime_internal.h"

namespace smart_speaker::product {
namespace {
constexpr const char *kTag = "smart_speaker";
lunaredu::lunaros::ProductRuntime runtime;
#if CONFIG_LUNAREDU_DEVICE_WEB
extern const uint8_t kPageStart[] asm("_binary_home_html_start");
extern const uint8_t kPageEnd[] asm("_binary_home_html_end");
#endif
}

void run_product() {
  smart_speaker::smart_speaker_quiet_audio_pins_early();
  std::string capture_error;
  if(!lunaredu::lunaros::start_diagnostics_boot_capture(capture_error))
    ESP_LOGW(kTag,"boot log capture unavailable: %s",capture_error.c_str());
  if (!smart_speaker::smart_speaker_validate_and_log_gpio_ownership())
    return;
  ESP_LOGI(kTag, "LunarOS smart speaker boot");
#if CONFIG_LUNAREDU_DEVICE_WEB
  std::string web_error;
  if (!lunaredu::lunaros::configure_device_web(
          {.product_name = "智能音箱", .application = {kPageStart, static_cast<size_t>(kPageEnd - kPageStart - 1)}, .bound_static_pages = true}, web_error)) {
    ESP_LOGE(kTag, "device portal configuration failed: %s", web_error.c_str());
    return;
  }
#endif
  std::string error;
  lunaredu::lunaros::NetworkPolicy policy;
  policy.initial_power_save = lunaredu::lunaros::NetworkWifiPowerSaveLevel::PERFORMANCE;
  lunaredu::lunaros::ProvisioningPolicy provisioning;
  provisioning.reconnect_failure_timeout_ms =
      CONFIG_LUNAREDU_SMART_SPEAKER_WIFI_PROVISION_TIMEOUT_MS;
  lunaredu::lunaros::RuntimeBootstrapStage failed_stage =
      lunaredu::lunaros::RuntimeBootstrapStage::CoreRuntime;
  if (!runtime.start(
          {{lunaredu::lunaros::kMainServiceExecutor, "product.main", 8},
           {lunaredu::lunaros::kNetworkServiceExecutor, "network.runtime", 8},
           lunaredu::lunaros::maintenance_io_executor(),
#if CONFIG_LUNAROS_ENABLE_MICROPYTHON
           {lunaredu::micropython::kRuntimeExecutorId, lunaredu::micropython::kRuntimeServiceId,
            static_cast<uint8_t>(CONFIG_LUNAREDU_MICROPYTHON_COMMAND_QUEUE_DEPTH)},
#endif
           },
          policy, "CST-8",
          {.provisioning=&provisioning,.register_services = [](void *owner, std::string &failure) {
             return runtime_internal::register_services(
                 *static_cast<lunaredu::lunaros::ProductRuntime *>(owner), failure);
           },
           .start_executor_tasks = [](void *owner, std::string &failure) {
             return runtime_internal::start_executor_tasks(
                 *static_cast<lunaredu::lunaros::ProductRuntime *>(owner), failure);
           }, .context = &runtime, .boot_healthy_period_ms = 15000},
          &failed_stage, error)) {
    ESP_LOGE(kTag, "runtime bootstrap failed stage=%s: %s",
         lunaredu::lunaros::runtime_bootstrap_stage_name(failed_stage),
         error.c_str());
    (void)lunaredu::lunaros::ota_service_rollback_running_image(
        lunaredu::lunaros::runtime_bootstrap_failure_reason(failed_stage));
    return;
  }
  // The main executor confirms only after the maintenance path stays healthy.
}

} // namespace smart_speaker::product

extern "C" void app_main(void) { smart_speaker::product::run_product(); }
