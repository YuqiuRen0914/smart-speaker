#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "configuration/device_config.h"

#include "lunaros_network.h"
#include "lunaros_network_policy.h"
using lunaredu::lunaros::NetworkWifiPowerSaveLevel;

struct WifiRuntimeSnapshot {
  lunaredu::lunaros::NetworkWifiEventMetrics wifi_events;
  uint32_t configuration_generation = 0;
  bool connected = false;
  bool credentials_available = false;
  bool apply_pending = false;
  uint32_t reconnect_attempts = 0;
  uint32_t disconnect_started_at_ms = 0;
  uint32_t next_reconnect_delay_ms = 0;
  int status_code = 0;
  int last_disconnect_reason = 0;
  int32_t rssi = 0;
  uint8_t channel = 0;
  bool station_suspended = false;
  bool softap_active = false;
  std::string local_ip;
  std::string target_ssid;
};

struct WifiScanApRecord {
  std::string ssid;
  int32_t rssi = 0;
  uint8_t authmode = 0;
  bool secure = false;
  uint8_t channel = 0;
  std::array<uint8_t, 6> bssid{};
};

bool setup_wifi(bool station_enabled = true, bool keep_sta_radio = false,
                uint8_t radio_channel = 1, bool default_fallback_enabled = true);
bool wifi_set_radio_channel(uint8_t channel, std::string &error);
int wifi_observed_channel();
bool stop_wifi(std::string &error);
void tick_wifi();
bool wifi_connected();
WifiRuntimeSnapshot wifi_runtime_snapshot();
void wifi_runtime_snapshot_into(WifiRuntimeSnapshot &output);
// Physical owner observation: read once under the driver lock and write into
// reusable Network-owned storage, without an intermediate text snapshot.
void wifi_observe_network(lunaredu::lunaros::NetworkPhysicalState &output,
                          bool local_access_active);
bool wifi_queue_credentials_apply(
    const lunaredu::lunaros::DeviceWifiCredential *credentials,
                                  size_t credential_count,
                                  uint8_t preferred_index, bool temporary=false);
void wifi_set_station_suspended(bool suspended);
bool wifi_station_suspended();
void wifi_disconnect_station();
int32_t wifi_rssi();
int wifi_channel();
bool wifi_set_power_save_level(NetworkWifiPowerSaveLevel level);
bool wifi_configure_softap_network(uint32_t ip, uint32_t gateway,
                                   uint32_t subnet);
bool wifi_start_softap(const char *ssid, const char *password);
bool wifi_stop_softap();
bool wifi_scan_access_points(std::vector<WifiScanApRecord> &records,
                             std::string &error);
bool wifi_survey_access_points(std::vector<WifiScanApRecord> &records,
                              uint16_t &total_count, std::string &error);
