#include "lunaros_network_projection.h"
#include "lunaros_network_control.h"
#include "lunaros_network_jobs.h"
#include "device_api/device_operation_builders.h"
#include "lunaros_network_lifecycle.h"
#include "device_api/projection_composition.h"
#include "device_api/device_api_context.h"
#include "projections/network_projection_internal.h"
#include "projections/espnow_management_projection.h"
#include "sdkconfig.h"
#include "lunaros_network_tls.h"
#include "tls_policy_fields_json.h"

#include <esp_log.h>
#include "device_api/device_projection.h"

namespace lunaredu::lunaros {

namespace {

const char *job_state_name(NetworkJobState state) {
  switch (state) {
    case NetworkJobState::Free: return "free";
    case NetworkJobState::Queued: return "queued";
    case NetworkJobState::Running: return "running";
    case NetworkJobState::Succeeded: return "succeeded";
    case NetworkJobState::Failed: return "failed";
    case NetworkJobState::Cancelled: return "cancelled";
  }
  return "unknown";
}

const char *job_phase_name(NetworkJobPhase phase) {
  switch (phase) {
    case NetworkJobPhase::Unspecified: return "unspecified";
    case NetworkJobPhase::TransportSetup: return "transport_setup";
    case NetworkJobPhase::RequestSend: return "request_send";
    case NetworkJobPhase::ResponseHeaders: return "response_headers";
    case NetworkJobPhase::ResponseBody: return "response_body";
    case NetworkJobPhase::ResourceCleanup: return "resource_cleanup";
  }
  return "unknown";
}

void fill_network_jobs_payload(DeviceApiContext &, JsonObject payload) {
  const auto snapshot = network_job_manager_snapshot();
  payload["timing_basis"] = "elapsed_including_io_wait";
  payload["queue_timing_basis"] = "submission_or_retry_to_runner_including_backoff_and_admission";
  payload["active_count"] = snapshot.active_count;
  payload["queued_count"] = snapshot.queued_count;
  payload["running_count"] = snapshot.running_count;
  payload["retirement_window_count"] = snapshot.retirement_window_count;
  payload["external_normal_work"] = snapshot.external_normal_work;
  payload["maintenance_state"] =
      snapshot.maintenance_state == NetworkMaintenanceState::Normal ? "normal" :
      snapshot.maintenance_state == NetworkMaintenanceState::OtaPending ? "ota_pending" : "ota_exclusive";
  JsonObject metrics = payload["metrics"].to<JsonObject>();
  metrics["submitted"] = snapshot.metrics.submitted;
  metrics["completed"] = snapshot.metrics.completed;
  metrics["retried"] = snapshot.metrics.retried;
  metrics["cancelled"] = snapshot.metrics.cancelled;
  metrics["deadline_exceeded"] = snapshot.metrics.deadline_exceeded;
  metrics["runner_calls"] = snapshot.metrics.runner_calls;
  metrics["runner_elapsed_ms"] = snapshot.metrics.runner_elapsed_ms;
  metrics["maximum_run_elapsed_ms"] = snapshot.metrics.maximum_run_elapsed_ms;
  metrics["maximum_queue_wait_ms"] = snapshot.metrics.maximum_queue_wait_ms;
  JsonObject last = payload["last_run"].to<JsonObject>();
  last["id"] = snapshot.last_run.handle.id;
  last["owner"] = std::string(snapshot.last_run.owner_service_id.data());
  last["kind"] = std::string(snapshot.last_run.kind.data());
  last["elapsed_ms"] = snapshot.last_run.elapsed_ms;
  last["queue_wait_ms"] = snapshot.last_run.queue_wait_ms;
  JsonArray jobs = payload["jobs"].to<JsonArray>();
  for (size_t index = 0; index < snapshot.record_count; ++index) {
    const auto &job = snapshot.jobs[index];
    JsonObject item = jobs.add<JsonObject>();
    item["id"] = job.handle.id;
    item["owner"] = job.owner_service_id;
    item["kind"] = job.kind;
    item["state"] = job_state_name(job.state);
    item["phase"] = job_phase_name(job.phase);
    item["class"] = job.job_class == NetworkJobClass::Retirement ? "retirement" :
        job.job_class == NetworkJobClass::Ota ? "ota" : "normal";
    item["yieldable_transport_setup"] = job.yieldable_transport_setup;
    item["attempts"] = job.attempts;
    item["started_at_ms"] = job.started_at_ms;
    item["last_run_elapsed_ms"] = job.last_run_elapsed_ms;
    item["queued_at_ms"] = job.queued_at_ms;
    item["last_queue_wait_ms"] = job.last_queue_wait_ms;
    item["cancellation_requested"] = job.cancellation_requested;
  }
}

void fill_network_device_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  JsonObject network = payload["network"].to<JsonObject>();
  fill_network_service_bootstrap_payload(device, network);
  payload["local_ip"] = network["wifi_local_ip"];
}

DeviceOperationResult handle_network_bluetooth_set(DeviceApiContext &device, JsonVariantConst request, JsonObject payload) {
  if(!request["enabled"].is<bool>())return device_operation_error("invalid_bluetooth_enabled",400,"network_bluetooth_error");
  std::string error;
  if(!request_network_set_bluetooth(device.runtime,request["enabled"].as<bool>(),error))
    return device_operation_error(error.c_str(),503,"network_bluetooth_error");
  const auto state = network_bluetooth_control_snapshot();
  payload["accepted"] = true;
  payload["requested_enabled"] = request["enabled"].as<bool>();
  payload["enabled"] = state.enabled;
  payload["stop_pending"] = state.stop_pending;
  payload["error"] = state.error;
  return device_operation_ack();
}
void fill_tls_policy(DeviceApiContext &, JsonObject payload) {
  const auto policy = network_tls_saved_policy();
  payload["origin"] = policy->origin;
  payload["mode"] = policy->mode;
  payload["ca_present"] = !policy->ca_pem.empty();
  payload["maximum_ca_bytes"] = kNetworkTlsCaMaxBytes;
}
DeviceOperationResult set_tls_policy(DeviceApiContext &device, JsonVariantConst request, JsonObject payload) {
  NetworkTlsPolicy policy;
  if (!internal::read_tls_policy_fields(request, policy) ||
      (!request["keep_ca"].isNull() && !request["keep_ca"].is<bool>()))
    return device_operation_error("tls_invalid_policy_or_ca", 400);
  if (request["keep_ca"] | false) {
    const auto old = network_tls_saved_policy();
    std::string normalized;
    if (policy.mode != "custom_ca" || !policy.ca_pem.empty() ||
        !network_tls_normalize_origin(policy.origin, normalized) || normalized != old->origin || old->mode != "custom_ca")
      return device_operation_error("tls_invalid_policy_or_ca", 400);
    policy.ca_pem = old->ca_pem;
  }
  std::string error;
  if (!network_tls_save_policy(std::move(policy), error))
    return device_operation_error(error.c_str(), error == "tls_policy_busy" ? 409 : error == "tls_save_failed" ? 503 : 400);
  fill_tls_policy(device, payload);
  payload["restart_required"] = true;
  return device_operation_ack();
}
constexpr const char *kTlsSchema = R"json({"type":"object","additionalProperties":false,"required":["origin","mode"],"properties":{"origin":{"type":"string","maxLength":256},"mode":{"type":"string","enum":["standard","custom_ca","skip_verify"]},"ca_pem":{"type":"string","maxLength":4096},"keep_ca":{"type":"boolean"}}})json";
constexpr DeviceOperationDefinition kNetworkOperations[] = {
    make_device_json_provider("wifi.status",fill_network_wifi_status,kNetworkServiceExecutor),
    make_device_json_command("wifi.connect",handle_network_wifi_connect,device_operation_on_executor(kNetworkServiceExecutor)),
    make_device_json_command("wifi.cancel",handle_network_wifi_cancel,device_operation_on_executor(kNetworkServiceExecutor)),
    make_device_json_command("wifi.forget",handle_network_wifi_forget,device_operation_on_executor(kNetworkServiceExecutor)),
    device_operation_on_caller(make_device_json_command("wifi.scan",handle_network_wifi_scan)),
    make_device_json_provider("tls.get", fill_tls_policy, kNetworkServiceExecutor),
    device_operation_schemas(make_device_json_command("tls.set", set_tls_policy,
        device_operation_body_limit(6144, device_operation_on_executor(kNetworkServiceExecutor))), kTlsSchema, nullptr),
    device_operation_diagnostic_access(device_operation_on_caller(
        make_device_json_provider("jobs.get", fill_network_jobs_payload)),
        DeviceDiagnosticAccess::Session),
    device_operation_remote(make_device_json_provider(
        "config.get", fill_network_config_payload,
        lunaredu::lunaros::kNetworkServiceExecutor), DeviceRemoteAccess::Query),
    make_device_json_command(
        "config.set", handle_network_config_set,
        device_operation_body_limit(kDeviceConfigBodyBytes, device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor))),
    make_device_json_command(
        "config.clear", handle_network_config_clear,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
    make_device_json_command("bluetooth.set",handle_network_bluetooth_set,
        device_operation_on_executor(kNetworkServiceExecutor)),
    device_operation_diagnostic_access(device_operation_on_caller(
        make_device_json_provider("espnow.management.status",internal::fill_espnow_management)),DeviceDiagnosticAccess::Session),
    device_operation_diagnostic_access(make_device_json_command("espnow.management.open",internal::open_espnow_management,
        device_operation_body_limit(512)),DeviceDiagnosticAccess::Session),
    device_operation_diagnostic_access(make_device_json_command("espnow.management.close",internal::close_espnow_management),DeviceDiagnosticAccess::Session),

};

const DeviceModuleProjectionMetadata kNetworkModule = {
    "network",
    "network",
};

const DeviceModuleProjection kNetworkProjection = {
    "network_service",
    &kNetworkModule,
    kNetworkOperations,
    sizeof(kNetworkOperations) / sizeof(kNetworkOperations[0]),
};

} // namespace

static bool install_network_projection(DeviceProjectionRegistry &registry, std::string &error) {
  static constexpr DeviceMetadataProjection kNetworkMetadata = {
      "network.service", "network", fill_network_service_status_payload,
      fill_network_device_payload};
  if (!registry.register_projection(
      {&lunaredu::lunaros::network_service_capability_descriptor(),
       &kNetworkProjection, &kNetworkMetadata, false}, &error)) {
    return false;
  }
  (void)registry.context().capabilities.deactivate(
      network_service_capability_descriptor().id);
  return true;
}

const StandardProjectionContribution standard_projection::network{"network", install_network_projection};

} // namespace lunaredu::lunaros
