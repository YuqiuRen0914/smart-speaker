#include "network/network_service_internal.h"
#include "lunaredu_lunaros/network/network_commands.h"

#include "lunaredu_lunaros/kernel/service_executor.h"

namespace lunaredu::lunaros {
namespace {

enum class NetworkOperation : uint8_t {
  StartProvisioning,
  StopProvisioning,
  ApplySavedCredential,
};

struct NetworkOperationRequest {
  NetworkOperation operation = NetworkOperation::StopProvisioning;
  NetworkProvisioningMode provisioning_mode = NetworkProvisioningMode::Disabled;
  bool suspend_station = false;
  uint8_t credential_index = 0;
};

void execute_network_operation(NetworkOperationRequest &request,
                               ServiceOperationResult &result) {
  NetworkService &service = network_service_internal();
  switch (request.operation) {
  case NetworkOperation::StartProvisioning:
    result.ok = service.start_provisioning_on_owner(
        request.provisioning_mode, request.suspend_station, result.error);
    return;
  case NetworkOperation::StopProvisioning:
    result.ok = service.stop_provisioning_on_owner(result.error);
    return;
  case NetworkOperation::ApplySavedCredential:
    result.ok = service.apply_saved_credential_on_owner(
        request.credential_index, result.error);
    return;
  }
  result.ok = false;
  result.error = "unsupported_network_operation";
}

bool invoke_network_operation(NetworkOperationRequest &request,
                              std::string &error) {
  return invoke_service_operation(kNetworkServiceExecutor,
                                  execute_network_operation, request, error);
}

} // namespace

bool request_network_start_provisioning(NetworkProvisioningMode mode,
                                        bool suspend_station,
                                        std::string &error) {
  if (mode == NetworkProvisioningMode::Disabled) {
    error = "network_provisioning_mode_disabled";
    return false;
  }
  NetworkOperationRequest request;
  request.operation = NetworkOperation::StartProvisioning;
  request.provisioning_mode = mode;
  request.suspend_station = suspend_station;
  return invoke_network_operation(request, error);
}

bool request_network_stop_provisioning(std::string &error) {
  NetworkOperationRequest request;
  request.operation = NetworkOperation::StopProvisioning;
  return invoke_network_operation(request, error);
}

bool request_network_apply_saved_credential(uint8_t index,
                                            std::string &error) {
  NetworkOperationRequest request;
  request.operation = NetworkOperation::ApplySavedCredential;
  request.credential_index = index;
  return invoke_network_operation(request, error);
}

} // namespace lunaredu::lunaros
