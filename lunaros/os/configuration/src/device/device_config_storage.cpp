#include "device_config_mutation.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <utility>
#include <atomic>
#include "device_config_record.h"

#include <esp_log.h>
#include "platform/text.h"
#include "device_config_defaults.h"
#include "storage/nvs_backend.h"

#include "platform/system.h"

namespace lunaredu::lunaros {

namespace {

constexpr const char *kDeviceConfigNvsNamespace = "device_cfg";
constexpr const char *kRecoveryPartition = "recovery_state";
constexpr const char *kRecoveryNamespace = "recovery_cfg";
std::atomic<bool> g_recovery_snapshot_available{false};
} // 匿名命名空间

std::string derive_local_device_uuid() {
  uint8_t mac[6] = {0};
  if (!lunaredu::lunaros::system_read_factory_mac(mac, sizeof(mac))) {
    return "00000000-0000-4000-8000-000000000000";
  }
  uint64_t chip_id = 0;
  for (uint8_t byte : mac) {
    chip_id = (chip_id << 8) | byte;
  }
  const uint32_t upper = static_cast<uint32_t>(chip_id >> 32);
  const uint32_t lower = static_cast<uint32_t>(chip_id & 0xFFFFFFFFULL);
  const uint32_t hash_a = lower ^ 0xA5C35A1Fu;
  const uint32_t hash_b = upper ^ 0x5AF0C3A5u;

  uint8_t bytes[16] = {
      static_cast<uint8_t>((upper >> 8) & 0xFF),
      static_cast<uint8_t>(upper & 0xFF),
      static_cast<uint8_t>((lower >> 24) & 0xFF),
      static_cast<uint8_t>((lower >> 16) & 0xFF),
      static_cast<uint8_t>((lower >> 8) & 0xFF),
      static_cast<uint8_t>(lower & 0xFF),
      static_cast<uint8_t>((hash_a >> 24) & 0xFF),
      static_cast<uint8_t>((hash_a >> 16) & 0xFF),
      static_cast<uint8_t>((hash_a >> 8) & 0xFF),
      static_cast<uint8_t>(hash_a & 0xFF),
      static_cast<uint8_t>((hash_b >> 24) & 0xFF),
      static_cast<uint8_t>((hash_b >> 16) & 0xFF),
      static_cast<uint8_t>((hash_b >> 8) & 0xFF),
      static_cast<uint8_t>(hash_b & 0xFF),
      static_cast<uint8_t>((upper >> 24) & 0xFF),
      static_cast<uint8_t>((upper >> 16) & 0xFF),
  };

  bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);
  bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);

  char buffer[37];
  snprintf(buffer, sizeof(buffer),
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
           "%02x%02x%02x%02x%02x%02x",
           bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
           bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
           bytes[12], bytes[13], bytes[14], bytes[15]);
  return std::string(buffer);
}

