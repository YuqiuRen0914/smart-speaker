#include "lunaros_network.h"
#include "lunaros_network_espnow.h"
#include "wifi_analysis_internal.h"
#include "wifi_manager_internal.h"
#include "wifi_manager.h"
#include "network/bluetooth/bluetooth_lifecycle.h"
#include "kernel/runtime.h"
#include "kernel/service_executor.h"
#include "network/network_service_internal.h"
#include "platform/system.h"
#include <atomic>
#include <esp_err.h>

namespace lunaredu::lunaros {
namespace {
std::atomic<bool> receiving{false};
std::atomic<uint32_t> management{0}, control{0}, data{0};
StaticMutex snapshot_mutex;
NetworkWifiActivitySnapshot snapshot;
uint32_t generation = 0, renewed_at = 0, sampled_at = 0;
uint8_t original_channel = 0;
wifi_second_chan_t original_secondary = WIFI_SECOND_CHAN_NONE;
wifi_ps_type_t original_power_save = WIFI_PS_NONE;
bool restore_radio_settings = false;
wifi_promiscuous_filter_t original_filter{};
wifi_promiscuous_filter_t original_control_filter{};
std::atomic<bool> configured{false};
NetworkWifiMonitorClient client{};
enum class StopStage { Client, Promiscuous, Callback, Filter, ControlFilter, Channel, Power };
StopStage stop_stage = StopStage::Client;
bool exclusive_busy() {
  const auto espnow = espnow_status();
  const bool radio_occupied = espnow.initialized || espnow.stopping || bluetooth_occupied();
  auto &state = wifi_driver_state();
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  return state.station_connected || (!state.station_suspended && state.credential_count) ||
      radio_occupied;
}

// Wi-Fi callback only updates counters: no allocation, parsing, UI, logging or
// packet retention. ESP32 reports CRC/error and non-frame callbacks as MISC.
void receive(void *, wifi_promiscuous_pkt_type_t type) {
  if (!receiving.load(std::memory_order_acquire)) return;
  if (type == WIFI_PKT_MGMT) ++management;
  else if (type == WIFI_PKT_CTRL) ++control;
  else if (type == WIFI_PKT_DATA) ++data;
}
void clear_counters() { management = 0; control = 0; data = 0; }
enum class Operation { Start, Renew, Stop };
struct Request {
  KernelRuntime *runtime;
  Operation operation;
  uint8_t channel;
  NetworkWifiMonitorHandle handle;
  NetworkWifiMonitorClient client{};
};
void execute(void *p, ServiceOperationResult &result) {
  auto &request = *static_cast<Request *>(p);
  if (!require_network_runtime(*request.runtime, result.error)) { result.ok = false; return; }
  if (request.operation == Operation::Start)
    result.ok = internal::wifi_monitor_start(request.channel, request.client, request.handle, result.error);
  else if (request.operation == Operation::Renew)
    result.ok = internal::wifi_monitor_renew(request.handle, result.error);
  else result.ok = internal::wifi_monitor_stop(request.handle, result.error);
}
bool invoke(KernelRuntime &runtime, Request &request, std::string &error) {
  if (!require_network_runtime(runtime, error)) return false;
  return runtime.invoke_executor(kNetworkServiceExecutor, execute, &request, error);
}
}
namespace internal {
bool wifi_monitor_active() { return configured.load(std::memory_order_acquire); }
bool wifi_monitor_release_exclusive(std::string &error) {
  error.clear(); return !configured || !client.exclusive || wifi_monitor_shutdown(error);
}

bool wifi_monitor_shutdown(std::string &error) {
  error.clear();
  if (!configured) return true;
  std::unique_lock<std::recursive_mutex> radio_lock(wifi_radio_mutex(), std::try_to_lock);
  if (!radio_lock.owns_lock()) { error = "wifi_radio_busy"; return false; }
  receiving = false;
  // A successful stage is not replayed if a later SDK operation fails.
  // Ownership and client context stay alive until all stages acknowledge.
  while (true) {
    esp_err_t result = ESP_OK;
    switch (stop_stage) {
    case StopStage::Client:
      if (client.stop && !client.stop(client.context, error)) return false;
      break;
    case StopStage::Promiscuous: result = esp_wifi_set_promiscuous(false); break;
    case StopStage::Callback: result = esp_wifi_set_promiscuous_rx_cb(nullptr); break;
    case StopStage::Filter: result = esp_wifi_set_promiscuous_filter(&original_filter); break;
    case StopStage::ControlFilter: result = esp_wifi_set_promiscuous_ctrl_filter(&original_control_filter); break;
    case StopStage::Channel:
      if (restore_radio_settings) result = esp_wifi_set_channel(original_channel, original_secondary);
      break;
    case StopStage::Power:
      if (restore_radio_settings) result = esp_wifi_set_ps(original_power_save);
      break;
    }
    if (result != ESP_OK) { error = esp_err_to_name(result); return false; }
    if (stop_stage == StopStage::Power) break;
    stop_stage = static_cast<StopStage>(static_cast<unsigned>(stop_stage) + 1);
  }
  restore_radio_settings = false;
  // Do not retune a newly associated radio. The ordinary network owner now
  // determines its channel; restoring an old idle channel is best effort.
  configured = false;
  client = {};
  std::string ignored;
  if (original_channel) (void)wifi_set_radio_channel(original_channel, ignored);
  StaticLockGuard lock(snapshot_mutex); snapshot.active = false; ++snapshot.sequence;
  return true;
}
bool wifi_monitor_start(uint8_t channel, NetworkWifiMonitorHandle &handle, std::string &error) {
  return wifi_monitor_start(channel, {}, handle, error);
}
bool wifi_monitor_start(uint8_t channel, const NetworkWifiMonitorClient &requested_client,
    NetworkWifiMonitorHandle &handle, std::string &error) {
  error.clear(); handle = {};
  if ((requested_client.prepare || requested_client.poll) && !requested_client.stop) {
    error = "wifi_monitor_client_invalid"; return false;
  }
  if (!admit_network_work(error)) return false;
  if (configured) { error = "wifi_monitor_busy"; return false; }
  if (!network_fast_snapshot().service_started) { error = "network_service_not_started"; return false; }
  std::unique_lock<std::recursive_mutex> radio_lock(wifi_radio_mutex(), std::try_to_lock);
  if (!radio_lock.owns_lock()) { error = "wifi_radio_busy"; return false; }
  if (requested_client.exclusive && exclusive_busy()) { error = "wifi_radio_in_use"; return false; }
  bool enabled = false;
  auto result = esp_wifi_get_promiscuous(&enabled);
  if (result != ESP_OK || enabled) { error = "wifi_monitor_unavailable"; return false; }
  auto &state = wifi_driver_state();
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    if (!state.sta_started || state.softap_active || state.pending_apply ||
        (!state.station_suspended && state.credential_count &&
         !state.station_connected && state.status == kWifiStatusIdle)) {
      error = "wifi_radio_busy"; return false;
    }
  }
  result=esp_wifi_get_channel(&original_channel,&original_secondary);
  if(result!=ESP_OK){error=esp_err_to_name(result);return false;}
  if(requested_client.exclusive){
    result=esp_wifi_get_ps(&original_power_save);
    if(result!=ESP_OK){error=esp_err_to_name(result);return false;}
  }
  if (channel == 0) channel = original_channel;
  wifi_country_t country{};
  if (esp_wifi_get_country(&country) != ESP_OK || channel < country.schan ||
      channel >= unsigned(country.schan) + country.nchan || channel > 14) {
    error = "wifi_monitor_channel_unavailable"; return false;
  }
  // A product's ESP-NOW channel belongs to its transport, even while idle.
  const auto espnow = espnow_status();
  if (channel != original_channel && (espnow.initialized || espnow.stopping)) {
    error = "wifi_radio_channel_in_use"; return false;
  }
  result = esp_wifi_get_promiscuous_filter(&original_filter);
  if (result != ESP_OK) { error = esp_err_to_name(result); return false; }
  result = esp_wifi_get_promiscuous_ctrl_filter(&original_control_filter);
  if (result != ESP_OK) { error = esp_err_to_name(result); return false; }
  if (!wifi_set_radio_channel(channel, error)) return false;
  if (++generation == 0) ++generation;
  handle.value = generation;
  configured = true;
  stop_stage = StopStage::Client;
  client = requested_client;
  restore_radio_settings=client.exclusive;
  if(client.exclusive)result=esp_wifi_set_ps(WIFI_PS_NONE);
  wifi_promiscuous_filter_t filter{};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_CTRL | WIFI_PROMIS_FILTER_MASK_DATA;
  if(result==ESP_OK)result = esp_wifi_set_promiscuous_filter(&filter);
  wifi_promiscuous_filter_t controls{};
  controls.filter_mask = WIFI_PROMIS_CTRL_FILTER_MASK_ALL;
  if (result == ESP_OK) result = esp_wifi_set_promiscuous_ctrl_filter(&controls);
  if (result == ESP_OK) result = esp_wifi_set_promiscuous_rx_cb(receive);
  clear_counters();
  if (result == ESP_OK) result = esp_wifi_set_promiscuous(true);
  const bool prepared = result == ESP_OK && (!client.prepare || client.prepare(client.context, channel, error));
  if (!prepared) {
    if (result != ESP_OK) error = esp_err_to_name(result);
    std::string ignored;
    if (wifi_monitor_shutdown(ignored)) handle = {};
    return false;
  }
  renewed_at = sampled_at = system_monotonic_ms();
  {
    StaticLockGuard lock(snapshot_mutex);
    snapshot = {}; snapshot.session = generation; snapshot.channel = channel;
    snapshot.active = true; ++snapshot.sequence;
  }
  receiving.store(true, std::memory_order_release);
  return true;
}
bool wifi_monitor_renew(NetworkWifiMonitorHandle handle, std::string &error) {
  if (!configured || !receiving || !handle || handle.value != generation) {
    error = "wifi_monitor_session_expired"; return false;
  }
  renewed_at = system_monotonic_ms(); error.clear(); return true;
}
bool wifi_monitor_stop(NetworkWifiMonitorHandle handle, std::string &error) {
  error.clear();
  if (!handle || !configured) return true;
  if (handle.value != generation) { error = "wifi_monitor_session_mismatch"; return false; }
  return wifi_monitor_shutdown(error);
}
void wifi_monitor_poll(uint32_t now) {
  if (!configured) return;
  bool abort = !receiving || now - renewed_at >= 5000;
  abort = abort || (client.exclusive && exclusive_busy());
  {
    auto &state = wifi_driver_state(); std::lock_guard<std::recursive_mutex> lock(state.mutex);
    abort = abort || !state.sta_started || state.softap_active || state.pending_apply ||
        (!state.station_suspended && state.credential_count && !state.station_connected);
  }
  if (abort) { std::string ignored; (void)wifi_monitor_shutdown(ignored); return; }
  const auto channel = static_cast<uint8_t>(wifi_channel());
  if (client.poll) client.poll(client.context, channel);
  StaticLockGuard lock(snapshot_mutex);
  if (channel != snapshot.channel) {
    clear_counters(); sampled_at = now; snapshot.channel = channel;
    snapshot.elapsed_ms = 0; snapshot.management = snapshot.control = snapshot.data = 0;
    ++snapshot.sequence; return;
  }
  const uint32_t elapsed = now - sampled_at;
  if (elapsed < 1000) return;
  snapshot.elapsed_ms = elapsed;
  snapshot.management = management.exchange(0); snapshot.control = control.exchange(0); snapshot.data = data.exchange(0);
  sampled_at = now; ++snapshot.sequence;
}
}
bool request_network_wifi_monitor_start(KernelRuntime &runtime, uint8_t channel,
    NetworkWifiMonitorHandle &handle, std::string &error) {
  return request_network_wifi_monitor_start(runtime, channel, {}, handle, error);
}
bool request_network_wifi_monitor_start(KernelRuntime &runtime, uint8_t channel,
    const NetworkWifiMonitorClient &client, NetworkWifiMonitorHandle &handle, std::string &error) {
  Request request{&runtime, Operation::Start, channel, {}, client};
  const bool ok = invoke(runtime, request, error); handle = request.handle; return ok;
}
bool request_network_wifi_monitor_renew(KernelRuntime &runtime, NetworkWifiMonitorHandle handle, std::string &error) {
  Request request{&runtime, Operation::Renew, 0, handle}; return invoke(runtime, request, error);
}
bool request_network_wifi_monitor_stop(KernelRuntime &runtime, NetworkWifiMonitorHandle handle, std::string &error) {
  Request request{&runtime, Operation::Stop, 0, handle}; return invoke(runtime, request, error);
}
void network_wifi_activity_snapshot_into(NetworkWifiActivitySnapshot &output) {
  StaticLockGuard lock(snapshot_mutex); output = snapshot;
}
} // namespace lunaredu::lunaros
