#include "network/wifi/wifi_credential_list.h"


namespace lunaredu::lunaros {

namespace {

void append_unique_wifi_credential(DeviceWifiCredential *credentials,
                                   size_t &credential_count,
                                   size_t credential_capacity,
                                   const DeviceWifiCredential &credential) {
  const std::string &ssid = credential.ssid;
  if (ssid.empty() || credential_count >= credential_capacity) {
    return;
  }

  for (size_t index = 0; index < credential_count; ++index) {
    if (credentials[index].ssid == ssid) {
      return;
    }
  }

  credentials[credential_count].ssid = ssid;
  credentials[credential_count].password = credential.password;
  ++credential_count;
}

} // namespace

size_t build_wifi_credential_list(
    const DeviceWifiCredential &submitted,
    const DeviceWifiCredential *saved_credentials, size_t saved_count,
    DeviceWifiCredential *output, size_t output_capacity) {
  if (output == nullptr || output_capacity == 0) {
    return 0;
  }

  size_t output_count = 0;
  append_unique_wifi_credential(output, output_count, output_capacity,
                                submitted);
  if (output_count == 0 || saved_credentials == nullptr) {
    return output_count;
  }

  for (size_t index = 0; index < saved_count; ++index) {
    append_unique_wifi_credential(output, output_count, output_capacity,
                                  saved_credentials[index]);
  }
  return output_count;
}

} // namespace lunaredu::lunaros
