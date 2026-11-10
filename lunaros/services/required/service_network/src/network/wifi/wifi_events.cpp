#include "lunaros_network.h"
#include "wifi_manager_internal.h"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>

#include <esp_log.h>
#include "network/wifi/wifi_manager.h"
#include "wifi_reason.h"

namespace {

constexpr const char *kTag = "wifi_events";
constexpr uint32_t kDisconnectWarnRepeatIntervalMs = 60000;
constexpr size_t kEventQueueCapacity = lunaredu::lunaros::kNetworkWifiEventQueueCapacity;
void increment(uint32_t &counter, size_t count = 1) {
  counter += std::min<uint32_t>(UINT32_MAX - counter, static_cast<uint32_t>(count));
}

WifiDriverState &state = wifi_driver_state();
bool wifi_handler_registered = false, ip_handler_registered = false;

enum class WifiRuntimeEventKind {
  kStaStarted,
  kStaConnected,
  kStaDisconnected,
  kApStarted,
  kApStopped,
  kStaGotIp,
  kHomeChannelChanged,
};

struct WifiRuntimeEvent {
  WifiRuntimeEventKind kind = WifiRuntimeEventKind::kStaDisconnected;
  bool has_disconnect_reason = false;
  int disconnect_reason = 0;
  char local_ip[20] = {};
  uint32_t generation = 0;
};

struct WifiEventQueue {
  WifiRuntimeEvent events[kEventQueueCapacity];
  size_t head = 0;
  size_t count = 0;
};

WifiEventQueue &event_queue() {
  static WifiEventQueue queue;
  return queue;
}

void write_ip_info_string(char *buffer, size_t buffer_size,
                          const esp_netif_ip_info_t &ip_info) {
  std::snprintf(buffer, buffer_size, IPSTR, IP2STR(&ip_info.ip));
}

std::string read_netif_ip(const char *ifkey) {
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey(ifkey);
  if (netif == nullptr) {
    return {};
  }

  esp_netif_ip_info_t ip_info = {};
  if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
    return {};
  }

  char buffer[20] = {};
  write_ip_info_string(buffer, sizeof(buffer), ip_info);
  return buffer;
}

void enqueue_event(const WifiRuntimeEvent &event) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  WifiEventQueue &queue = event_queue();
  if (queue.count == kEventQueueCapacity) {
    increment(state.wifi_events.overflow_dropped);
    queue.head = (queue.head + 1) % kEventQueueCapacity;
    --queue.count;
  }

  const size_t tail = (queue.head + queue.count) % kEventQueueCapacity;
  queue.events[tail] = event;
  queue.events[tail].generation = state.configuration_generation;
  ++queue.count;
  increment(state.wifi_events.enqueued);
  state.wifi_events.queued = static_cast<uint32_t>(queue.count);
  state.wifi_events.high_water = std::max(state.wifi_events.high_water, state.wifi_events.queued);
}

bool pop_event(WifiRuntimeEvent &event) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  WifiEventQueue &queue = event_queue();
  if (queue.count == 0) {
    return false;
  }

  event = queue.events[queue.head];
  queue.head = (queue.head + 1) % kEventQueueCapacity;
  --queue.count;
  increment(state.wifi_events.processed);
  state.wifi_events.queued = static_cast<uint32_t>(queue.count);
  return true;
}

