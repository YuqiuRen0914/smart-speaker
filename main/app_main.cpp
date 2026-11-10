#include "lunaredu_lunaros/device_web/portal.h"
#include "lunaredu_lunaros/delivery/update.h"
#include "smart_speaker_audio.h"
#include "smart_speaker_log_buffer.h"

#include "sdkconfig.h"
#include <string>

#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/device_api/device_api.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/kernel/runtime.h"
#include "lunaredu_lunaros/kernel/service_host.h"

#include "product/product_runtime_internal.h"

namespace smart_speaker::product {
namespace {
constexpr const char *kTag = "smart_speaker";
#if CONFIG_LUNAREDU_DEVICE_WEB
extern const uint8_t kPageStart[] asm("_binary_home_html_start");
extern const uint8_t kPageEnd[] asm("_binary_home_html_end");
#endif
}

void run_product() {
  smart_speaker::smart_speaker_quiet_audio_pins_early();
  smart_speaker::setup_log_buffer();
  if (!smart_speaker::smart_speaker_validate_and_log_gpio_ownership())
    return;
  LOGI(kTag, "LunarOS smart speaker boot");
#if CONFIG_LUNAREDU_DEVICE_WEB
  std::string web_error;
  if (!lunaredu::lunaros::configure_device_web(
          {.product_name = "智能音箱", .home = {kPageStart, static_cast<size_t>(kPageEnd - kPageStart - 1)}}, web_error)) {
    LOGE(kTag, "device portal configuration failed: %s", web_error.c_str());
    return;
  }
#endif
  std::string error;
  if (!lunaredu::lunaros::setup_core_runtime({{lunaredu::lunaros::kMainServiceExecutor, "product.main", 8},
      {lunaredu::lunaros::kNetworkServiceExecutor, "network.runtime", 8},
      {lunaredu::lunaros::kNetworkIoServiceExecutor, "network.runtime", 2}}, error)) {
    LOGE(kTag, "core setup failed: %s", error.c_str());
    (void)lunaredu::lunaros::ota_service_rollback_running_image("lunaros_core_setup_failed");
    return;
  }
  lunaredu::lunaros::NetworkPolicy policy;
  policy.initial_power_save = lunaredu::lunaros::NetworkWifiPowerSaveLevel::PERFORMANCE;
  policy.provisioning.reconnect_failure_timeout_ms =
      CONFIG_LUNAREDU_SMART_SPEAKER_WIFI_PROVISION_TIMEOUT_MS;
  if (!lunaredu::lunaros::configure_network_runtime(policy, "CST-8", error)) {
    LOGE(kTag, "network setup failed: %s", error.c_str());
    (void)lunaredu::lunaros::ota_service_rollback_running_image("network_service_binding_failed");
    return;
  }
  if (!runtime_internal::register_services(error)) {
    LOGE(kTag, "service registration failed: %s", error.c_str());
    (void)lunaredu::lunaros::ota_service_rollback_running_image("service_registry_failed");
    return;
  }
  if (!lunaredu::lunaros::seal_device_projection_registry(&error)) {
    LOGE(kTag, "projection sealing failed: %s", error.c_str());
    (void)lunaredu::lunaros::ota_service_rollback_running_image(
        "device_projection_sealing_failed");
    return;
  }
  if (!runtime_internal::start_executor_tasks(error)) {
    LOGE(kTag, "managed task startup failed: %s", error.c_str());
    (void)lunaredu::lunaros::ota_service_rollback_running_image("managed_task_start_failed");
    return;
  }
  if (!lunaredu::lunaros::start_all_services(error)) {
    LOGE(kTag, "service startup failed: %s", error.c_str());
    (void)lunaredu::lunaros::ota_service_rollback_running_image("service_start_failed");
    return;
  }
  // The main executor confirms only after the maintenance path stays healthy.
}

} // namespace smart_speaker::product

extern "C" void app_main(void) { smart_speaker::product::run_product(); }