static std::string build_alpha_suffix_from_uuid(const std::string &uuid) {
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

bool has_short_mac_suffix(const std::string &value) {
  if (value.size() < 7 || value[value.size() - 7] != '-') return false;
  for (size_t index = value.size() - 6; index < value.size(); ++index) {
    if (!std::isxdigit(static_cast<unsigned char>(value[index]))) return false;
  }
  return true;
}

std::string default_device_base_name() {
  std::string base_name = text::trim_copy(DEVICE_CONFIG_DEFAULT_DEVICE_NAME);
  return base_name.empty() ? std::string("Lunar Hardware Device") : base_name;
}

std::string default_provision_ap_base_name() {
  std::string base_name =
      text::trim_copy(DEVICE_CONFIG_DEFAULT_PROVISION_AP_NAME);
  return base_name.empty() ? std::string("LunarHardware") : base_name;
}

std::string build_default_device_name(const std::string &device_uuid) {
  std::string base_name = text::trim_copy(default_device_base_name());
  return base_name + "-" + build_alpha_suffix_from_uuid(device_uuid);
}

std::string device_config_provision_ap_ssid(const LocalDeviceIdentityConfig &identity) {
  std::string base = text::trim_copy(identity.provision_ap_name);
  if (base.empty()) base = default_provision_ap_base_name();
  // Generic device identity only. Product names have no special cases here.
  if (base.size() <= kDeviceWifiSsidMax && has_short_mac_suffix(base)) return base;
  // Four suffix letters plus the separator leave 27 bytes for the base name.
  if (base.size() > kDeviceWifiSsidMax - 5) base.resize(kDeviceWifiSsidMax - 5);
  if (base.empty()) base = "Lunar";
  return base + "-" + build_alpha_suffix_from_uuid(identity.local_device_uuid);
}

namespace {
DeviceConfig default_device_config() {
  DeviceConfig config;
  config.identity.local_device_uuid = derive_local_device_uuid();
  config.identity.name = build_default_device_name(config.identity.local_device_uuid);
  config.identity.provision_ap_name = default_provision_ap_base_name();
  return config;
}

} // namespace

bool commit_device_config(DeviceConfig config, DeviceConfigWriteError *outcome) {
  if (outcome) *outcome = DeviceConfigWriteError::None;
  std::string record, error;
  if (configuration_recovery_active()) {
    if (outcome) *outcome = DeviceConfigWriteError::NotReady;
    return false;
  }
  if (!nvs_store_ready()) { if (outcome) *outcome = DeviceConfigWriteError::NotReady; return false; }
  if (!encode_device_config_record(config, record, true)) { if (outcome) *outcome = DeviceConfigWriteError::EncodeFailed; return false; }
  if (!nvs_store_write_blob(kDeviceConfigNvsNamespace, kDeviceConfigRecordKey, record, &error)) {
    if (outcome) *outcome = DeviceConfigWriteError::CommitFailed;
    ESP_LOGE("device_config", "configuration commit failed: %s", error.c_str());
    return false;
  }
  mirror_recovery_configuration(record);
  publish_device_config(std::move(config));
  return true;
}

bool configuration_recovery_snapshot_available() { return g_recovery_snapshot_available; }

void mirror_recovery_configuration(const std::string &record) {
  std::string error, previous;
  g_recovery_snapshot_available = false;
  // Optional in v2; never create or format a partition to manufacture backup.
  if (!initialize_nvs_partition(kRecoveryPartition, error)) return;
  const auto read = nvs_store_read_blob(kRecoveryNamespace, kDeviceConfigRecordKey,
      kDeviceConfigRecordMaxBytes, previous, &error, kRecoveryPartition);
  if (read == NvsReadResult::Found && previous == record) {
    g_recovery_snapshot_available = true; return;
  }
  g_recovery_snapshot_available = nvs_store_write_blob(kRecoveryNamespace,
      kDeviceConfigRecordKey, record, &error, kRecoveryPartition);
  if (!g_recovery_snapshot_available)
    ESP_LOGW("device_config", "independent recovery configuration unavailable");
}

bool load_recovery_configuration() {
  std::string error, record;
  g_recovery_snapshot_available = false;
  if (!initialize_nvs_partition(kRecoveryPartition, error)) return false;
  DeviceConfig config;
  if (nvs_store_read_blob(kRecoveryNamespace, kDeviceConfigRecordKey,
          kDeviceConfigRecordMaxBytes, record, &error, kRecoveryPartition) != NvsReadResult::Found ||
      !decode_device_config_record(record, config) ||
      config.identity.local_device_uuid != derive_local_device_uuid()) return false;
  // CRC detects corruption, not authorization. Grants still require the cloud
  // credential, current binding epoch, audience and valid expiration time.
  publish_device_config(std::move(config));
  g_recovery_snapshot_available = true;
  return true;
}

void initialize_device_config() {
  StaticLockGuard write_lock(device_config_write_mutex());
  publish_device_config(default_device_config());
}

bool load_device_config() {
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig config = default_device_config();
  if (!nvs_store_ready()) {
    ESP_LOGW("device_config", "NVS 尚未就绪，当前保持初始化的空网络配置");
    return false;
  }

  std::string record, error;
  const auto stored = nvs_store_read_blob(kDeviceConfigNvsNamespace,
      kDeviceConfigRecordKey, kDeviceConfigRecordMaxBytes, record, &error);
  if (stored == NvsReadResult::Failed) {
    ESP_LOGE("device_config", "configuration read failed: %s", error.c_str());
    return false;
  }
  if (stored == NvsReadResult::Found) {
    bool migration_required = false;
    if (!decode_device_config_record(record, config, &migration_required)) {
      ESP_LOGE("device_config", "invalid or unsupported configuration record");
      return false;
    }
    const auto uuid = derive_local_device_uuid();
    // Reading LDC8 is a migration into the current in-memory schema. Preserve
    // its compatible disk representation until a field genuinely requires 9.
    if (config.identity.local_device_uuid != uuid) {
      config.identity.local_device_uuid = uuid;
      const auto retained = config;
      if (!commit_device_config(std::move(config))) {
        // The complete old record was validated. Preserve its credentials for
        // management without claiming a migration commit or overwriting it.
        publish_device_config(DeviceConfig(retained));
        activate_configuration_recovery();
        g_recovery_snapshot_available = true;
      }
    } else {
      mirror_recovery_configuration(record);
      publish_device_config(std::move(config));
    }
    return true;
  }
  // Missing current record is a new-device configuration. Old split keys
  // and split-key schemas are never read, rewritten or erased here.
  // An existing independent identity means this is not a new device. Do not
  // destroy its recovery copy by publishing a fresh empty primary snapshot.
  if (load_recovery_configuration()) return false;
  return commit_device_config(std::move(config));
}

bool save_device_wifi_last_success_index(uint8_t index, DeviceConfigWriteError &error) {
  error = DeviceConfigWriteError::None;
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig config = device_config();
  if (index >= config.wifi.credential_count) { error = DeviceConfigWriteError::InvalidValue; return false; }
  if (config.wifi.last_success_index == index) return true;
  config.wifi.last_success_index = index;
  return commit_device_config(std::move(config), &error);
}
bool save_device_wifi_last_success_index(uint8_t index) {
  DeviceConfigWriteError error; return save_device_wifi_last_success_index(index, error);
}

} // namespace lunaredu::lunaros
