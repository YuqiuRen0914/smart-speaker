#pragma once

#include <cstdint>
#include <string>

#include "lunaredu_lunaros/network/network.h"

namespace lunaredu::lunaros {

struct ProvisioningWifiSnapshot {
  bool connected = false;
  bool station_suspended = false;
  bool softap_active = false;
  int status_code = 0;
  int last_disconnect_reason = 0;
  uint32_t reconnect_attempts = 0;
  uint32_t disconnect_started_at_ms = 0;
  uint32_t next_reconnect_delay_ms = 0;
  int32_t rssi = 0;
  std::string local_ip;
  std::string target_ssid;
  std::string softap_ip;
};

enum class NetworkProvisioningCredentialResult {
  Ok,
  MissingSsid,
  NotReady,
  SaveFailed,
  ApplyFailed,
};

const char *network_provisioning_mode_name(
    lunaredu::lunaros::NetworkProvisioningMode mode);
bool network_provisioning_softap_active();
NetworkProvisioningCredentialResult
submit_network_provisioning_wifi_credentials(const char *ssid,
                                             const char *password);
ProvisioningWifiSnapshot provisioning_driver_wifi_snapshot();
int32_t provisioning_driver_wifi_rssi();
int provisioning_driver_wifi_channel();
uint32_t schedule_network_provisioning_restart();

} // namespace lunaredu::lunaros
