#include "device_config_mutation.h"

#include <charconv>
#include <cstdio>
#include <string>
#include <utility>
#include "persistence/settings/device_config_record.h"

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/kernel/text.h"
#include "persistence/settings/device_config_defaults.h"
#include "persistence/settings/nvs_store.h"

#include "kernel/system_primitives.h"

namespace lunaredu::lunaros {

namespace {

constexpr const char *kDeviceConfigNvsNamespace = "device_cfg";
constexpr const char *kSchemaVersionKey = "schema_ver";
constexpr const char *kSchemaVersion = "7";
constexpr const char *kWifiCountKey = "wifi_count";
constexpr const char *kWifiLastKey = "wifi_last";
constexpr const char *kWifiFactoryResetKey = "wifi_factory";
constexpr const char *kDeviceUuidKey = "dev_uuid";
constexpr const char *kDeviceNameKey = "dev_name";
constexpr const char *kProvisionApKey = "prov_ap";
constexpr const char *kCloudEnvironmentKey = "cloud_env";
constexpr const char *kCloudCredentialKey = "cloud_cred";

// Accessed only during legacy loading while device_config_write_mutex is held.
bool legacy_read_failed = false;

std::string make_indexed_key(const char *prefix, uint8_t index) {
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%s%u", prefix,
           static_cast<unsigned>(index));
  return std::string(buffer);
}

bool read_config_string(const char *key, std::string &value) {
  const auto result = nvs_store_read_string_bounded(kDeviceConfigNvsNamespace,
      key, kDeviceConfigRecordMaxBytes, value);
  legacy_read_failed |= result == NvsReadResult::Failed;
  return result == NvsReadResult::Found;
}

bool read_uint8_key(const char *key, uint8_t &value, uint8_t fallback = 0) {
  std::string raw;
  if (!read_config_string(key, raw)) {
    value = fallback;
    return false;
  }

  unsigned parsed = 0;
  const char *begin = raw.data();
  const char *end = begin + raw.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc() || result.ptr != end || parsed > 255) {
    value = fallback;
    return false;
  }

  value = static_cast<uint8_t>(parsed);
  return true;
}

std::string read_config_string_or_default(const char *key,
                                          const std::string &fallback,
                                          size_t max_length,
                                          bool &needs_rewrite) {
  std::string value;
  if (!read_config_string(key, value)) {
    needs_rewrite = true;
    return fallback;
  }

  value = text::trim_copy(value);
  if (value.empty() || value.length() > max_length) {
    needs_rewrite = true;
    return fallback;
  }

  return value;
}

