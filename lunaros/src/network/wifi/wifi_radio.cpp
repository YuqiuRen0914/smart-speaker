#include "network/wifi/wifi_manager_internal.h"
#include "network/wifi/wifi_radio_policy.h"
#include <esp_err.h>

int wifi_channel() {
  // ESP-NOW and SoftAP have a channel even without a STA association.
  uint8_t primary = 0;
  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  return esp_wifi_get_channel(&primary, &secondary) == ESP_OK
             ? static_cast<int>(primary) : 0;
}

std::recursive_mutex &wifi_radio_mutex() {
  static std::recursive_mutex mutex;
  return mutex;
}

bool wifi_set_radio_channel(uint8_t channel, std::string &error) {
  using namespace lunaredu::lunaros::internal;
  std::unique_lock<std::recursive_mutex> radio_lock(wifi_radio_mutex(), std::try_to_lock);
  if (!radio_lock.owns_lock()) {
    error = "wifi_radio_busy";
    return false;
  }
  auto &state = wifi_driver_state();
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  uint8_t current = 0;
  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  const bool started = state.stack_ready && state.sta_started &&
      esp_wifi_get_channel(&current, &secondary) == ESP_OK;
  wifi_ap_record_t associated_ap = {};
  const bool associated = state.station_connected ||
      esp_wifi_sta_get_ap_info(&associated_ap) == ESP_OK;
  const auto decision = decide_radio_channel(started, false,
      state.station_enabled && state.credential_count > 0 && !state.station_suspended &&
          state.status == kWifiStatusIdle && !state.station_connected,
      associated, state.softap_active, current, channel);
  error.clear();
  switch (decision) {
  case RadioChannelDecision::Unavailable: error = "wifi_radio_unavailable"; return false;
  case RadioChannelDecision::Busy: error = "wifi_radio_busy"; return false;
  case RadioChannelDecision::Conflict: error = "wifi_radio_channel_in_use"; return false;
  case RadioChannelDecision::Keep: break;
  case RadioChannelDecision::Retune: {
    const auto result = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (result != ESP_OK) { error = esp_err_to_name(result); return false; }
    break;
  }
  }
  state.radio_channel = channel;
  return true;
}
