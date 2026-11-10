#include "network/network_service_internal.h"
#include "network/provisioning/network_provisioning.h"

#include <string>
#include "sdkconfig.h"

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/kernel/text.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/network/network.h"
#include "kernel/system_primitives.h"
#include "network/provisioning/provisioning_credentials.h"
#include "network/provisioning/provisioning_driver.h"
#include "network/provisioning/provisioning_restart.h"

namespace lunaredu::lunaros {

using lunaredu::lunaros::NetworkProvisioningPolicy;

namespace {

#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE

std::string g_softap_ssid;
bool g_softap_active = false;
ProvisioningRestartSchedule g_restart_after_submit;

constexpr uint32_t kProvisionSoftApIp = 0xC0A80401u;
constexpr uint32_t kProvisionSoftApGateway = 0xC0A80401u;
constexpr uint32_t kProvisionSoftApSubnet = 0xFFFFFF00u;
constexpr size_t kWifiSsidMaxBytes = 32;
constexpr size_t kSoftApSuffixBytes = 5;

NetworkProvisioningPolicy provisioning_policy() {
  return lunaredu::lunaros::network_service_internal().policy().provisioning;
}

bool mode_uses_softap(NetworkProvisioningMode mode) {
  return mode == NetworkProvisioningMode::SoftAp;
}

std::string build_alpha_suffix_from_uuid(const std::string &uuid) {
  uint32_t hash = 2166136261u;
  for (size_t index = 0; index < uuid.length(); ++index) {
    hash ^= static_cast<uint8_t>(uuid[index]);
    hash *= 16777619u;
  }

  char buffer[5];
  for (size_t index = 0; index < 4; ++index) {
    buffer[index] = static_cast<char>('A' + (hash % 26));
    hash /= 26;
  }
  buffer[4] = '\0';
  return std::string(buffer);
}

std::string build_provision_softap_ssid() {
  const DeviceConfig &config = device_config();
  std::string base_name = text::trim_copy(config.identity.provision_ap_name);
  if (base_name.empty()) {
    base_name = device_config_default_provision_ap_base_name();
  }

  const std::string suffix =
      "-" + build_alpha_suffix_from_uuid(config.identity.local_device_uuid);
  const size_t max_base_bytes = kWifiSsidMaxBytes > kSoftApSuffixBytes
                                    ? kWifiSsidMaxBytes - kSoftApSuffixBytes
                                    : 0;
  if (base_name.size() > max_base_bytes) {
    base_name.resize(max_base_bytes);
  }
  if (base_name.empty()) {
    base_name = "Lunar";
  }
  return base_name + suffix;
}

uint32_t now_ms() {
  return lunaredu::lunaros::internal::system_monotonic_ms();
}

void run_restart_after_submit_request() {
  if (!g_restart_after_submit.consume_if_due(now_ms())) {
    return;
  }

  LOGI("provision", "正在重启以应用新的 Wi-Fi 凭据");
  lunaredu::lunaros::internal::system_restart();
}

#endif

} // namespace

const char *network_provisioning_mode_name(NetworkProvisioningMode mode) {
  switch (mode) {
  case NetworkProvisioningMode::Disabled:
    return "disabled";
  case NetworkProvisioningMode::SoftAp:
    return "softap_only";
  }
  return "unknown";
}

#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
static bool start_network_provisioning_softap() {
  if (g_softap_active) {
    return true;
  }

  g_softap_ssid = build_provision_softap_ssid();
  provisioning_driver_configure_softap(
      kProvisionSoftApIp, kProvisionSoftApGateway, kProvisionSoftApSubnet);
  const bool ap_ok =
      provisioning_driver_start_softap(g_softap_ssid.c_str());
  if (!ap_ok) {
    LOGE("provision_ap", "启动配网热点失败");
    g_softap_ssid = "";
    return false;
  }

  provisioning_driver_start_captive_dns(kProvisionSoftApIp);
  g_softap_active = true;
  const ProvisioningWifiSnapshot wifi = provisioning_driver_wifi_snapshot();
  LOGI("provision_ap",
       "配网热点已开启：SSID=%s，地址=%s，可访问 http://%s",
       g_softap_ssid.c_str(), wifi.softap_ip.c_str(), wifi.softap_ip.c_str());
  return true;
}

static void stop_network_provisioning_softap() {
  if (!g_softap_active) {
    return;
  }

  provisioning_driver_stop_captive_dns();
  provisioning_driver_stop_softap();
  g_softap_active = false;
  g_softap_ssid = "";
  LOGI("provision_ap", "配网热点已关闭");
}
#endif

bool network_provisioning_softap_active() {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  return g_softap_active;
#else
  return false;
#endif
}

const char *network_provisioning_softap_ssid() {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  return g_softap_ssid.c_str();
#else
  return "";
#endif
}

bool start_network_provisioning_softap_only() {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  provisioning_driver_set_station_suspended(true);
  provisioning_driver_disconnect_station();
  return start_network_provisioning(NetworkProvisioningMode::SoftAp);
#else
  return false;
#endif
}

void stop_network_provisioning_softap_only() {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  provisioning_driver_set_station_suspended(false);
  stop_network_provisioning_softap();
#endif
}

NetworkProvisioningCredentialResult
submit_network_provisioning_wifi_credentials(const char *ssid,
                                             const char *password) {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  return apply_provisioning_wifi_credentials(ssid, password);
#else
  (void)ssid;
  (void)password;
  return NetworkProvisioningCredentialResult::NotReady;
#endif
}

uint32_t schedule_network_provisioning_restart() {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  const uint32_t delay_ms = g_restart_after_submit.schedule(
      now_ms(), provisioning_policy().restart_after_submit_delay_ms);
  LOGI("provision", "设备将在 %u ms 后重启并应用网络状态",
       static_cast<unsigned>(delay_ms));
  return delay_ms;
#else
  return 0;
#endif
}

void start_network_provisioning() {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  provisioning_driver_set_station_suspended(true);
  provisioning_driver_disconnect_station();
  start_network_provisioning(NetworkProvisioningMode::SoftAp);
#endif
}

bool start_network_provisioning(NetworkProvisioningMode mode) {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  if (mode == NetworkProvisioningMode::Disabled) {
    return false;
  }

  if (mode_uses_softap(mode)) {
    return start_network_provisioning_softap();
  }

  return false;
#else
  (void)mode;
  return false;
#endif
}

void stop_network_provisioning() {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  provisioning_driver_set_station_suspended(false);
  stop_network_provisioning_softap();
#endif
}

void tick_network_provisioning() {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  run_restart_after_submit_request();
#endif
}

bool network_provisioning_active() {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  return network_provisioning_softap_active();
#else
  return false;
#endif
}

} // namespace lunaredu::lunaros