bool load_wifi_credentials(DeviceConfig &config, bool &needs_rewrite,
                           bool remove_retired_default,
                           bool remove_pre_schema6_default) {
  std::string factory_reset_marker;
  if (read_config_string(kWifiFactoryResetKey, factory_reset_marker) &&
      factory_reset_marker == "1") {
    config.wifi = DeviceWifiConfig{};
    return true;
  }

  uint8_t credential_count = 0;
  if (!read_uint8_key(kWifiCountKey, credential_count, 0)) {
    config.wifi = {};
    needs_rewrite = true;
    return false;
  }
  if (credential_count > kDeviceWifiCredentialMax) {
    credential_count = kDeviceWifiCredentialMax;
    needs_rewrite = true;
  }

  DeviceWifiCredential credentials[kDeviceWifiCredentialMax];
  uint8_t loaded_count = 0;
  for (uint8_t index = 0; index < credential_count; ++index) {
    const std::string ssid_key = make_indexed_key("ssid", index);
    const std::string pass_key = make_indexed_key("pass", index);

    std::string ssid;
    if (!read_config_string(ssid_key.c_str(), ssid)) {
      needs_rewrite = true;
      continue;
    }
    if (ssid.empty() || ssid.length() > kDeviceWifiSsidMax) {
      needs_rewrite = true;
      continue;
    }

    std::string password;
    if (!read_config_string(pass_key.c_str(), password)) {
      password = "";
    }
    if (password.length() > kDeviceWifiPasswordMax) {
      needs_rewrite = true;
      continue;
    }

    if (remove_retired_default &&
        ssid == DEVICE_CONFIG_RETIRED_DEFAULT_WIFI_SSID &&
        password == DEVICE_CONFIG_RETIRED_DEFAULT_WIFI_PASSWORD) {
      // Schema 7 removes the last firmware-injected classroom network. New
      // firmware persists only credentials explicitly submitted by a user.
      needs_rewrite = true;
      continue;
    }

    if (remove_pre_schema6_default &&
        ssid == DEVICE_CONFIG_LEGACY_DEFAULT_WIFI_SSID &&
        password == DEVICE_CONFIG_LEGACY_DEFAULT_WIFI_PASSWORD) {
      // Before schema 6 this pair was also injected by firmware. Schema 6
      // made the same pair legal as an explicitly submitted classroom Wi-Fi,
      // so schema 6+ values must be preserved.
      needs_rewrite = true;
      continue;
    }

    credentials[loaded_count].ssid = ssid;
    credentials[loaded_count].password = password;
    ++loaded_count;
  }

  uint8_t last_success_index = 0;
  if (!read_uint8_key(kWifiLastKey, last_success_index, 0)) {
    needs_rewrite = true;
  }

  if (loaded_count == 0) {
    config.wifi = {};
    needs_rewrite = true;
    return false;
  }

  if (!build_device_wifi_config(credentials, loaded_count,
                                   last_success_index, config.wifi)) {
    config.wifi = {};
    needs_rewrite = true;
    return false;
  }
  if (last_success_index >= loaded_count ||
      config.wifi.credential_count != loaded_count ||
      config.wifi.last_success_index != last_success_index) {
    needs_rewrite = true;
  } else {
    for (uint8_t index = 0; index < loaded_count; ++index) {
      if (config.wifi.credentials[index].ssid !=
              credentials[index].ssid ||
          config.wifi.credentials[index].password !=
              credentials[index].password) {
        needs_rewrite = true;
        break;
      }
    }
  }
  return true;
}

void load_cloud_config(DeviceConfig &config, bool &needs_rewrite) {
  std::string environment;
  std::string credential;
  const bool has_environment =
      read_config_string(kCloudEnvironmentKey, environment);
  const bool has_credential =
      read_config_string(kCloudCredentialKey, credential);
  if (!has_environment && !has_credential) {
    config.cloud = {};
    return;
  }

  environment = text::trim_copy(environment);
  credential = text::trim_copy(credential);
  if (environment.empty() || credential.empty() ||
      environment.size() > kDeviceCloudEnvironmentMax ||
      credential.size() > kDeviceCloudCredentialMax) {
    config.cloud = {};
    needs_rewrite = true;
    return;
  }
  config.cloud = {std::move(environment), std::move(credential), true};
}

} // 匿名命名空间

