#pragma once

#include <functional>
#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "lunaredu_lunaros/persistence/settings.h"

#include "lunaredu_lunaros/network/network.h"
using lunaredu::lunaros::NetworkWifiPowerSaveLevel;

struct WifiRuntimeSnapshot {
  bool connected = false;
  bool credentials_available = false;
  bool apply_pending = false;
  bool station_connected = false;
  bool has_connected_once = false;
  unsigned long connection_started_at_ms = 0;
  uint32_t last_reconnect_ms = 0;
  uint32_t last_status_log_ms = 0;
  uint32_t reconnect_attempts = 0;
  uint32_t disconnect_started_at_ms = 0;
  uint32_t next_reconnect_delay_ms = 0;
  int status_code = 0;
  int last_disconnect_reason = 0;
  int32_t rssi = 0;
  bool station_suspended = false;
  bool softap_active = false;
  std::string local_ip;
  std::string target_ssid;
  std::string softap_ip;
};

struct WifiScanApRecord {
  std::string ssid;
  int32_t rssi = 0;
  uint8_t authmode = 0;
  bool secure = false;
  uint8_t channel = 0;
};

bool setup_wifi(bool station_enabled = true, bool keep_sta_radio = false,
                uint8_t radio_channel = 1);
bool wifi_set_radio_channel(uint8_t channel, std::string &error);
bool stop_wifi(std::string &error);
void tick_wifi();
bool wifi_connected();
WifiRuntimeSnapshot wifi_runtime_snapshot();
bool wifi_queue_credentials_apply(
    const lunaredu::lunaros::DeviceWifiCredential *credentials,
                                  size_t credential_count,
                                  uint8_t preferred_index);
void wifi_set_station_suspended(bool suspended);
bool wifi_station_suspended();
void wifi_disconnect_station();
int32_t wifi_rssi();
int wifi_channel();
bool wifi_set_power_save_level(NetworkWifiPowerSaveLevel level);
bool wifi_configure_softap_network(uint32_t ip, uint32_t gateway,
                                   uint32_t subnet);
bool wifi_start_softap(const char *ssid, const char *password);
void wifi_stop_softap();
bool wifi_scan_access_points(std::vector<WifiScanApRecord> &records,
                             std::string &error);
