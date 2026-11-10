#include "lunaros_network_service_id.h"
#include "lunaros_provisioning_policy.h"
#include "lunaros_network_wifi_configuration.h"
#include "lunaros_network_local_access.h"
#include "client_proof.h"
#include "lunaros_hmac.h"
#include "official_protocol.h"
#include <memory>
#include <new>
#include "lunaredu_device_profiles.h"
#include "configuration/device_config.h"
#include "device_api/device_frame_endpoint.h"
#include "hotspot_password.h"
#include "kernel/runtime.h"
#include "kernel/service_executor.h"
#include "device_api/device_api_context.h"
#include "kernel/static_mutex.h"
#include "lunaros_network.h"
#include "lunaros_provisioning.h"
#include "projection.h"
#include "activity_session.h"
#include "activity.h"
#include "sdkconfig.h"
#include "system/identity.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <esp_random.h>
#include <esp_timer.h>
#include <mbedtls/platform_util.h>
namespace lunaredu::lunaros {
namespace {
StaticMutex mutex;
KernelRuntime *owner = nullptr;
lunaredu::lunaros::ProvisioningPolicy policy;
ProvisioningSnapshot state;
uint64_t session_generation = 0;
lunaredu::lunaros::ProvisioningMode active_mode = lunaredu::lunaros::ProvisioningMode::Disabled;
ServiceResourceWindow window;
NetworkLocalAccessLease access_lease;
std::string password, proof;
uint32_t started_at = 0, finished_at = 0, next_auto_at = 0;
bool cooldown = false, finished = false, owns_access = false,
     auto_started = false;
std::atomic<uint8_t> disconnected_mask{0};
struct Client {
  uint32_t nonce = 0, expires = 0;
  std::unique_ptr<OfficialProvisioning> official;
};
std::array<Client, 2> clients;
uint32_t now() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
void wipe(std::string &s) {
  if(!s.empty())mbedtls_platform_zeroize(s.data(),s.size());
  s.clear();
}
bool same_proof(const char *s) {
  if (!s || std::strlen(s) != proof.size() || proof.size() != 32)
    return false;
  return digest::constant_time_equal(s,proof.data(),proof.size());
}
void revoke_clients() {
  if (owner)
    for (size_t i = 0; i < clients.size(); ++i)
      revoke_device_frame_authority({*owner, DeviceFrameTransport(i)});
  clients = {};
  wipe(proof);
}
bool gatt_frame(const DeviceFrame &request, DeviceFrame &response,
                void *context) {
  return dispatch_device_frame(
      {*static_cast<KernelRuntime *>(context), DeviceFrameTransport::Ble},
      request, response);
}
void gatt_closed(void *context) {
  disconnect_device_frame_transport(
      {*static_cast<KernelRuntime *>(context), DeviceFrameTransport::Ble});
}
NetworkLocalGattConfig gatt_config() {
  return {"LunarOS", gatt_frame, owner, gatt_closed};
}
bool require(KernelRuntime &runtime, std::string &error) {
  if (owner != &runtime || !state.started || !state.enabled) {
    error = "provision_not_ready";
    return false;
  }
  return true;
}
bool retire(std::string &error) {
  if(!digest::hmac_sha256_cleanup()) {
    state.state=ProvisioningState::Stopping;
    state.error=error="provisioning_mac_key_retirement_pending";
    return false;
  }
  if (state.state == ProvisioningState::Idle) {
    error.clear();
    return true;
  }
  state.state = ProvisioningState::Stopping;
  if (owns_access) {
    (void)request_network_close_local_access(*owner, access_lease, error);
    if (network_fast_snapshot().local_access != NetworkLocalAccessState::Idle) {
      state.error = error.empty() ? "local_access_stop_pending" : error;
      return false;
    }
    owns_access = false;
    access_lease = {};
  }
  if (window.generation && !owner->release_service_resource(window, error)) {
    state.error = error;
    return false;
  }
  window = {};
  state.state = ProvisioningState::Idle;
  state.remaining_ms = 0;
  state.ssid.clear();
  state.ip.clear();
  state.error.clear();
  wipe(password);
  revoke_clients();
  active_mode = lunaredu::lunaros::ProvisioningMode::Disabled;
  finished = false;
  next_auto_at = now() + policy.retry_cooldown_ms;
  cooldown = true;
  ++state.sequence;
  error.clear();
  return true;
}
bool prepare(std::string &error) {
  if (active_mode == lunaredu::lunaros::ProvisioningMode::Direct) {
    state.state = ProvisioningState::Active;
    return true;
  }
  if (owns_access) {
    const auto access = network_fast_snapshot().local_access;
    if (access == NetworkLocalAccessState::Preparing) return true;
    if (access != NetworkLocalAccessState::Active) {
      error = "local_access_start_failed";
      return false;
    }
    state.state = ProvisioningState::Active;
    ++state.sequence;
    return true;
  }
  if (!owner->reserve_service_resource(kBackgroundExecutionResource,
                                       kProvisioningServiceId, window, error)) {
    return window.generation != 0;
  }
  NetworkLocalAccessConfig config;
  config.ssid = device_config_provision_ap_ssid(device_identity_config());
  config.password = password;
  config.suspend_station = state.suspends_station;
  config.ble = active_mode == lunaredu::lunaros::ProvisioningMode::SoftApBle;
  config.gatt = gatt_config();
  // A failed start can still own resources while the transport retires.
  access_lease = {};
  if (!request_network_open_local_access(*owner, config, access_lease, error)) {
    owns_access = access_lease.generation != 0;
    const auto start_error = error;
    state.state = ProvisioningState::Stopping;
    (void)retire(error);
    state.error = start_error;
    error = start_error;
    return false;
  }
  owns_access = true;
  state.state = network_fast_snapshot().local_access == NetworkLocalAccessState::Active
      ? ProvisioningState::Active : ProvisioningState::Preparing;
  state.ssid = config.ssid;
  ++state.sequence;
  return true;
}
bool begin(lunaredu::lunaros::ProvisioningMode mode, bool suspend, std::string &error, bool deferred = false) {
  if (mode == lunaredu::lunaros::ProvisioningMode::Disabled) {
    error = "provisioning_mode_disabled";
    return false;
  }
  if (state.state == ProvisioningState::Stopping) {
    error = "provisioning_stop_pending";
    return false;
  }
  if (state.state != ProvisioningState::Idle) {
    error.clear();
    return true;
  }
  if (!network_fast_snapshot().station_enabled) {
    error = "network_station_disabled";
    return false;
  }
  if (session_generation == UINT64_MAX) { error = "provisioning_session_generation_exhausted"; return false; }
  state.session = ++session_generation; state.expired = false;
  // Generic UI requests respect the product's selected channels.
  if (mode == lunaredu::lunaros::ProvisioningMode::SoftAp &&
      policy.mode == lunaredu::lunaros::ProvisioningMode::SoftApBle)
    mode = policy.mode;
  auto_started = false;
  revoke_clients();
  state.progress = {};
  finished = false;
  state.error.clear();
  state.suspends_station = suspend;
  active_mode = mode;
  started_at = now();
  if (mode != lunaredu::lunaros::ProvisioningMode::Direct &&
      !provisioning_hotspot_password(policy.softap_password, password, error))
    return false;
  if (mode == lunaredu::lunaros::ProvisioningMode::SoftApBle) {
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    proof=digest::hex(bytes,sizeof(bytes));
    mbedtls_platform_zeroize(bytes,sizeof(bytes));
  }
  state.state = ProvisioningState::Preparing;
  state.remaining_ms = policy.window_timeout_ms;
  ++state.sequence;
  if (deferred) { error.clear(); return true; }
  if (!prepare(error)) {
    const auto failure=error;
    if (state.state != ProvisioningState::Idle) {
      state.state = ProvisioningState::Stopping;
      (void)retire(error);
    }
    return false;
  }
  error.clear();
  return true;
}
enum class Kind { Begin, End, Cancel, Submit, Claim, Release };
struct Operation {
  Kind kind;
  KernelRuntime *runtime;
  lunaredu::lunaros::ProvisioningMode mode = lunaredu::lunaros::ProvisioningMode::Disabled;
  bool suspend = false, use_saved = false;
  uint64_t session = 0;
  bool owned = false;
  uint32_t attempt = 0;
  const char *ssid = nullptr, *password = nullptr;
  NetworkWifiCredentialResult submitted = NetworkWifiCredentialResult::NotReady;
};
void execute(void *opaque, ServiceOperationResult &result) {
  auto &op = *static_cast<Operation *>(opaque);
  StaticLockGuard lock(mutex);
  if (!require(*op.runtime, result.error)) {
    result.ok = false;
    return;
  }
  switch (op.kind) {
  case Kind::Claim:
    op.owned = state.state == ProvisioningState::Idle;
    result.ok = begin(policy.mode, false, result.error, true);
    op.session = state.session;
    return;
  case Kind::Release:
    if (!op.session || op.session != state.session) { result.ok = false; result.error = "stale_provisioning_session"; return; }
    revoke_clients();
    state.state = state.state == ProvisioningState::Idle ? ProvisioningState::Idle : ProvisioningState::Stopping;
    result.error.clear();
    result.ok = true;
    return;
  case Kind::Begin:
    result.ok = begin(op.mode, op.suspend, result.error);
    return;
  case Kind::End:
    revoke_clients();
    state.state = state.state == ProvisioningState::Idle
                      ? ProvisioningState::Idle
                      : ProvisioningState::Stopping;
    (void)retire(result.error);
    result.ok = true;
    return;
  case Kind::Cancel:
    if (!op.attempt || op.attempt != state.progress.attempt) {
      result.ok = false;
      result.error = "provisioning_attempt_not_found";
      return;
    }
    result.ok = request_network_cancel_wifi_connection(*owner, op.attempt,
                                                       result.error);
    return;
  case Kind::Submit:
    if (state.state == ProvisioningState::Stopping) {
      op.submitted = NetworkWifiCredentialResult::Busy;
      result.ok = false;
      result.error = "provisioning_stop_pending";
      return;
    }
    if (state.state == ProvisioningState::Idle &&
        !begin(lunaredu::lunaros::ProvisioningMode::Direct, false, result.error)) {
      result.ok = false;
      return;
    }
    op.submitted =
        op.use_saved
            ? request_network_connect_saved_wifi(*owner, op.ssid, op.attempt)
            : request_network_connect_wifi(*owner, op.ssid, op.password,
                                           op.attempt);
    result.ok = op.submitted == NetworkWifiCredentialResult::Ok;
    if (result.ok) {
      state.progress = {
          "connecting", op.ssid ? op.ssid : "", {}, {}, op.attempt};
      finished = false;
      ++state.sequence;
    }
    return;
  }
}
bool invoke(KernelRuntime &runtime, Operation &op, std::string &error) {
  op.runtime = &runtime;
  return runtime.invoke_executor(kNetworkServiceExecutor, execute, &op, error);
}
struct FrameCall {
  const DeviceFrameContext *context;
  const DeviceFrame *request;
  DeviceFrame *response;
};
void frame_on_owner(void *opaque, ServiceOperationResult &result) {
  auto &call = *static_cast<FrameCall *>(opaque);
  const auto &context = *call.context;
  const auto &request = *call.request;
  auto &response = *call.response;
  JsonDocument input, out;
  out["ok"] = true;
  const auto type = static_cast<DeviceFrameType>(request.type);
  auto reject = [&](const char *error) {
    out.clear();
    out["ok"] = false;
    out["error"]["code"] = error;
  };
  auto progress = [&] {
    const auto s = provisioning_snapshot();
    out["phase"] = s.progress.phase;
    out["wifi_state"] = s.progress.phase;
    out["ssid"] = s.progress.ssid;
    out["ip"] = s.progress.ip;
    out["error"] = s.progress.error;
    out["attempt"] = s.progress.attempt;
    const auto wifi = network_snapshot();
    out["network_connected"] =
        wifi.link == NetworkLinkState::Up && !wifi.local_ip.empty();
    out["network_ssid"] = wifi.target_ssid;
    out["network_ip"] = wifi.local_ip;
  };
  if (type == DeviceFrameType::GetInfo) {
    out["provisioning_supported"] = true;
    out["provisioning_protocols"].to<JsonArray>().add(lunaredu::protocol::profiles::provisioning_protocol);
    out["protocolVersion"] = kDeviceProtocolVersion;
    out["transport"] = context.transport == DeviceFrameTransport::Usb
                           ? "usb_serial_jtag"
                           : "ble";
    out["serialNumber"] = hardware_serial_number();
    out["firmwareVersion"] = firmware_version();
    out["model"] = "LunarOS";
    out["binding_supported"] =
        device_frame_supported(uint8_t(DeviceFrameType::GetBinding));
    out["confirmation"] = context.transport == DeviceFrameTransport::Usb
                              ? "physical_usb"
                              : "qr_proof";
    if (context.transport == DeviceFrameTransport::Usb) {
      const auto url = provisioning_local_url();
      if (!url.empty())
        out["provisioningUrl"] = url;
    }
    progress();
  } else if (type == DeviceFrameType::GetStatus)
    progress();
  else if (deserializeJson(input, request.payload) || !input.is<JsonObject>())
    reject("invalid_payload");
  else if (type == DeviceFrameType::SetWifi) {
    const uint32_t nonce = input["nonce"] | uint32_t{0};
    const bool confirmed = device_frame_authorized(context, nonce);
    if (!confirmed)
      reject("bootstrap_confirmation_required");
    else if (input["protocol"] == lunaredu::protocol::profiles::provisioning_protocol) {
      auto &client = clients[size_t(context.transport)];
      if (!client.nonce || nonce != client.nonce || static_cast<int32_t>(now() - client.expires) >= 0 ||
          !input["endpoint"].is<const char *>() || !input["data"].is<const char *>()) reject("bootstrap_session_expired");
      else {
        if (!client.official) client.official.reset(new (std::nothrow) OfficialProvisioning());
        std::string encoded, error;
        const auto possession = context.transport == DeviceFrameTransport::Ble ? proof : std::to_string(nonce);
        if (!client.official) reject("provisioning_out_of_memory");
        else if (!client.official->exchange(context.runtime, nonce, possession, input["endpoint"], input["data"].as<std::string>(), encoded, error)) reject(error.c_str());
        else { progress(); out["data"] = encoded; }
      }
    }
    else reject("official_provisioning_required");
  } else {
    StaticLockGuard lock(mutex);
    std::string error;
    auto &client = clients[size_t(context.transport)];
    if (!require(context.runtime, error))
      reject(error.c_str());
    else if (type == DeviceFrameType::OpenSession) {
      if (state.state == ProvisioningState::Stopping)
        reject("provisioning_stop_pending");
      else if (context.transport == DeviceFrameTransport::Ble &&
               (state.state != ProvisioningState::Active || proof.empty()))
        reject("provision_not_ready");
      else if (context.transport == DeviceFrameTransport::Usb &&
               state.state == ProvisioningState::Idle &&
               !begin(lunaredu::lunaros::ProvisioningMode::Direct, false, error))
        reject(error.c_str());
      else {
        revoke_device_frame_authority(context);
        client = {};
        client.nonce = esp_random();
        if (!client.nonce)
          client.nonce = 1;
        client.expires = now() + 300000;
        out["nonce"] = client.nonce;
        out["expires_in_ms"] = 300000;
        out["expires_at_ms"] = client.expires;
        if (context.transport == DeviceFrameTransport::Ble) {
          const auto confirmation = device_confirmation(client.nonce, proof);
          if (confirmation.empty()) {
            client = {};
            reject("bootstrap_crypto_failed");
          } else
            out["device_confirmation"] = confirmation;
        }
      }
    } else if (type == DeviceFrameType::ConfirmSession) {
      if (!client.nonce || !input["nonce"].is<uint32_t>() ||
          input["nonce"].as<uint32_t>() != client.nonce ||
          static_cast<int32_t>(now() - client.expires) >= 0 ||
          (context.transport == DeviceFrameTransport::Ble &&
           !same_proof(input["proof"] | "")))
        reject("bootstrap_confirmation_failed");
      else {
        set_device_frame_authority(context, client.nonce, client.expires);
        out["confirmed"] = true;
        out["phase"] = "confirmed";
      }
    } else if (type == DeviceFrameType::CloseSession) {
      client = {};
      revoke_device_frame_authority(context);
      out["phase"] = "idle";
    } else
      reject("bootstrap_unsupported");
  }
  response.version = request.version;
  response.request_id = request.request_id;
  response.type = (out["ok"].as<bool>()
                       ? request.type
                       : static_cast<uint8_t>(DeviceFrameType::Error)) |
                  kDeviceFrameResponseFlag;
  serializeJson(out, response.payload);
  result.ok = true;
}
bool frame(const DeviceFrameContext &context, const DeviceFrame &request,
           DeviceFrame &response) {
  if (request.type < uint8_t(DeviceFrameType::GetInfo) ||
      request.type > uint8_t(DeviceFrameType::CloseSession))
    return false;
  {
    StaticLockGuard lock(mutex);
    if (owner != &context.runtime)
      return false;
  }
  FrameCall call{&context, &request, &response};
  std::string error;
  if (!context.runtime.invoke_executor(kNetworkServiceExecutor, frame_on_owner,
                                       &call, error)) {
    response.version = request.version;
    response.request_id = request.request_id;
    response.type =
        uint8_t(DeviceFrameType::Error) | kDeviceFrameResponseFlag;
    JsonDocument out;
    out["ok"] = false;
    out["error"]["code"] = error;
    serializeJson(out, response.payload);
  }
  return true;
}
void disconnected(const DeviceFrameContext &context) {
  disconnected_mask.fetch_or(uint8_t(1u << uint8_t(context.transport)));
}
bool supports(uint8_t type) {
  StaticLockGuard lock(mutex);
  return state.started && state.enabled &&
         type >= uint8_t(DeviceFrameType::GetInfo) &&
         type <= uint8_t(DeviceFrameType::CloseSession);
}
const DeviceFrameEndpoint endpoint{frame, disconnected, supports};
ServiceStartResult start(void *opaque) {
  auto &device = *static_cast<DeviceApiContext *>(opaque);
  StaticLockGuard lock(mutex);
  if (owner != &device.runtime)
    return {ServiceStartDisposition::Failed, "provisioning_not_configured"};
  if (!install_device_frame_endpoint(device.runtime, endpoint))
    return {ServiceStartDisposition::Failed, "provisioning_endpoint_conflict"};
  state.started = true;
  state.enabled = policy.enabled && policy.mode != lunaredu::lunaros::ProvisioningMode::Disabled && network_fast_snapshot().station_enabled &&
                  CONFIG_LUNAROS_PROVISIONING;
  state.mode = policy.mode;
  ++state.sequence;
  if (state.enabled && policy.mode == lunaredu::lunaros::ProvisioningMode::SoftApBle) {
    const auto gatt = gatt_config();
    std::string error;
    if (!request_network_prepare_local_gatt(*owner, gatt, error)) {
      // BLE is an additional transport. Its failure must not remove the
      // independent SoftAP/HTTP repair path on an otherwise usable radio.
      policy.mode = ProvisioningMode::SoftAp;
      state.mode = ProvisioningMode::SoftAp;
      state.error = error;
    }
  }
  std::string activation_error;
  if (state.enabled &&
      !device.model.capabilities.activate(provisioning_capability_descriptor(),
                                          &activation_error)) {
    state.started = false;
    state.enabled = false;
    state.error = activation_error;
    remove_device_frame_endpoint(device.runtime, endpoint);
    return {ServiceStartDisposition::Failed, activation_error};
  }
  return {ServiceStartDisposition::Ready, "provisioning_ready"};
}
bool stop(void *opaque, std::string &error) {
  auto &device = *static_cast<DeviceApiContext *>(opaque);
  StaticLockGuard lock(mutex);
  if (owner != &device.runtime) {
    error = "provisioning_runtime_owner_mismatch";
    return false;
  }
  state.enabled = false;
  revoke_clients();
  remove_device_frame_endpoint(device.runtime, endpoint);
  (void)device.model.capabilities.deactivate("provisioning");
  if (!retire(error))
    return false;
  state.started = false;
  ++state.sequence;
  return true;
}
} // namespace
const char *provisioning_mode_name(lunaredu::lunaros::ProvisioningMode mode) {
  switch (mode) {
  case lunaredu::lunaros::ProvisioningMode::Disabled:
    return "disabled";
  case lunaredu::lunaros::ProvisioningMode::SoftAp:
    return "softap_only";
  case lunaredu::lunaros::ProvisioningMode::SoftApBle:
    return "softap_ble";
  case lunaredu::lunaros::ProvisioningMode::Direct:
    return "direct";
  }
  return "unknown";
}
bool configure_provisioning_runtime(KernelRuntime &runtime,
                                    const lunaredu::lunaros::ProvisioningPolicy &value,
                                    std::string &error) {
  StaticLockGuard lock(mutex);
  if (state.started || state.state != ProvisioningState::Idle ||
      window.generation) {
    error = "provisioning_runtime_not_quiescent";
    return false;
  }
  if (!value.window_timeout_ms || value.window_timeout_ms > 3600000 ||
      value.retry_cooldown_ms > 3600000) {
    error = "invalid_provisioning_policy";
    return false;
  }
  owner = &runtime;
  policy = value;
  state = {};
  state.enabled = value.enabled;
  state.mode = value.mode;
  cooldown = false;
  finished = false;
  next_auto_at = 0;
  error.clear();
  return true;
}
ServiceNode provisioning_service_node(DeviceApiContext &device) {
  static constexpr ServiceDependency deps[] = {
      {kNetworkRuntimeServiceId, true}};
  return {{kProvisioningServiceId, "Network provisioning",
           kNetworkServiceExecutor, ServiceCriticality::Optional, deps, 1},
          start,
          stop,
          &device};
}
ProvisioningSnapshot provisioning_snapshot() {
  StaticLockGuard lock(mutex);
  return state;
}
ProvisioningFastSnapshot provisioning_fast_snapshot() {
  StaticLockGuard lock(mutex);
  return {state.sequence,
          state.state,
          state.started,
          state.enabled,
          !state.ip.empty(),
          state.state != ProvisioningState::Idle && !state.ssid.empty()};
}
ProvisioningProgress provisioning_progress() {
  StaticLockGuard lock(mutex);
  return state.progress;
}
std::string provisioning_local_password() {
  StaticLockGuard lock(mutex);
  return state.state == ProvisioningState::Active ? password : "";
}
std::string provisioning_local_url() {
  StaticLockGuard lock(mutex);
  return state.state == ProvisioningState::Active && !proof.empty() && network_local_gatt_running()
             ? std::string("https://board.lunaredu.cn/link/provision?d=") +
                   hardware_serial_number() + "#p=" + proof
             : "";
}
bool request_provisioning_begin(KernelRuntime &runtime, lunaredu::lunaros::ProvisioningMode mode,
                                bool suspend, std::string &error) {
  Operation op{Kind::Begin, &runtime};
  op.mode = mode;
  op.suspend = suspend;
  return invoke(runtime, op, error);
}
bool request_provisioning_end(KernelRuntime &runtime, std::string &error) {
  Operation op{Kind::End, &runtime};
  return invoke(runtime, op, error);
}
bool request_provisioning_cancel_attempt(KernelRuntime &runtime,
                                         uint32_t attempt, std::string &error) {
  Operation op{Kind::Cancel, &runtime};
  op.attempt = attempt;
  return invoke(runtime, op, error);
}
NetworkWifiCredentialResult
submit_provisioning_saved_wifi(KernelRuntime &runtime, const char *ssid) {
  Operation op{Kind::Submit, &runtime};
  op.ssid = ssid;
  op.use_saved = true;
  std::string error;
  (void)invoke(runtime, op, error);
  return op.submitted;
}
NetworkWifiCredentialResult
submit_provisioning_wifi_credentials(KernelRuntime &runtime, const char *ssid,
                                     const char *password_value) {
  Operation op{Kind::Submit, &runtime};
  op.ssid = ssid;
  op.password = password_value;
  std::string error;
  (void)invoke(runtime, op, error);
  return op.submitted;
}
namespace provisioning {
ActivityObservation observe_session() {
  StaticLockGuard lock(mutex);
  ActivityObservation out{state,{}, {}};
  if (state.state == ProvisioningState::Active) {
    out.password = password;
    if (!proof.empty() && network_local_gatt_running())
      out.url = std::string("https://board.lunaredu.cn/link/provision?d=") + hardware_serial_number() + "#p=" + proof;
  }
  return out;
}
bool claim_session(uint64_t &session, bool &owned, std::string &error) {
  KernelRuntime *runtime;
  { StaticLockGuard lock(mutex); runtime = owner; }
  if (!runtime) { error = "provision_not_ready"; return false; }
  Operation op{Kind::Claim, runtime};
  const bool ok = invoke(*runtime, op, error); session = op.session; owned = op.owned; return ok;
}
bool release_session(uint64_t session, std::string &error) {
  KernelRuntime *runtime;
  { StaticLockGuard lock(mutex); runtime = owner; }
  if (!runtime) { error = "provision_not_ready"; return false; }
  Operation op{Kind::Release, runtime}; op.session = session; return invoke(*runtime, op, error);
}
}
void poll_provisioning_on_owner() {
  StaticLockGuard lock(mutex);
  if (!state.started)
    return;
  const auto disconnected = disconnected_mask.exchange(0);
  for (size_t i = 0; i < clients.size(); ++i)
    if ((disconnected & (1u << i)) || (clients[i].nonce && static_cast<int32_t>(now() - clients[i].expires) >= 0))
      clients[i] = {};
  std::string error;
  const auto time = now();
  if (state.state == ProvisioningState::Stopping) {
    const auto failure = state.error;
    (void)retire(error);
    if (!failure.empty()) state.error = failure;
    return;
  }
  if (!state.enabled)
    return;
  if (state.state == ProvisioningState::Preparing) {
    if (time - started_at >= policy.window_timeout_ms) {
      state.expired = true;
      revoke_clients();
      state.state = ProvisioningState::Stopping;
      (void)retire(error);
      return;
    }
    if (!prepare(error)) {
      state.error = error;
      state.state = ProvisioningState::Stopping;
    }
    return;
  }
  const auto wifi = network_snapshot();
  if (state.state == ProvisioningState::Active) {
    if (owns_access && wifi.local_access != NetworkLocalAccessState::Active) {
      revoke_clients();
      state.state = ProvisioningState::Stopping;
      (void)retire(error);
      state.error = "local_access_interrupted";
      return;
    }
    if (state.ip != wifi.local_access_ip) {
      state.ip = wifi.local_access_ip;
      ++state.sequence;
    }
    const uint32_t elapsed = time - started_at;
    state.remaining_ms = elapsed >= policy.window_timeout_ms
                             ? 0
                             : policy.window_timeout_ms - elapsed;
    if (state.progress.attempt) {
      const auto attempt = network_wifi_connection_snapshot();
      if (attempt.attempt == state.progress.attempt) {
        const std::string phase =
            network_wifi_connection_phase_name(attempt.phase);
        if (state.progress.phase != phase ||
            state.progress.error != attempt.error ||
            state.progress.ip != attempt.ip) {
          state.progress.phase = phase;
          state.progress.error = attempt.error;
          state.progress.ip = attempt.ip;
          ++state.sequence;
        }
        if (!finished &&
            attempt.phase == NetworkWifiConnectionPhase::Connected) {
          finished = true;
          finished_at = time;
        }
      }
    }
    const bool idle_auto_connected =
        auto_started && !state.progress.attempt && !state.suspends_station &&
        wifi.link == NetworkLinkState::Up && !wifi.local_ip.empty();
    if (!finished && idle_auto_connected && policy.stop_when_connected) {
      finished = true;
      finished_at = time;
    }
    if (!state.remaining_ms) state.expired = true;
    if (!state.remaining_ms ||
        (finished && policy.stop_when_connected && time - finished_at >= 5000 &&
         !device_frame_reply_pending(*owner))) {
      revoke_clients();
      state.state = ProvisioningState::Stopping;
      (void)retire(error);
    }
    return;
  }
  if (policy.mode == lunaredu::lunaros::ProvisioningMode::Disabled ||
      policy.mode == lunaredu::lunaros::ProvisioningMode::Direct ||
      wifi.link == NetworkLinkState::Up)
    return;
  if (cooldown && static_cast<int32_t>(time - next_auto_at) < 0)
    return;
  const auto attempt = network_wifi_connection_snapshot();
  if (attempt.phase == NetworkWifiConnectionPhase::Connecting ||
      attempt.phase == NetworkWifiConnectionPhase::Saving)
    return;
  bool should =
      !wifi.credentials_available && policy.start_when_credentials_missing;
  const uint32_t timeout = policy.reconnect_failure_timeout_ms
                               ? policy.reconnect_failure_timeout_ms
                               : lunaredu::lunaros::kProvisioningFailureTimeoutMs;
  if (!should && policy.start_when_connection_times_out &&
      time - wifi.disconnect_started_at_ms >= timeout &&
      wifi.reconnect_attempts >= policy.reconnect_failure_min_attempts)
    should = true;
  if (should) {
    if (begin(policy.mode, policy.suspend_station_while_provisioning, error))
      auto_started = true;
    else {
      next_auto_at = time + policy.retry_cooldown_ms;
      cooldown = true;
      state.error = error;
    }
  }
}
} // namespace lunaredu::lunaros