std::string derive_local_device_uuid() {
  uint8_t mac[6] = {0};
  if (!lunaredu::lunaros::internal::system_read_factory_mac(mac, sizeof(mac))) {
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

namespace {
DeviceConfig default_device_config() {
  DeviceConfig config;
  config.identity.local_device_uuid = derive_local_device_uuid();
  config.identity.name = build_default_device_name(config.identity.local_device_uuid);
  config.identity.provision_ap_name = default_provision_ap_base_name();
  return config;
}

// Legacy keys are retired only after a complete new record is committed. A
// failed cleanup is retried at the next boot; readers always prefer the record.
void retire_legacy_keys() {
  const char *keys[] = {kSchemaVersionKey, kWifiCountKey, kWifiLastKey,
      kWifiFactoryResetKey, kDeviceUuidKey, kDeviceNameKey, kProvisionApKey,
      kCloudEnvironmentKey, kCloudCredentialKey};
  for (const char *key : keys) (void)nvs_store_erase_key(kDeviceConfigNvsNamespace, key);
  for (uint8_t i = 0; i < kDeviceWifiCredentialMax; ++i) {
    (void)nvs_store_erase_key(kDeviceConfigNvsNamespace, make_indexed_key("ssid", i).c_str());
    (void)nvs_store_erase_key(kDeviceConfigNvsNamespace, make_indexed_key("pass", i).c_str());
  }
}
} // namespace

bool commit_device_config(DeviceConfig config) {
  std::string record;
  if (!nvs_store_ready() || !encode_device_config_record(config, record) ||
      !nvs_store_write_blob(kDeviceConfigNvsNamespace, kDeviceConfigRecordKey, record))
    return false;
  publish_device_config(std::move(config));
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
    LOGW("device_config", "NVS 尚未就绪，当前保持初始化的空网络配置");
    return false;
  }

  std::string record;
  const auto stored = nvs_store_read_blob(kDeviceConfigNvsNamespace,
      kDeviceConfigRecordKey, kDeviceConfigRecordMaxBytes, record);
  if (stored == NvsReadResult::Failed) return false;
  if (stored == NvsReadResult::Found) {
    if (!decode_device_config_record(record, config)) {
      LOGE("device_config", "invalid configuration record; refusing legacy fallback");
      return false;
    }
    const auto uuid = derive_local_device_uuid();
    if (config.identity.local_device_uuid != uuid) {
      config.identity.local_device_uuid = uuid;
      if (!commit_device_config(std::move(config))) return false;
    } else {
      publish_device_config(std::move(config));
    }
    retire_legacy_keys();
    return true;
  }
  // First upgrade only: read schema 3-7 with its original filtering rules,
  // then publish exactly one complete record. Never rewrite old keys.
  legacy_read_failed = false;
  bool needs_rewrite = false;
  std::string schema_version;
  const bool schema_is_current =
      read_config_string(kSchemaVersionKey, schema_version) &&
      schema_version == kSchemaVersion;
  const bool schema_preserves_legacy_pair =
      schema_version == "6" || schema_is_current;
  if (!schema_is_current) {
    // Known keys are intentionally loaded before rewriting the schema marker.
    // A firmware upgrade must never erase Wi-Fi or cloud identity merely
    // because the schema version changed.
    LOGW("device_config", "配置缺失或版本不匹配，正在迁移已知配置");
    needs_rewrite = true;
  }

  load_wifi_credentials(config, needs_rewrite, !schema_is_current,
                        !schema_preserves_legacy_pair);

  const std::string expected_uuid = derive_local_device_uuid();
  std::string stored_uuid;
  if (!read_config_string(kDeviceUuidKey, stored_uuid) ||
      stored_uuid != expected_uuid) {
    needs_rewrite = true;
  }
  config.identity.local_device_uuid = expected_uuid;

  const std::string fallback_device_name =
      build_default_device_name(expected_uuid);
  const std::string fallback_ap_name = default_provision_ap_base_name();
  config.identity.name = read_config_string_or_default(
      kDeviceNameKey, fallback_device_name, kDeviceNameMax, needs_rewrite);
  config.identity.provision_ap_name = read_config_string_or_default(
      kProvisionApKey, fallback_ap_name, kDeviceProvisionApNameMax,
      needs_rewrite);

  load_cloud_config(config, needs_rewrite);

  if (legacy_read_failed || !commit_device_config(config)) return false;
  retire_legacy_keys();

  LOGI("device_config",
       "设备配置已加载：Wi-Fi=%u 组 官网连接=%s 设备ID=%s",
       static_cast<unsigned>(config.wifi.credential_count),
       config.cloud.configured ? "已配置" : "未配置",
       config.identity.local_device_uuid.c_str());
  return true;
}

bool save_device_wifi_last_success_index(uint8_t index) {
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig config = device_config();
  if (index >= config.wifi.credential_count) return false;
  if (config.wifi.last_success_index == index) return true;
  config.wifi.last_success_index = index;
  return commit_device_config(std::move(config));
}

} // namespace lunaredu::lunaros
