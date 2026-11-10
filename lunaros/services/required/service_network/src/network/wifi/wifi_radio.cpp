#include "network/wifi/wifi_manager_internal.h"
#include "network/wifi/wifi_radio_policy.h"
#include "network/wifi/wifi_analysis_internal.h"
#include <esp_err.h>
#include <esp_timer.h>

int wifi_channel() {
  // ESP-NOW and SoftAP have a channel even without a STA association.
  uint8_t primary = 0;
  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  return esp_wifi_get_channel(&primary, &secondary) == ESP_OK
             ? static_cast<int>(primary) : 0;
}

int wifi_observed_channel() {
  struct Facts {
    uint32_t configuration,events;uint8_t requested;
    bool ready,station,softap,connecting;
    bool operator==(const Facts &b)const{return configuration==b.configuration&&events==b.events&&requested==b.requested&&ready==b.ready&&station==b.station&&softap==b.softap&&connecting==b.connecting;}
  };
  static Facts observed{};static bool have=false;static int channel=0;static int64_t sampled=0;
  auto &state=wifi_driver_state();Facts facts;
  {std::lock_guard<std::recursive_mutex> lock(state.mutex);
    facts={state.configuration_generation,state.wifi_events.processed,state.radio_channel,
      state.stack_ready,state.station_connected,state.softap_active,state.status==kWifiStatusIdle};}
  const auto now=esp_timer_get_time();
  if(!have||!(facts==observed)||facts.connecting||lunaredu::lunaros::internal::wifi_monitor_active()||now-sampled>=250'000){
    channel=wifi_channel();observed=facts;sampled=now;have=true;
  }
  return channel;
}

std::recursive_mutex &wifi_radio_mutex() {
  static std::recursive_mutex mutex;
  return mutex;
}

bool wifi_set_radio_channel(uint8_t channel, std::string &error) {
  using namespace lunaredu::lunaros;
using namespace lunaredu::lunaros::internal;
  std::unique_lock<std::recursive_mutex> radio_lock(wifi_radio_mutex(), std::try_to_lock);
  if (!radio_lock.owns_lock()) {
    error = "wifi_radio_busy";
    return false;
  }
  if (lunaredu::lunaros::internal::wifi_monitor_active() && wifi_channel() != channel) {
    error = "wifi_radio_busy"; return false;
  }
  auto &state = wifi_driver_state();
  bool ready, connected, connecting, softap;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    ready = state.stack_ready && state.sta_started;
    connected = state.station_connected;
    connecting = state.station_enabled && state.credential_count > 0 && !state.station_suspended &&
        state.status == kWifiStatusIdle && !connected;
    softap = state.softap_active;
  }
  // The radio lease serializes mutation; readers never wait for SDK calls.
  uint8_t current = 0;
  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  const bool started = ready &&
      esp_wifi_get_channel(&current, &secondary) == ESP_OK;
  wifi_ap_record_t associated_ap = {};
  const bool associated = connected ||
      esp_wifi_sta_get_ap_info(&associated_ap) == ESP_OK;
  const auto decision = decide_radio_channel(started, false,
      connecting, associated, softap, current, channel);
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
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.radio_channel = channel;
  }
  return true;
}
