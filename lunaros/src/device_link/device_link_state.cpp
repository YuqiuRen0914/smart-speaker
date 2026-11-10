#include "device_link/device_link_state.h"
#include "lunaredu_lunaros/device_link/device_link_json.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/delivery/resources.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "kernel/system_primitives.h"

namespace lunaredu::lunaros::device_link {
namespace {

constexpr size_t kMinimumDeviceCredentialLength = 43;
constexpr uint32_t kPlatformActivityWindowMs = 35'000;

std::mutex g_pairing_mutex;
DeviceLinkPairingSnapshot g_pairing_snapshot;
std::mutex g_platform_mutex;
DeviceLinkPlatformConnectionSnapshot g_platform_snapshot;
DeviceLinkConfig g_runtime_config;

} // namespace

void configure_runtime(const DeviceLinkConfig &config) {
  g_runtime_config = config;
  update_platform_connection(DeviceLinkPlatformConnectionState::Disconnected);
}

const DeviceLinkConfig &runtime_config() {
  return g_runtime_config;
}

void update_pairing_snapshot(DeviceLinkBindingState state,
                             const std::string &code,
                             const std::string &bind_url,
                             int64_t expires_at_ms,
                             const std::string &error,
                             int binding_epoch,
                             const std::string &account_id,
                             const std::string &account_display_name,
                             const std::string &account_avatar_url,
                             bool account_can_manage_hardware) {
  {
    std::lock_guard<std::mutex> lock(g_pairing_mutex);
    if (!account_id.empty() && account_id != g_pairing_snapshot.account_id) {
      g_pairing_snapshot.account_display_name.clear();
      g_pairing_snapshot.account_avatar_url.clear();
      g_pairing_snapshot.account_can_manage_hardware = false;
    }
    g_pairing_snapshot.state = state;
    g_pairing_snapshot.code = code;
    g_pairing_snapshot.bind_url = bind_url;
    g_pairing_snapshot.expires_at_ms = expires_at_ms;
    g_pairing_snapshot.error = error;
    // A bound response can be temporarily incomplete while the platform is
    // rebuilding its joined account/resource projection.  Only a non-empty
    // account id is authoritative; never replace a cached identity with an
    // empty field from such a response.
    if (!account_id.empty()) {
      g_pairing_snapshot.account_id = account_id;
    }
    if (!account_display_name.empty()) {
      g_pairing_snapshot.account_display_name = account_display_name;
    }
    if (!account_avatar_url.empty()) {
      g_pairing_snapshot.account_avatar_url = account_avatar_url;
    }
    if (state == DeviceLinkBindingState::Bound) {
      g_pairing_snapshot.account_can_manage_hardware =
          account_can_manage_hardware;
    }
    if (state == DeviceLinkBindingState::Unbound) {
      g_pairing_snapshot.account_id.clear();
      g_pairing_snapshot.account_display_name.clear();
      g_pairing_snapshot.account_avatar_url.clear();
      g_pairing_snapshot.account_can_manage_hardware = false;
    }
    if (binding_epoch >= 0) {
      g_pairing_snapshot.binding_epoch = binding_epoch;
    }
    ++g_pairing_snapshot.revision;
  }
  const auto binding = pairing_snapshot();
  set_private_resource_scope(
      static_cast<uint32_t>(std::max(0, binding.binding_epoch)), binding.account_id);
  if (state == DeviceLinkBindingState::Unbound) {
    erase_cached_binding();
  }
  if (g_runtime_config.observer && g_runtime_config.observer->binding_changed) {
    g_runtime_config.observer->binding_changed(binding);
  }
}

void replace_pairing_snapshot(DeviceLinkPairingSnapshot snapshot) {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  snapshot.revision = g_pairing_snapshot.revision + 1;
  g_pairing_snapshot = std::move(snapshot);
}

DeviceLinkPairingSnapshot pairing_snapshot() {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  return g_pairing_snapshot;
}

DeviceLinkBindingState pairing_state() {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  return g_pairing_snapshot.state;
}

void update_platform_connection(DeviceLinkPlatformConnectionState state,
                                int status_code,
                                const std::string &error) {
  std::lock_guard<std::mutex> lock(g_platform_mutex);
  if (g_platform_snapshot.state == state &&
      g_platform_snapshot.status_code == status_code &&
      g_platform_snapshot.error == error) {
    return;
  }
  g_platform_snapshot.state = state;
  g_platform_snapshot.status_code = status_code;
  g_platform_snapshot.error = error;
  g_platform_snapshot.last_activity_ms =
      state == DeviceLinkPlatformConnectionState::Disconnected
          ? 0
          : internal::system_monotonic_ms();
  ++g_platform_snapshot.sequence;
}

DeviceLinkPlatformConnectionSnapshot platform_connection_snapshot() {
  const bool connected = network_connected();
  const uint32_t now = internal::system_monotonic_ms();
  std::lock_guard<std::mutex> lock(g_platform_mutex);
  if (!connected ||
      (g_platform_snapshot.state !=
           DeviceLinkPlatformConnectionState::Disconnected &&
       g_platform_snapshot.last_activity_ms != 0 &&
       now - g_platform_snapshot.last_activity_ms >
           kPlatformActivityWindowMs)) {
    DeviceLinkPlatformConnectionSnapshot disconnected;
    disconnected.sequence = g_platform_snapshot.sequence;
    return disconnected;
  }
  return g_platform_snapshot;
}

DeviceLinkBindingState state_from_server(const std::string &status) {
  return status == "active" ? DeviceLinkBindingState::Bound
                            : DeviceLinkBindingState::Unbound;
}

bool account_identity_ready() {
  return device_cloud_credential_ready(kMinimumDeviceCredentialLength);
}

bool network_connected() {
  return network_link_state() == NetworkLinkState::Up;
}

void report_state(bool operation_succeeded) {
  if (!account_identity_ready()) {
    report_network_device_link(NetworkDeviceLinkState::Unbound);
    return;
  }
  if (!operation_succeeded) {
    report_network_device_link(NetworkDeviceLinkState::Degraded,
                                         "device_link_operation_failed");
    return;
  }
  DeviceLinkBindingState state;
  std::string error;
  {
    std::lock_guard<std::mutex> lock(g_pairing_mutex);
    state = g_pairing_snapshot.state;
    if (state == DeviceLinkBindingState::Error) {
      error = g_pairing_snapshot.error;
    }
  }
  // Report outside the pairing lock; Network has its own synchronization.
  switch (state) {
  case DeviceLinkBindingState::Bound:
    report_network_device_link(NetworkDeviceLinkState::Ready);
    break;
  case DeviceLinkBindingState::Unbound:
    report_network_device_link(NetworkDeviceLinkState::Unbound);
    break;
  case DeviceLinkBindingState::Connecting:
  case DeviceLinkBindingState::Pairing:
    report_network_device_link(NetworkDeviceLinkState::Connecting);
    break;
  case DeviceLinkBindingState::Error:
    report_network_device_link(NetworkDeviceLinkState::Degraded,
                                         error.c_str());
    break;
  }
}

void add_capabilities(JsonArray capabilities) {
  for (const std::string &capability : g_runtime_config.capabilities) {
    capabilities.add(capability);
  }
}

void add_runtime_state(JsonObject state) {
  const NetworkSnapshot network = network_snapshot();
  state["localAddress"] = network.local_ip;
  state["controlBaseUrl"] = std::string("http://") + network.local_ip;
  state["wifiSsid"] = network.target_ssid;
  if (g_runtime_config.observer && g_runtime_config.observer->write_runtime) {
    g_runtime_config.observer->write_runtime(state);
  }
}

} // namespace lunaredu::lunaros::device_link
