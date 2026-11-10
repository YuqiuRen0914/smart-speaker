#include "configuration/device_config.h"
#include "device_api/device_api_context.h"
#include "platform_link_state.h"
#include "platform_link_pairing_cycle.h"
#include "lunaros_platform_link_json.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <utility>
#include <sys/time.h>

#include "lunaros_network.h"
#include "lunaros_resource.h"
#include "configuration/settings.h"
#include "platform/system.h"

namespace lunaredu::lunaros::platform_link {
namespace {

constexpr size_t kMinimumDeviceCredentialLength = 43;
constexpr uint32_t kPlatformActivityWindowMs = 35'000;

// Serializes authority transitions. Observers run only after this lock is released.
std::mutex g_binding_transition_mutex;
bool g_binding_authority_observed = false; // Protected by transition mutex.
bool g_local_management_unbound=false;
bool g_local_binding_loaded=false;
std::string g_local_management_profile;
std::string g_binding_authority_profile;
std::optional<PlatformLinkPairingSnapshot> g_pending_binding;
std::mutex g_pairing_mutex;
PlatformLinkPairingSnapshot g_pairing_snapshot;
PairingCountdown g_pairing_countdown;
std::mutex g_platform_mutex;
PlatformLinkPlatformConnectionSnapshot g_platform_snapshot;
PlatformLinkConfig g_runtime_config;
DeviceApiContext *g_runtime_device = nullptr;

void clear_account_avatar_resource(PlatformLinkPairingSnapshot &snapshot) {
  snapshot.account_avatar_resource_available = false;
  snapshot.account_avatar_resource_revision.clear();
  snapshot.account_avatar_resource_url.clear();
  snapshot.account_avatar_resource_format.clear();
  snapshot.account_avatar_resource_width = 0;
  snapshot.account_avatar_resource_height = 0;
  snapshot.account_avatar_resource_maximum_bytes = 0;
}

void notify_binding(const PlatformLinkPairingSnapshot &binding) {
  if (g_runtime_config.observer && g_runtime_config.observer->binding_changed)
    g_runtime_config.observer->binding_changed(binding);
}

// transition_mutex is held; no observer callback or nested transition here.
bool commit_binding(PlatformLinkPairingSnapshot candidate, std::string &error,
                    bool authoritative = false) {
  if (!authoritative && (!g_binding_authority_observed || g_binding_authority_profile!=binding_profile_tag())) {
    error="binding_authority_unavailable"; return false;
  }
  g_binding_authority_observed = g_binding_authority_observed || authoritative;
  g_local_binding_loaded=true;
  g_local_management_unbound=false;
  if (authoritative) g_binding_authority_profile=binding_profile_tag();
  const auto previous = pairing_snapshot();
  if (previous.persistence_pending || candidate.revocation_requested || candidate.account_id != previous.account_id ||
      candidate.binding_epoch != previous.binding_epoch)
    suspend_private_resource_scope();
  if (!persist_cached_binding(candidate, error) ||
      (!candidate.revocation_requested &&
       !set_private_resource_scope(static_cast<uint32_t>(candidate.binding_epoch),
                                   candidate.account_id, error))) {
    suspend_private_resource_scope();
    g_pending_binding = candidate;
    candidate.persistence_pending = true;
    candidate.error = "binding_persistence:" + error;
    if (candidate.state != PlatformLinkBindingState::Unbound)
      candidate.state = PlatformLinkBindingState::Error;
    clear_account_avatar_resource(candidate);
    replace_pairing_snapshot(std::move(candidate));
    return false;
  }
  candidate.persistence_pending = false;
  g_local_management_unbound=candidate.state==PlatformLinkBindingState::Unbound&&candidate.account_id.empty();
  if(g_local_management_unbound)g_local_management_profile=binding_profile_tag();
  g_pending_binding.reset();
  replace_pairing_snapshot(std::move(candidate));
  error.clear();
  return true;
}

} // namespace

void configure_runtime(DeviceApiContext &runtime, const PlatformLinkConfig &config) {
  std::lock_guard<std::mutex> transition_lock(g_binding_transition_mutex);
  g_binding_authority_observed=g_pending_binding.has_value() && g_binding_authority_observed;
  g_runtime_device = &runtime;
  g_runtime_config = config;
  g_runtime_config.capabilities = normalize_platform_link_capabilities(config.capabilities);
  update_platform_connection(PlatformLinkPlatformConnectionState::Disconnected);
}

DeviceApiContext *runtime_device() { return g_runtime_device; }

const PlatformLinkConfig &runtime_config() {
  return g_runtime_config;
}

void update_pairing_snapshot(PlatformLinkBindingState state,
                             const std::string &code,
                             const std::string &bind_url,
                             int64_t expires_at_ms,
                             const std::string &error,
                             int binding_epoch,
                             const std::string &account_id,
                             const std::string &account_display_name,
                             const std::string &account_avatar_url,
                             bool account_can_manage_hardware) {
  PlatformLinkPairingSnapshot binding;
  {
    std::lock_guard<std::mutex> transition_lock(g_binding_transition_mutex);
    // A transient network/error response cannot erase a pending revocation.
    if (g_pending_binding) return;
    auto candidate = pairing_snapshot();
    if (candidate.revocation_requested) return;
    if (binding_epoch >= 0 && binding_epoch < candidate.binding_epoch) return;
    if (candidate.binding_claim_required &&
        (state == PlatformLinkBindingState::Bound || state == PlatformLinkBindingState::Connecting ||
         state == PlatformLinkBindingState::Error)) return;
    if (!account_id.empty() && account_id != candidate.account_id) {
      candidate.account_display_name.clear();
      candidate.account_avatar_url.clear();
      candidate.account_can_manage_hardware = false;
    }
    if ((!account_id.empty() && account_id != candidate.account_id) ||
        (binding_epoch >= 0 && binding_epoch != candidate.binding_epoch) ||
        state == PlatformLinkBindingState::Unbound) {
      clear_account_avatar_resource(candidate);
    }
    candidate.state = state;
    candidate.code = code;
    candidate.bind_url = bind_url;
    candidate.expires_at_ms = expires_at_ms;
    candidate.error = error;
    // Only a successful server confirmation releases the write-ahead intent.
    // A timeout may have lost a successful DELETE response.
    if ((state == PlatformLinkBindingState::Bound && error.empty()) ||
        state == PlatformLinkBindingState::Unbound)
      candidate.revocation_requested = false;
    // A bound response can be temporarily incomplete while the platform is
    // rebuilding its joined account/resource projection.  Only a non-empty
    // account id is authoritative; never replace a cached identity with an
    // empty field from such a response.
    if (!account_id.empty()) {
      candidate.account_id = account_id;
    }
    if (!account_display_name.empty()) {
      candidate.account_display_name = account_display_name;
    }
    if (!account_avatar_url.empty()) {
      candidate.account_avatar_url = account_avatar_url;
    }
    if (state == PlatformLinkBindingState::Bound) {
      candidate.account_can_manage_hardware =
          account_can_manage_hardware;
    }
    if (state == PlatformLinkBindingState::Unbound) {
      candidate.account_id.clear();
      candidate.account_display_name.clear();
      candidate.account_avatar_url.clear();
      candidate.account_can_manage_hardware = false;
    }
    if (binding_epoch >= 0) {
      candidate.binding_epoch = binding_epoch;
    }
    std::string commit_error;
    const bool authoritative = binding_epoch>=0 && error.empty() &&
        (state==PlatformLinkBindingState::Unbound ||
         (state==PlatformLinkBindingState::Bound && !account_id.empty()));
    if ((state == PlatformLinkBindingState::Bound || state == PlatformLinkBindingState::Unbound) &&
        (authoritative || g_binding_authority_observed))
      (void)commit_binding(std::move(candidate), commit_error, authoritative);
    else
      replace_pairing_snapshot(std::move(candidate));
    binding = pairing_snapshot();
  }
  notify_binding(binding);
}

void replace_pairing_snapshot(PlatformLinkPairingSnapshot snapshot) {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  const bool active = snapshot.state == PlatformLinkBindingState::Pairing &&
      !snapshot.code.empty() && !snapshot.persistence_pending && !snapshot.revocation_requested;
  if (g_pairing_countdown.needs_observation(active, snapshot.expires_at_ms)) {
    timeval wall_time{};
    const bool clock_ready = active && gettimeofday(&wall_time, nullptr) == 0 && wall_time.tv_sec > 0;
    const int64_t wall_ms = static_cast<int64_t>(wall_time.tv_sec) * 1000 + wall_time.tv_usec / 1000;
    g_pairing_countdown.observe(clock_ready, snapshot.expires_at_ms, wall_ms, system_monotonic_ms());
  }
  snapshot.revision = g_pairing_snapshot.revision + 1;
  g_pairing_snapshot = std::move(snapshot);
}

float pairing_remaining_fraction(uint32_t current_ms) {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  return g_pairing_countdown.remaining_fraction(current_ms);
}

bool load_cached_binding(std::string &error) {
  std::lock_guard<std::mutex> transition_lock(g_binding_transition_mutex);
  g_local_binding_loaded=true;
  // Stop/start in the same boot must not restore an older durable account
  // over an acknowledged but not yet committed revocation.
  if (g_pending_binding) return commit_binding(*g_pending_binding, error);
  g_binding_authority_observed=false;
  g_local_management_unbound=false;
  suspend_private_resource_scope();
  PlatformLinkPairingSnapshot cached;
  bool authority_restored=false;
  bool local_unbound=false;
  if (!read_cached_binding(cached, error, &authority_restored,&local_unbound) ||
      (!cached.revocation_requested &&
       !set_private_resource_scope(static_cast<uint32_t>(cached.binding_epoch), cached.account_id, error))) {
    cached = {};
    cached.state = PlatformLinkBindingState::Error;
    cached.error = "binding_restore:" + error;
    replace_pairing_snapshot(std::move(cached));
    return false;
  }
  replace_pairing_snapshot(std::move(cached));
  g_binding_authority_observed=authority_restored;
  g_local_management_unbound=local_unbound;
  if(local_unbound)g_local_management_profile=binding_profile_tag();
  if (authority_restored) g_binding_authority_profile=binding_profile_tag();
  return true;
}

bool persist_pairing_snapshot(std::string &error) {
  PlatformLinkPairingSnapshot binding;
  bool committed;
  {
    std::lock_guard<std::mutex> transition_lock(g_binding_transition_mutex);
    committed = commit_binding(g_pending_binding ? *g_pending_binding : pairing_snapshot(), error);
    binding = pairing_snapshot();
  }
  notify_binding(binding);
  return committed;
}

bool retry_binding_persistence(std::string &error) {
  return persist_pairing_snapshot(error);
}

bool binding_persistence_pending() {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  return g_pairing_snapshot.persistence_pending;
}

bool binding_revocation_requested() {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  return g_pairing_snapshot.revocation_requested;
}

bool reset_local_binding(std::string &error) {
  PlatformLinkPairingSnapshot binding;
  bool committed;
  {
    std::lock_guard<std::mutex> transition_lock(g_binding_transition_mutex);
    suspend_private_resource_scope();
    committed = commit_binding(PlatformLinkPairingSnapshot{}, error, true);
    binding = pairing_snapshot();
  }
  notify_binding(binding);
  return committed;
}

bool prepare_binding_revocation(std::string &error) {
  PlatformLinkPairingSnapshot binding;
  bool committed;
  {
    std::lock_guard<std::mutex> transition_lock(g_binding_transition_mutex);
    auto candidate = pairing_snapshot();
    if (candidate.persistence_pending || candidate.binding_epoch <= 0 ||
        (candidate.state != PlatformLinkBindingState::Bound && !candidate.revocation_requested)) {
      error = "binding_not_confirmed";
      return false;
    }
    const int epoch = candidate.binding_epoch;
    candidate = {};
    candidate.binding_epoch = epoch;
    candidate.revocation_requested = true;
    candidate.binding_claim_required = true;
    suspend_private_resource_scope();
    committed = commit_binding(std::move(candidate), error, true);
    binding = pairing_snapshot();
  }
  notify_binding(binding);
  return committed;
}

bool complete_binding_revocation(int expected_epoch, int binding_epoch,
                                 bool superseded, std::string &error) {
  PlatformLinkPairingSnapshot binding;
  bool committed;
  {
    std::lock_guard<std::mutex> transition_lock(g_binding_transition_mutex);
    auto candidate = pairing_snapshot();
    if (!candidate.revocation_requested || candidate.binding_epoch != expected_epoch ||
        binding_epoch < expected_epoch || candidate.persistence_pending) {
      error = "binding_revocation_changed";
      return false;
    }
    candidate.revocation_requested = false;
    candidate.binding_epoch = binding_epoch;
    candidate.error = superseded ? "binding_changed" : "";
    committed = commit_binding(std::move(candidate), error, true);
    binding = pairing_snapshot();
  }
  notify_binding(binding);
  return committed;
}

bool complete_pairing_binding(PlatformLinkPairingSnapshot binding, std::string &error) {
  bool committed;
  {
    std::lock_guard<std::mutex> transition_lock(g_binding_transition_mutex);
    const auto previous = pairing_snapshot();
    if (previous.revocation_requested || previous.persistence_pending ||
        previous.state != PlatformLinkBindingState::Pairing ||
        previous.revision != binding.revision || binding.binding_epoch <= previous.binding_epoch ||
        binding.account_id.empty()) {
      error = "pairing_binding_changed";
      return false;
    }
    binding.state = PlatformLinkBindingState::Bound;
    binding.binding_claim_required = false;
    binding.code.clear();
    binding.bind_url.clear();
    binding.expires_at_ms = 0;
    binding.error.clear();
    committed = commit_binding(std::move(binding), error, true);
    binding = pairing_snapshot();
  }
  notify_binding(binding);
  return committed;
}

void update_account_avatar_resource(const PlatformLinkPairingSnapshot &binding,
                                    JsonVariantConst manifest) {
  std::lock_guard<std::mutex> transition_lock(g_binding_transition_mutex);
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  if (binding.revision != g_pairing_snapshot.revision ||
      binding.state != PlatformLinkBindingState::Bound ||
      binding.account_id.empty() ||
      binding.account_id != g_pairing_snapshot.account_id ||
      binding.binding_epoch != g_pairing_snapshot.binding_epoch) {
    return;
  }
  const JsonVariantConst avatar = manifest["avatar"];
  if (avatar.isUnbound()) return;
  if (avatar.isNull()) {
    clear_account_avatar_resource(g_pairing_snapshot);
    ++g_pairing_snapshot.revision;
    return;
  }
  const std::string revision = avatar["revision"] | "";
  const std::string url = avatar["url"] | "";
  const std::string format = avatar["format"] | "";
  const uint16_t width = avatar["width"] | 0;
  const uint16_t height = avatar["height"] | 0;
  const size_t maximum_bytes = avatar["maximumBytes"] | 0;
  // Only the transport descriptor is validated here. Display requirements
  // such as 96x96 rgb666 belong to the product that renders the resource.
  if (revision.empty() || url.empty() || format.empty() || width == 0 ||
      height == 0 || maximum_bytes == 0) return;
  g_pairing_snapshot.account_avatar_resource_available = true;
  g_pairing_snapshot.account_avatar_resource_revision = revision;
  g_pairing_snapshot.account_avatar_resource_url = url;
  g_pairing_snapshot.account_avatar_resource_format = format;
  g_pairing_snapshot.account_avatar_resource_width = width;
  g_pairing_snapshot.account_avatar_resource_height = height;
  g_pairing_snapshot.account_avatar_resource_maximum_bytes = maximum_bytes;
  ++g_pairing_snapshot.revision;
}

PlatformLinkPairingSnapshot pairing_snapshot() {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  return g_pairing_snapshot;
}
PlatformLinkPairingPresentation pairing_presentation(){
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  const auto &value=g_pairing_snapshot;
  return {{value.state,value.persistence_pending,value.revocation_requested,value.binding_epoch,value.revision},value.code,value.bind_url,value.error,value.expires_at_ms};
}
bool ui_snapshot_into(PlatformLinkUiSnapshot &out){
  std::lock_guard<std::mutex> lock(g_pairing_mutex);const auto &value=g_pairing_snapshot;
  if(out.valid&&out.binding.revision==value.revision)return false;
  out.binding={value.state,value.persistence_pending,value.revocation_requested,value.binding_epoch,value.revision};
  out.code=value.code;out.bind_url=value.bind_url;out.error=value.error;out.expires_at_ms=value.expires_at_ms;
  out.account_id=value.account_id;out.account_display_name=value.account_display_name;
  out.account_avatar_resource_available=value.account_avatar_resource_available;
  out.account_avatar_resource_revision=value.account_avatar_resource_revision;out.account_avatar_resource_url=value.account_avatar_resource_url;
  out.account_avatar_resource_format=value.account_avatar_resource_format;out.account_avatar_resource_width=value.account_avatar_resource_width;
  out.account_avatar_resource_height=value.account_avatar_resource_height;out.valid=true;return true;
}

PlatformLinkBindingState pairing_state() {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  return g_pairing_snapshot.state;
}

PlatformLinkBindingStatus binding_status() {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  return {g_pairing_snapshot.state, g_pairing_snapshot.persistence_pending,
          g_pairing_snapshot.revocation_requested, g_pairing_snapshot.binding_epoch,
          g_pairing_snapshot.revision};
}

bool local_authority_ready() {
  std::lock_guard<std::mutex> transition_lock(g_binding_transition_mutex);
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  return g_binding_authority_observed && !g_pairing_snapshot.persistence_pending &&
      (!g_pairing_snapshot.revocation_requested || g_local_management_unbound) && g_binding_authority_profile==binding_profile_tag();
}
bool local_management_access_required() {
  std::lock_guard<std::mutex> transition_lock(g_binding_transition_mutex);
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  if(!g_runtime_device&&!g_local_binding_loaded&&device_cloud_credential().empty())return false;
  return !g_local_management_unbound || g_local_management_profile!=binding_profile_tag() ||
      g_pairing_snapshot.persistence_pending ||
      g_pairing_snapshot.state==PlatformLinkBindingState::Bound || !g_pairing_snapshot.account_id.empty();
}

PlatformLinkAccountScope account_scope() {
  std::lock_guard<std::mutex> lock(g_pairing_mutex);
  const auto &binding = g_pairing_snapshot;
  if (binding.state != PlatformLinkBindingState::Bound ||
      binding.persistence_pending || binding.revocation_requested ||
      binding.account_id.empty()) return {};
  return {binding.account_id + ":" + std::to_string(binding.binding_epoch),binding.binding_epoch};
}
std::string account_identity(){return account_scope().identity;}

void update_platform_connection(PlatformLinkPlatformConnectionState state,
                                int status_code,
                                const std::string &error) {
  std::lock_guard<std::mutex> lock(g_platform_mutex);
  if (state == PlatformLinkPlatformConnectionState::Disconnected && status_code < 0 && error.empty()) {
    g_platform_snapshot.operation.clear();
    g_platform_snapshot.error_code.clear();
  }
  // Every confirmed response refreshes liveness, even when presentation is
  // unchanged. Repeated 200s must not expire as an idle/disconnected link.
  g_platform_snapshot.last_activity_ms =
      state == PlatformLinkPlatformConnectionState::Disconnected
          ? 0
          : system_monotonic_ms();
  if (g_platform_snapshot.state == state &&
      g_platform_snapshot.status_code == status_code &&
      g_platform_snapshot.error == error) {
    return;
  }
  g_platform_snapshot.state = state;
  g_platform_snapshot.status_code = status_code;
  g_platform_snapshot.error = error;
  ++g_platform_snapshot.sequence;
}

void update_platform_request_diagnostics(const std::string &operation,const std::string &error_code) {
  std::lock_guard<std::mutex> lock(g_platform_mutex);
  g_platform_snapshot.operation=operation;
  g_platform_snapshot.error_code=error_code;
}
PlatformLinkPlatformConnectionSnapshot platform_connection_snapshot() {
  const bool connected = network_connected();
  const uint32_t now = system_monotonic_ms();
  std::lock_guard<std::mutex> lock(g_platform_mutex);
  if (!connected ||
      (g_platform_snapshot.state !=
           PlatformLinkPlatformConnectionState::Disconnected &&
       g_platform_snapshot.last_activity_ms != 0 &&
       now - g_platform_snapshot.last_activity_ms >
           kPlatformActivityWindowMs)) {
    PlatformLinkPlatformConnectionSnapshot disconnected;
    disconnected.sequence = g_platform_snapshot.sequence;
    return disconnected;
  }
  return g_platform_snapshot;
}
PlatformLinkPlatformConnectionSnapshot platform_connection_last_observation() {
  std::lock_guard<std::mutex> lock(g_platform_mutex);
  return g_platform_snapshot;
}
PlatformLinkPlatformConnectionState platform_connection_state(){
  const bool connected=network_connected();const auto now=system_monotonic_ms();
  std::lock_guard<std::mutex> lock(g_platform_mutex);
  if(!connected||(g_platform_snapshot.last_activity_ms&&now-g_platform_snapshot.last_activity_ms>kPlatformActivityWindowMs))
    return PlatformLinkPlatformConnectionState::Disconnected;
  return g_platform_snapshot.state;
}

PlatformLinkBindingState state_from_server(const std::string &status) {
  if (status=="active") return PlatformLinkBindingState::Bound;
  if (status=="provisioned" || status=="unbound") return PlatformLinkBindingState::Unbound;
  return PlatformLinkBindingState::Error;
}

bool account_identity_ready() {
  return device_cloud_credential_ready(kMinimumDeviceCredentialLength);
}

bool network_connected() {
  return network_fast_snapshot().link == NetworkLinkState::Up;
}

void update_shared_access(int epoch, bool enabled, const std::string &manager) {
  std::lock_guard<std::mutex> transition_lock(g_binding_transition_mutex);
  auto current = pairing_snapshot();
  // Delayed presence replies or MQTT frames cannot undo a newer revocation.
  if (epoch < 0 || epoch < current.access_epoch) return;
  if (current.access_epoch == epoch && current.shared_control_enabled == enabled && current.control_manager_id == manager) return;
  // Revoking sharing stops active outputs before exposing the new generation.
  if (current.access_epoch >= 0 && g_runtime_config.on_realtime_channel_lost)
    g_runtime_config.on_realtime_channel_lost();
  current.access_epoch = epoch;
  current.control_manager_id = manager;
  current.shared_control_enabled = enabled;
  replace_pairing_snapshot(std::move(current));
}

void add_capabilities(JsonArray capabilities) {
  capabilities.add("shared_access_v1");
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
    g_runtime_config.observer->write_runtime(g_runtime_config.observer->context, state);
  }
}

} // namespace lunaredu::lunaros::platform_link
