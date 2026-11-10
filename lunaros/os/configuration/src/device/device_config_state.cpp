#include "configuration/device_config.h"

#include "device_config_mutation.h"
#include "configuration/wifi_credentials.h"

#include <utility>
#include <algorithm>
#include <cstring>
#include <atomic>

#include "device_config_defaults.h"
#include "storage/nvs_backend.h"

namespace lunaredu::lunaros {

namespace {
StaticMutex g_state_mutex;
StaticMutex g_write_mutex;
DeviceConfig g_device_config;
uint64_t g_credential_fingerprint = 14695981039346656037ULL;
std::atomic<bool> g_recovery_enabled{false};
std::atomic<bool> g_recovery_active{false};
} // namespace

StaticMutex &device_config_write_mutex() { return g_write_mutex; }

void publish_device_config(DeviceConfig &&config) {
  StaticLockGuard lock(g_state_mutex);
  if (config.cloud.credential != g_device_config.cloud.credential) {
    uint64_t fingerprint = 14695981039346656037ULL;
    for (unsigned char byte : config.cloud.credential) {
      fingerprint ^= byte;
      fingerprint *= 1099511628211ULL;
    }
    g_credential_fingerprint = fingerprint;
  }
  g_device_config = std::move(config);
}

bool build_device_wifi_config(const DeviceWifiCredential *credentials,
                              size_t credential_count,
                              uint8_t last_success_index,
                              DeviceWifiConfig &target) {
  if (!valid_device_wifi_list(credentials,credential_count) || last_success_index>=credential_count) {
    return false;
  }
  target = DeviceWifiConfig{};
  for(size_t i=0;i<credential_count;++i) target.credentials[i]=credentials[i];
  target.credential_count=static_cast<uint8_t>(credential_count);
  target.last_success_index=last_success_index;
  target.configured=true;
  return true;
}

bool initialize_configuration(std::string &error) {
  error.clear();
  g_recovery_active = false;
  initialize_device_config();
  if (setup_nvs_store(&error) && load_device_config()) return true;
  if (!g_recovery_enabled) {
    if (error.empty()) error = "configuration_load_failed";
    return false;
  }
  // Keep damaged primary bytes intact. A missing/corrupt independent snapshot
  // still permits local provisioning discovery, but never administrator access.
  g_recovery_active = true;
  (void)load_recovery_configuration();
  error.clear();
  return true;
}

void enable_configuration_recovery() { g_recovery_enabled = true; }
void activate_configuration_recovery() { g_recovery_active = true; }
bool configuration_recovery_active() { return g_recovery_active; }
bool restore_configuration_from_recovery(std::string &error) {
  StaticLockGuard lock(device_config_write_mutex());
  if (!g_recovery_active || !configuration_recovery_snapshot_available()) {
    error = "recovery_configuration_unavailable"; return false;
  }
  if (!setup_nvs_store(&error)) return false;
  // Hold the writer gate while temporarily admitting this explicit repair.
  g_recovery_active = false;
  DeviceConfigWriteError outcome;
  if (!commit_device_config(device_config(), &outcome)) {
    g_recovery_active = true;
    error = device_config_write_error_code(outcome); return false;
  }
  error.clear(); return true;
}

DeviceConfig device_config() {
  StaticLockGuard lock(g_state_mutex);
  return g_device_config;
}
DeviceWifiConfig device_wifi_config() { StaticLockGuard lock(g_state_mutex); return g_device_config.wifi; }
DeviceCloudConfig device_cloud_config() { StaticLockGuard lock(g_state_mutex); return g_device_config.cloud; }
std::string device_cloud_credential() { StaticLockGuard lock(g_state_mutex); return g_device_config.cloud.credential; }
uint64_t device_cloud_credential_fingerprint() { StaticLockGuard lock(g_state_mutex); return g_credential_fingerprint; }
bool device_wifi_configured() { StaticLockGuard lock(g_state_mutex); return g_device_config.wifi.configured; }
DeviceNetworkConfig device_network_config() { StaticLockGuard lock(g_state_mutex); return {g_device_config.wifi,g_device_config.identity}; }
DeviceWifiNames device_wifi_names() {
  StaticLockGuard lock(g_state_mutex);
  DeviceWifiNames names;
  names.count=std::min<uint8_t>(g_device_config.wifi.credential_count,kDeviceWifiCredentialMax);
  for(uint8_t i=0;i<names.count;++i){
    const auto &ssid=g_device_config.wifi.credentials[i].ssid;
    std::memcpy(names.ssids[i].data(),ssid.data(),std::min(ssid.size(),kDeviceWifiSsidMax));
  }
  return names;
}

LocalDeviceIdentityConfig device_identity_config() {
  StaticLockGuard lock(g_state_mutex);
  return g_device_config.identity;
}

bool device_cloud_credential_ready(size_t minimum_length) {
  StaticLockGuard lock(g_state_mutex);
  return g_device_config.cloud.configured &&
         g_device_config.cloud.credential.size() >= minimum_length;
}

const char *device_config_default_device_base_name() {
  static const std::string name = default_device_base_name();
  return name.c_str();
}

bool device_config_name_is_default() {
  const auto identity = device_identity_config();
  const std::string &name = identity.name;
  return name.empty() || name == default_device_base_name() ||
         name == build_default_device_name(
                     identity.local_device_uuid);
}

const char *device_config_default_provision_ap_base_name() {
  static const std::string name = default_provision_ap_base_name();
  return name.c_str();
}

} // namespace lunaredu::lunaros
