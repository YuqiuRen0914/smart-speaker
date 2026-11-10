#include "device_config_record.h"

#include <utility>
#include "lunaros_crc32.h"

namespace lunaredu::lunaros {
namespace {

using digest::crc32;

bool valid_text(const std::string &text, size_t maximum, bool empty = false) {
  return (empty || !text.empty()) && text.size() <= maximum &&
         text.find('\0') == std::string::npos;
}

bool valid_config(const DeviceConfig &config) {
  const auto &wifi = config.wifi;
  if (wifi.credential_count > kDeviceWifiCredentialMax ||
      wifi.configured != (wifi.credential_count != 0) ||
      (wifi.credential_count == 0 ? wifi.last_success_index != 0
                                 : wifi.last_success_index >= wifi.credential_count))
    return false;
  for (size_t i = 0; i < wifi.credential_count; ++i) {
    if (!valid_text(wifi.credentials[i].ssid, kDeviceWifiSsidMax) ||
        !valid_text(wifi.credentials[i].password, kDeviceWifiPasswordMax, true))
      return false;
  }
  if (!valid_text(config.identity.local_device_uuid, 36) ||
      !valid_text(config.identity.name, kDeviceNameMax) ||
      !valid_text(config.identity.provision_ap_name, kDeviceProvisionApNameMax))
    return false;
  const auto &cloud = config.cloud;
  if (!valid_text(cloud.development_base_url, kDeviceCloudBaseUrlMax, true)) return false;
  return cloud.configured
      ? valid_text(cloud.environment, kDeviceCloudEnvironmentMax) &&
            valid_text(cloud.credential, kDeviceCloudCredentialMax)
      : cloud.environment.empty() && cloud.credential.empty() && cloud.development_base_url.empty();
}

void append_text(std::string &record, const std::string &text) {
  record.push_back(static_cast<char>(text.size() & 0xFF));
  record.push_back(static_cast<char>((text.size() >> 8) & 0xFF));
  record.append(text);
}

bool read_text(const std::string &record, size_t end, size_t &offset,
               size_t maximum, std::string &text) {
  if (offset > end || end - offset < 2) return false;
  const size_t size = static_cast<uint8_t>(record[offset]) |
      (static_cast<size_t>(static_cast<uint8_t>(record[offset + 1])) << 8);
  offset += 2;
  if (size > maximum || size > end - offset) return false;
  text.assign(record.data() + offset, size);
  offset += size;
  return true;
}

} // namespace

bool encode_device_config_record(const DeviceConfig &config, std::string &record,
                                 bool legacy_compatible) {
  record.clear();
  if (!valid_config(config)) return false;
  record.reserve(kDeviceConfigRecordMaxBytes);
  const bool legacy = legacy_compatible && config.cloud.development_base_url.empty();
  record = legacy ? "LDC8" : "LDC9";
  record.push_back(static_cast<char>(config.wifi.credential_count));
  record.push_back(static_cast<char>(config.wifi.last_success_index));
  record.push_back(config.cloud.configured ? 1 : 0);
  for (size_t i = 0; i < config.wifi.credential_count; ++i) {
    append_text(record, config.wifi.credentials[i].ssid);
    append_text(record, config.wifi.credentials[i].password);
  }
  append_text(record, config.identity.local_device_uuid);
  append_text(record, config.identity.name);
  append_text(record, config.identity.provision_ap_name);
  append_text(record, config.cloud.environment);
  append_text(record, config.cloud.credential);
  if (!legacy) append_text(record, config.cloud.development_base_url);
  const uint32_t crc = crc32(record.data(), record.size());
  for (unsigned i = 0; i < 4; ++i)
    record.push_back(static_cast<char>((crc >> (i * 8)) & 0xFF));
  return record.size() <= kDeviceConfigRecordMaxBytes;
}

bool decode_device_config_record(const std::string &record, DeviceConfig &config,
                                 bool *migration_required) {
  if (migration_required) *migration_required = false;
  const bool legacy = record.size() >= 4 && record.compare(0, 4, "LDC8") == 0;
  if (record.size() < 11 || record.size() > kDeviceConfigRecordMaxBytes ||
      (!legacy && record.compare(0, 4, "LDC9") != 0)) return false;
  const size_t end = record.size() - 4;
  uint32_t stored_crc = 0;
  for (unsigned i = 0; i < 4; ++i)
    stored_crc |= static_cast<uint32_t>(static_cast<uint8_t>(record[end + i])) << (i * 8);
  if (crc32(record.data(), end) != stored_crc) return false;
  DeviceConfig next;
  next.wifi.credential_count = static_cast<uint8_t>(record[4]);
  next.wifi.last_success_index = static_cast<uint8_t>(record[5]);
  next.wifi.configured = next.wifi.credential_count != 0;
  if (next.wifi.credential_count > kDeviceWifiCredentialMax ||
      static_cast<uint8_t>(record[6]) > 1) return false;
  next.cloud.configured = record[6] != 0;
  size_t offset = 7;
  for (size_t i = 0; i < next.wifi.credential_count; ++i) {
    if (!read_text(record, end, offset, kDeviceWifiSsidMax, next.wifi.credentials[i].ssid) ||
        !read_text(record, end, offset, kDeviceWifiPasswordMax, next.wifi.credentials[i].password))
      return false;
  }
  if (!read_text(record, end, offset, 36, next.identity.local_device_uuid) ||
      !read_text(record, end, offset, kDeviceNameMax, next.identity.name) ||
      !read_text(record, end, offset, kDeviceProvisionApNameMax, next.identity.provision_ap_name) ||
      !read_text(record, end, offset, kDeviceCloudEnvironmentMax, next.cloud.environment) ||
      !read_text(record, end, offset, kDeviceCloudCredentialMax, next.cloud.credential)) return false;
  if (!legacy && !read_text(record, end, offset, kDeviceCloudBaseUrlMax,
                 next.cloud.development_base_url)) return false;
  if (offset != end || !valid_config(next)) return false;
  config = std::move(next);
  if (migration_required) *migration_required = legacy;
  return true;
}

} // namespace lunaredu::lunaros
