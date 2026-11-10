#include "lunaros_network_policy.h"
#include "configuration/device_config.h"
#include "wifi_manager_internal.h"

#include "configuration/device_config.h"

using namespace lunaredu::lunaros;

namespace {

WifiDriverState &state = wifi_driver_state();

} // namespace

/**
 * @brief 拷贝并清洗 Wi-Fi 凭据数组。
 * @param source 源凭据数组。
 * @param source_count 源凭据数量。
 * @param target 目标凭据数组。
 * @param target_count 输出目标有效数量。
 * @return true 至少拷贝了一条有效 SSID。
 * @return false 没有有效 SSID。
 */
bool copy_wifi_credentials(const DeviceWifiCredential *source,
                           size_t source_count, DeviceWifiCredential *target,
                           uint8_t &target_count) {
  uint8_t next_count = 0;
  for (size_t index = 0;
       index < source_count && next_count < kDeviceWifiCredentialMax; ++index) {
    const std::string &ssid = source[index].ssid;
    if (ssid.empty()) {
      continue;
    }

    target[next_count].ssid = ssid;
    target[next_count].password = source[index].password;
    ++next_count;
  }

  for (uint8_t index = next_count; index < kDeviceWifiCredentialMax; ++index) {
    target[index].ssid = "";
    target[index].password = "";
  }

  target_count = next_count;
  return next_count > 0;
}

/**
 * @brief 限制 Wi-Fi 凭据索引在有效范围内。
 * @param index 候选索引。
 * @param count 凭据数量。
 * @return 合法索引；越界时返回 0。
 */
uint8_t clamp_wifi_credential_index(uint8_t index, uint8_t count) {
  return index < count ? index : 0;
}

/**
 * @brief 将指定 Wi-Fi 凭据设为当前连接目标。
 * @param index 凭据索引。
 */
void set_active_wifi_credential(uint8_t index) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (state.credential_count == 0) {
    state.ssid = "";
    state.password = "";
    state.current_credential_index = 0;
    return;
  }

  state.current_credential_index =
      clamp_wifi_credential_index(index, state.credential_count);
  state.ssid = state.credentials[state.current_credential_index].ssid;
  state.password =
      state.credentials[state.current_credential_index].password;
}

/**
 * @brief 重置 Wi-Fi 凭据尝试顺序。
 * @param preferred_index 优先尝试的凭据索引。
 */
void reset_wifi_attempt_sequence(uint8_t preferred_index) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  state.preferred_credential_index =
      clamp_wifi_credential_index(preferred_index, state.user_credential_count);
  state.retry_scan_started = false;
  state.current_candidate_tried = false;
  set_active_wifi_credential(state.preferred_credential_index);
}

/**
 * @brief 从运行配置加载 Wi-Fi 凭据。
 */
void load_wifi_credentials_from_runtime() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const DeviceWifiConfig config = device_wifi_config();
  copy_wifi_credentials(config.credentials, config.credential_count,
                        state.credentials, state.credential_count);
  append_default_wifi_fallback();
  reset_wifi_attempt_sequence(config.last_success_index);
}

void append_default_wifi_fallback() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  state.user_credential_count = state.temporary_credentials?0:state.credential_count;
  bool duplicate = false;
  for (uint8_t index = 0; index < state.user_credential_count; ++index) {
    duplicate = duplicate ||
        (state.credentials[index].ssid == kNetworkDefaultWifiSsid &&
         state.credentials[index].password == kNetworkDefaultWifiPassword);
  }
  if (!state.temporary_credentials && state.station_enabled && state.default_fallback_enabled && !duplicate &&
      state.credential_count < kWifiRuntimeCredentialCapacity) {
    state.credentials[state.credential_count++] =
        {kNetworkDefaultWifiSsid, kNetworkDefaultWifiPassword};
  }
  for (size_t index = state.credential_count;
       index < kWifiRuntimeCredentialCapacity; ++index) {
    state.credentials[index] = {};
  }
}

/**
 * @brief 暂存新的 Wi-Fi 凭据，等待 tick 中异步应用。
 * @param credentials Wi-Fi 凭据数组。
 * @param credential_count 凭据数量。
 * @param preferred_index 优先尝试的凭据索引。
 * @return true 暂存成功。
 * @return false 没有有效 SSID。
 */
bool queue_wifi_credentials_apply(const DeviceWifiCredential *credentials,
                                  size_t credential_count,
                                  uint8_t preferred_index, bool temporary) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (!copy_wifi_credentials(credentials, credential_count,
                             state.pending_credentials,
                             state.pending_credential_count)) {
    return false;
  }

  state.pending_preferred_index =
      clamp_wifi_credential_index(preferred_index,
                                  state.pending_credential_count);
  state.pending_temporary_credentials=temporary;
  state.pending_apply = true;
  return true;
}

/**
 * @brief 在连接失败后推进到下一个 Wi-Fi 候选凭据。
 */
void advance_wifi_candidate() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (state.credential_count <= 1) {
    return;
  }

  if (!state.current_candidate_tried) {
    return;
  }

  if (!state.retry_scan_started) {
    state.retry_scan_started = true;
    set_active_wifi_credential(state.preferred_credential_index == 0 ? 1 : 0);
    state.current_candidate_tried = false;
    return;
  }

  // Try the preferred user network, then every other saved user network in
  // order, and finally the runtime fallback. Start the same order next cycle.
  uint8_t next = state.current_credential_index + 1;
  if (next == state.preferred_credential_index) ++next;
  if (next >= state.credential_count) {
    next = state.preferred_credential_index;
    state.retry_scan_started = false;
  }
  set_active_wifi_credential(next);
  state.current_candidate_tried = false;
}

/**
 * @brief 首次检测到 Wi-Fi 连接成功时更新优先索引。
 */
void handle_wifi_connected_once() {
  uint8_t last_success_index = 0;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.was_connected = true;
    state.has_connected_once = true;
    // A fallback success must not become a saved user index or displace the
    // preferred user network on the next reconnect/boot.
    if (state.current_credential_index >= state.user_credential_count) return;
    state.preferred_credential_index = state.current_credential_index;
    state.retry_scan_started = false;
    last_success_index = state.current_credential_index;
  }
  save_device_wifi_last_success_index(last_success_index);
}
