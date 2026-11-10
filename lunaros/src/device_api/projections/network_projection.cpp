#include "device_api/projections/network_projection_internal.h"
#include "sdkconfig.h"

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/device_api/device_projection.h"

namespace lunaredu::lunaros {

namespace {

void fill_network_device_payload(JsonObject payload) {
  JsonObject network = payload["network"].to<JsonObject>();
  fill_network_service_bootstrap_payload(network);
  payload["local_ip"] = network["wifi_local_ip"];
}

constexpr DeviceOperationDefinition kNetworkOperations[] = {
    make_device_json_provider(
        "config.get", fill_network_config_payload,
        lunaredu::lunaros::kNetworkServiceExecutor),
    make_device_json_command(
        "config.set", handle_network_config_set,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
    make_device_json_command(
        "config.clear", handle_network_config_clear,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
    make_device_json_provider(
        "provision.status", fill_network_provisioning_status_payload,
        lunaredu::lunaros::kNetworkServiceExecutor),
    make_device_json_command(
        "provision.submit", handle_network_provision_submit,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
    device_operation_on_caller(make_device_json_command(
        "provision.scan", handle_network_provision_scan)),
#endif
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

bool setup_network_projection(std::string &error) {
  static constexpr DeviceMetadataProjection kNetworkMetadata = {
      "network.service", "network", fill_network_service_status_payload,
      fill_network_device_payload};
  return register_device_projection(
      {&lunaredu::lunaros::network_service_capability_descriptor(),
       &kNetworkProjection, &kNetworkMetadata, false},
      &error);
}

bool setup_network_projections(std::string &error) {
  if (!setup_network_projection(error)) {
    return false;
  }
  (void)deactivate_device_capability(
      network_service_capability_descriptor().id);
  return true;
}

} // namespace lunaredu::lunaros
