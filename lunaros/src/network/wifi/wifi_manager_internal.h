#pragma once

#include <mutex>
#include <string>
#include <esp_wifi.h>
#include "lunaredu_lunaros/persistence/settings.h"

using lunaredu::lunaros::DeviceWifiCredential;
using lunaredu::lunaros::kDeviceWifiCredentialMax;

inline constexpr int kWifiStatusIdle = 0;
inline constexpr int kWifiStatusConnected = 3;
inline constexpr int kWifiStatusDisconnected = 6;

// ESP-IDF callbacks and the LunarOS network loop share one driver state. Keep
// the storage private to this Wi-Fi backend instead of exporting independently
// mutable globals across the Wi-Fi implementation files.
struct WifiDriverState {
  std::recursive_mutex mutex;

  DeviceWifiCredential credentials[kDeviceWifiCredentialMax];
  DeviceWifiCredential pending_credentials[kDeviceWifiCredentialMax];
  std::string ssid;
  std::string password;
  std::string local_ip;
  std::string softap_ip;

  uint8_t credential_count = 0;
  uint8_t current_credential_index = 0;
  uint8_t preferred_credential_index = 0;
  uint8_t pending_credential_count = 0;
  uint8_t pending_preferred_index = 0;

  uint32_t last_reconnect_ms = 0;
  uint32_t last_status_log_ms = 0;
  uint32_t last_rssi_sample_ms = 0;
  uint32_t first_connect_ms = 0;
  uint32_t disconnect_started_ms = 0;
  uint32_t reconnect_attempts = 0;
  uint32_t station_connected_since_ms = 0;
  uint32_t last_warned_disconnect_ms = 0;

  int status = kWifiStatusDisconnected;
  int last_disconnect_reason = 0;
  int last_warned_disconnect_reason = 0;
  int32_t last_rssi = 0;
  wifi_ps_type_t power_save_type = WIFI_PS_MIN_MODEM;

  bool retry_scan_started = false;
  bool current_candidate_tried = false;
  bool was_connected = false;
  bool station_connected = false;
  bool has_connected_once = false;
  bool station_suspended = false;
  bool station_enabled = true;
  bool keep_sta_radio = false;
  uint8_t radio_channel = 1;
  bool stack_ready = false;
  bool sta_started = false;
  bool softap_active = false;
  bool pending_apply = false;
};

WifiDriverState &wifi_driver_state();
// Serializes explicit retunes against scans running on the netio worker.
std::recursive_mutex &wifi_radio_mutex();

bool copy_wifi_credentials(const DeviceWifiCredential *source,
                           size_t source_count, DeviceWifiCredential *target,
                           uint8_t &target_count);
uint8_t clamp_wifi_credential_index(uint8_t index, uint8_t count);
void set_active_wifi_credential(uint8_t index);
void reset_wifi_attempt_sequence(uint8_t preferred_index);
void load_wifi_credentials_from_runtime();
bool queue_wifi_credentials_apply(const DeviceWifiCredential *credentials,
                                  size_t credential_count,
                                  uint8_t preferred_index);
void advance_wifi_candidate();
void handle_wifi_connected_once();
bool wifi_driver_ensure_stack_ready();
bool wifi_driver_register_event_handlers();
void wifi_driver_unregister_event_handlers();
void wifi_driver_clear_event_queue();
bool wifi_driver_shutdown_stack(std::string &error);
void wifi_driver_drain_events();
bool wifi_driver_apply_power_save_setting();
void wifi_driver_begin_connection();
uint32_t wifi_driver_reconnect_delay(uint32_t attempts);
void wifi_driver_update_rssi_snapshot();
bool wifi_driver_connected_locked();
void wifi_driver_reset_reconnect_window_locked();
uint32_t wifi_driver_ensure_reconnect_window_started_locked(uint32_t now_ms);
void wifi_driver_apply_pending_config();
void wifi_driver_log_status();