void apply_event(const WifiRuntimeEvent &event) {
  const bool positive_station_event = event.kind == WifiRuntimeEventKind::kStaConnected ||
                                      event.kind == WifiRuntimeEventKind::kStaGotIp;
  std::string observed_ip, target;
  if (positive_station_event) {
    {
      std::lock_guard<std::recursive_mutex> lock(state.mutex);
      if (!state.stack_ready || event.generation != state.configuration_generation ||
          state.pending_apply || state.pending_disconnect || state.station_suspended ||
          !state.station_enabled) return;
      target = state.ssid;
    }
    // Callback receipt time cannot identify the attempt that produced it.
    // Confirm the current association on the owner, outside the state lock.
    wifi_ap_record_t current{};
    if (target.empty() || target.size() > sizeof(current.ssid) ||
        esp_wifi_sta_get_ap_info(&current) != ESP_OK ||
        strnlen(reinterpret_cast<const char *>(current.ssid), sizeof(current.ssid)) != target.size() ||
        std::memcmp(current.ssid, target.data(), target.size()) != 0) return;
    if (event.kind == WifiRuntimeEventKind::kStaGotIp) {
      observed_ip = read_netif_ip("WIFI_STA_DEF");
      if (observed_ip.empty() || observed_ip == "0.0.0.0" ||
          (event.local_ip[0] && observed_ip != event.local_ip)) return;
    }
  }
  bool should_refresh_softap_ip = false;
  bool should_update_rssi = false;
  bool should_log_connected = false;
  bool should_log_disconnect = false;
  bool should_warn_disconnect = false;
  int disconnect_reason = 0;
  std::string ssid;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    if (!state.stack_ready) return;
    if (positive_station_event && (state.ssid != target || state.pending_apply ||
        state.pending_disconnect || state.station_suspended || !state.station_enabled)) return;
    if (event.kind != WifiRuntimeEventKind::kApStarted &&
        event.kind != WifiRuntimeEventKind::kApStopped &&
        event.generation != state.configuration_generation) return;
    switch (event.kind) {
    case WifiRuntimeEventKind::kHomeChannelChanged:
      break; // Invalidate owner observation; SDK channel is read after event drain.
    case WifiRuntimeEventKind::kStaStarted:
      state.sta_started = true;
      break;
    case WifiRuntimeEventKind::kStaConnected:
      if (!state.station_connected) {
        state.station_connected_since_ms =
            static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
      }
      state.station_connected = true;
      break;
    case WifiRuntimeEventKind::kStaDisconnected:
      if (state.status == kWifiStatusConnected) {
        const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        // An established link can outlive the signed retry-clock horizon.
        // Make its expired first retry eligible in the new disconnect window.
        if (static_cast<int32_t>(now_ms - state.last_reconnect_ms) < 0)
          state.last_reconnect_ms = now_ms - wifi_driver_reconnect_delay(0);
      }
      state.pending_disconnect = false;
      state.status = kWifiStatusDisconnected;
      state.station_connected = false;
      state.station_connected_since_ms = 0;
      state.local_ip.clear();
      if (state.disconnect_started_ms == 0) {
        state.disconnect_started_ms =
            static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
      }
      if (event.has_disconnect_reason) {
        state.last_disconnect_reason = event.disconnect_reason;
        disconnect_reason = event.disconnect_reason;
        ssid = state.ssid;
        should_log_disconnect = true;
        const uint32_t now_ms =
            static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        should_warn_disconnect =
            state.last_warned_disconnect_reason != disconnect_reason ||
            now_ms - state.last_warned_disconnect_ms >=
                kDisconnectWarnRepeatIntervalMs;
        if (should_warn_disconnect) {
          state.last_warned_disconnect_reason = disconnect_reason;
          state.last_warned_disconnect_ms = now_ms;
        }
      }
      break;
    case WifiRuntimeEventKind::kApStarted:
      state.softap_active = true;
      should_refresh_softap_ip = true;
      ESP_LOGD(kTag, "SoftAP 接口已启动");
      break;
    case WifiRuntimeEventKind::kApStopped:
      state.softap_active = false;
      state.softap_ip.clear();
      ESP_LOGD(kTag, "SoftAP 接口已停止");
      break;
    case WifiRuntimeEventKind::kStaGotIp:
      state.station_connected = true;
      state.status = kWifiStatusConnected;
      state.last_warned_disconnect_reason = 0;
      state.last_warned_disconnect_ms = 0;
      state.has_connected_once = true;
      state.disconnect_started_ms = 0;
      state.reconnect_attempts = 0;
      state.local_ip = observed_ip;
      should_update_rssi = true;
      should_log_connected = true;
      break;
    }
  }

  if (should_refresh_softap_ip) {
    const std::string softap_ip = read_netif_ip("WIFI_AP_DEF");
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.softap_ip = softap_ip;
  }
  if (should_update_rssi) {
    wifi_driver_update_rssi_snapshot();
  }
  if (should_log_connected) {
    const WifiRuntimeSnapshot snapshot = wifi_runtime_snapshot();
    ESP_LOGI(kTag, "Wi-Fi 已连接：SSID=%s IP=%s RSSI=%ld，已清除重连失败窗口",
         snapshot.target_ssid.c_str(), snapshot.local_ip.c_str(),
         static_cast<long>(snapshot.rssi));
  }
  if (should_log_disconnect) {
    const WifiRuntimeSnapshot snapshot = wifi_runtime_snapshot();
    if (should_warn_disconnect) {
      ESP_LOGW(kTag, "Wi-Fi 已断开：原因=%d（%s） SSID=%s 状态=%d 重连次数=%u",
           disconnect_reason, wifi_disconnect_reason_name(disconnect_reason),
           ssid.c_str(), snapshot.status_code,
           static_cast<unsigned>(snapshot.reconnect_attempts));
    } else {
      ESP_LOGD(kTag,
               "Wi-Fi 已断开：原因=%d（%s） SSID=%s 状态=%d 重连次数=%u",
               disconnect_reason, wifi_disconnect_reason_name(disconnect_reason),
               ssid.c_str(), snapshot.status_code,
               static_cast<unsigned>(snapshot.reconnect_attempts));
    }
  }
}

