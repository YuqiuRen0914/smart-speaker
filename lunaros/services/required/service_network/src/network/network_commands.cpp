#include "lunaros_network_control.h"
#include "lunaros_network_local_access.h"
#include "network/network_service_internal.h"
#include "internal/lunaros_network_local_access.h"

#include "kernel/service_executor.h"
#include "kernel/runtime.h"

namespace lunaredu::lunaros {
namespace {

enum class NetworkOperation : uint8_t {
  OpenLocalAccess,
  CloseLocalAccess,
  ApplySavedCredential,
  SetBluetooth,
  PrepareLocalGatt,
};

struct NetworkOperationRequest {
  NetworkOperation operation = NetworkOperation::CloseLocalAccess;
  NetworkLocalAccessConfig local_access;
  NetworkLocalAccessLease lease;
  NetworkLocalGattConfig gatt;
  bool enabled = false;
  uint8_t credential_index = 0;
  KernelRuntime *runtime = nullptr;
};

void execute_network_operation(void *opaque,
                               ServiceOperationResult &result) {
  auto &request = *static_cast<NetworkOperationRequest *>(opaque);
  if (!require_network_runtime(*request.runtime, result.error)) {
    result.ok = false;
    return;
  }
  NetworkService &service = network_service_internal();
  const bool cleanup = request.operation == NetworkOperation::CloseLocalAccess ||
      (request.operation == NetworkOperation::SetBluetooth && !request.enabled);
  if (!cleanup && !admit_network_work(result.error)) { result.ok=false; return; }
  switch (request.operation) {
  case NetworkOperation::OpenLocalAccess:
    result.ok = service.open_local_access_on_owner(request.local_access,request.lease,result.error);
    return;
  case NetworkOperation::CloseLocalAccess:
    result.ok = service.close_local_access_on_owner(request.lease,result.error);
    return;
  case NetworkOperation::ApplySavedCredential:
    result.ok = service.apply_saved_credential_on_owner(
        request.credential_index, result.error);
    return;
  case NetworkOperation::PrepareLocalGatt:
    result.ok=network_local_gatt_prepare(request.gatt,result.error);return;
  case NetworkOperation::SetBluetooth:
    if (request.enabled && !service.fast_snapshot().service_started) {
      result.ok=false;result.error="network_service_not_started";return;
    }
    result.ok=set_network_bluetooth_on_owner(*request.runtime,request.enabled,result.error);
    return;
  }
  result.ok = false;
  result.error = "unsupported_network_operation";
}

bool invoke_network_operation(KernelRuntime &runtime,
                              NetworkOperationRequest &request,
                              std::string &error) {
  if (!require_network_runtime(runtime, error)) return false;
  request.runtime = &runtime;
  return runtime.invoke_executor(kNetworkServiceExecutor,
                                 execute_network_operation, &request, error);
}

} // namespace

bool request_network_open_local_access(KernelRuntime &runtime,const NetworkLocalAccessConfig &config,NetworkLocalAccessLease &lease,std::string &error) {
  NetworkOperationRequest request;request.operation=NetworkOperation::OpenLocalAccess;request.local_access=config;
  const bool ok=invoke_network_operation(runtime,request,error);lease=request.lease;return ok;
}
bool request_network_close_local_access(KernelRuntime &runtime,NetworkLocalAccessLease lease,std::string &error) {
  NetworkOperationRequest request;request.operation=NetworkOperation::CloseLocalAccess;request.lease=lease;
  return invoke_network_operation(runtime,request,error);
}
bool request_network_prepare_local_gatt(KernelRuntime &runtime,const NetworkLocalGattConfig &config,std::string &error) {
  NetworkOperationRequest request;request.operation=NetworkOperation::PrepareLocalGatt;request.gatt=config;
  return invoke_network_operation(runtime,request,error);
}

bool request_network_set_bluetooth(KernelRuntime &runtime,bool enabled,std::string &error) {
  NetworkOperationRequest request;
  request.operation=NetworkOperation::SetBluetooth;request.enabled=enabled;
  return invoke_network_operation(runtime,request,error);
}

bool request_network_apply_saved_credential(KernelRuntime &runtime,
                                            uint8_t index,
                                            std::string &error) {
  NetworkOperationRequest request;
  request.operation = NetworkOperation::ApplySavedCredential;
  request.credential_index = index;
  return invoke_network_operation(runtime, request, error);
}

} // namespace lunaredu::lunaros
