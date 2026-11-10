#include "device_config_mutation.h"

#include <utility>

#include "persistence/settings/device_config_defaults.h"
#include "persistence/settings/nvs_store.h"

namespace lunaredu::lunaros {

namespace {
StaticMutex g_state_mutex;
StaticMutex g_write_mutex;
DeviceConfig g_device_config;
} // namespace

StaticMutex &device_config_write_mutex() { return g_write_mutex; }

void publish_device_config(DeviceConfig &&config) {
  StaticLockGuard lock(g_state_mutex);
  g_device_config = std::move(config);
}

bool build_device_wifi_config(const DeviceWifiCredential *credentials,
                              size_t credential_count,
                              uint8_t last_success_index,
                              DeviceWifiConfig &target) {
  if (credentials == nullptr || credential_count == 0) {
    return false;
  }
  target = DeviceWifiConfig{};
  uint8_t next_count = 0;
  uint8_t next_last_success_index = 0;
  for (size_t index = 0;
       credentials != nullptr && index < credential_count &&
       next_count < kDeviceWifiCredentialMax;
       ++index) {
    const std::string &ssid = credentials[index].ssid;
    if (ssid.empty()) {
      continue;
    }
    if (ssid.length() > kDeviceWifiSsidMax ||
        credentials[index].password.length() > kDeviceWifiPasswordMax) {
      return false;
    }

    bool duplicate = false;
    for (uint8_t existing = 0; existing < next_count; ++existing) {
      if (target.credentials[existing].ssid == ssid) {
        duplicate = true;
        if (index == last_success_index) {
          next_last_success_index = existing;
        }
        break;
      }
    }
    if (duplicate) {
      continue;
    }

    target.credentials[next_count].ssid = ssid;
    target.credentials[next_count].password = credentials[index].password;
    if (index == last_success_index) {
      next_last_success_index = next_count;
    }
    ++next_count;
  }

  if (next_count == 0) {
    return false;
  }

  target.credential_count = next_count;
  target.last_success_index = next_last_success_index;
  target.configured = next_count > 0;
  return true;
}

bool setup_device_config_service() {
  const bool nvs_ready = setup_nvs_store();
  initialize_device_config();
  return nvs_ready && load_device_config();
}

DeviceConfig device_config() {
  StaticLockGuard lock(g_state_mutex);
  return g_device_config;
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
  const auto config = device_config();
  const std::string &name = config.identity.name;
  return name.empty() || name == default_device_base_name() ||
         name == build_default_device_name(
                     config.identity.local_device_uuid);
}

const char *device_config_default_provision_ap_base_name() {
  static const std::string name = default_provision_ap_base_name();
  return name.c_str();
}

} // namespace lunaredu::lunaros