void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                   void *event_data) {
  (void)arg;

  if (event_base == WIFI_EVENT) {
    switch (event_id) {
    case WIFI_EVENT_HOME_CHANNEL_CHANGE:
      enqueue_event({.kind=WifiRuntimeEventKind::kHomeChannelChanged});
      break;
    case WIFI_EVENT_STA_START:
      enqueue_event({.kind = WifiRuntimeEventKind::kStaStarted});
      break;
    case WIFI_EVENT_STA_CONNECTED:
      enqueue_event({.kind = WifiRuntimeEventKind::kStaConnected});
      break;
    case WIFI_EVENT_STA_DISCONNECTED: {
      WifiRuntimeEvent event = {
          .kind = WifiRuntimeEventKind::kStaDisconnected,
      };
      if (event_data != nullptr) {
        auto *disconnected =
            static_cast<wifi_event_sta_disconnected_t *>(event_data);
        event.has_disconnect_reason = true;
        event.disconnect_reason = disconnected->reason;
      }
      enqueue_event(event);
      break;
    }
    case WIFI_EVENT_AP_START:
      enqueue_event({.kind = WifiRuntimeEventKind::kApStarted});
      break;
    case WIFI_EVENT_AP_STOP:
      enqueue_event({.kind = WifiRuntimeEventKind::kApStopped});
      break;
    default:
      break;
    }
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    WifiRuntimeEvent event = {.kind = WifiRuntimeEventKind::kStaGotIp};
    if (event_data != nullptr) {
      auto *got_ip = static_cast<ip_event_got_ip_t *>(event_data);
      write_ip_info_string(event.local_ip, sizeof(event.local_ip),
                           got_ip->ip_info);
    }
    enqueue_event(event);
  }
}

} // namespace

bool wifi_driver_register_event_handlers() {
  if (wifi_handler_registered && ip_handler_registered) return true;
  std::string error;
  if (!wifi_driver_unregister_event_handlers(error)) return false;
  const esp_err_t wifi_error = esp_event_handler_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, nullptr);
  if (wifi_error != ESP_OK) {
    ESP_LOGE(kTag, "注册 Wi-Fi 事件处理器失败：%s",
         esp_err_to_name(wifi_error));
    return false;
  }

  wifi_handler_registered = true;
  const esp_err_t ip_error = esp_event_handler_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, nullptr);
  if (ip_error != ESP_OK) {
    (void)wifi_driver_unregister_event_handlers(error);
    ESP_LOGE(kTag, "注册 IP 事件处理器失败：%s", esp_err_to_name(ip_error));
    return false;
  }
  ip_handler_registered = true;
  return true;
}

bool wifi_driver_unregister_event_handlers(std::string &error) {
  error.clear();
  const auto remove = [&](bool &registered, esp_event_base_t base, int32_t id) {
    if (!registered) return;
    const auto result = esp_event_handler_unregister(base, id, &event_handler);
    if (result == ESP_OK) registered = false;
    else error = std::string("wifi_event_unregister_failed:") + esp_err_to_name(result);
  };
  remove(ip_handler_registered, IP_EVENT, IP_EVENT_STA_GOT_IP);
  remove(wifi_handler_registered, WIFI_EVENT, ESP_EVENT_ANY_ID);
  if (ip_handler_registered || wifi_handler_registered) return false;
  wifi_driver_clear_event_queue();
  return true;
}

void wifi_driver_clear_event_queue() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  increment(state.wifi_events.cleared, event_queue().count);
  state.wifi_events.queued = 0;
  event_queue() = {};
}

void wifi_driver_drain_events() {
  WifiRuntimeEvent event;
  while (pop_event(event)) {
    apply_event(event);
  }
}
