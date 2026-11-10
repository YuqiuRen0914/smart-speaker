#include "lunaros_network_service_id.h"
#include "lunaros_network_policy.h"
#include "kernel/runtime.h"
#include "lunaros_network_lifecycle.h"
#include "network/network_job_manager_internal.h"
#include "network/network_scan_internal.h"
#include "network/network_service_internal.h"
#include "bluetooth_profiles/lifecycle.h"
#include "network/bluetooth/local_gatt_internal.h"

#include "lunaros_network_jobs.h"
#include "kernel/service_executor.h"
#include "kernel/service.h"
#include "device_api/device_api_context.h"

namespace lunaredu::lunaros {
namespace {

ServiceStartResult start_network_runtime(void *context) {
  auto &device = *static_cast<DeviceApiContext *>(context);
  auto &runtime = device.runtime;
  std::string error;
  if (!require_network_runtime(runtime, error)) {
    return {ServiceStartDisposition::Failed, error};
  }
  if (!network_service_internal().snapshot().driver_bound) {
    return {ServiceStartDisposition::Failed,
            "network_platform_driver_not_bound"};
  }
  if (!start_network_bluetooth_profiles(runtime,error)) return {ServiceStartDisposition::Failed,error};
  if (!network_service_internal().start(error)) {
    stop_network_bluetooth_profiles();
    return {ServiceStartDisposition::Failed,
            error.empty() ? "network_start_failed" : error};
  }
  if (!set_network_capability_available(device.model.capabilities, true, error)) {
    stop_network_bluetooth_profiles();
    std::string stop_error;
    (void)network_service_internal().stop(stop_error);
    return {ServiceStartDisposition::Failed,
            error.empty() ? "network_capability_activation_failed" : error};
  }
  return {ServiceStartDisposition::Ready, "network_executor_ready"};
}

bool stop_network_runtime(void *context, std::string &error) {
  auto &device = *static_cast<DeviceApiContext *>(context);
  auto &runtime = device.runtime;
  if (!require_network_runtime(runtime, error)) return false;
  network_service_internal().begin_retirement();
  network_job_manager_internal().close_admission();
  stop_network_bluetooth_profiles();
  const NetworkJobManagerSnapshot jobs = network_job_manager_internal().snapshot();
  if (jobs.active_count != 0) {
    error = "network_jobs_still_active";
    return false;
  }
  const NetworkJobPolicy policy = network_service_internal().job_policy();
  if (!network_service_internal().stop(error)) {
    return false;
  }
  std::string projection_error;
  (void)set_network_capability_available(device.model.capabilities, false, projection_error);
  if (!reset_network_job_manager_internal(policy)) {
    error = "network_job_manager_not_quiescent";
    return false;
  }
  error.clear();
  return true;
}

} // namespace

bool configure_network_runtime(KernelRuntime &runtime, const NetworkPolicy &policy,
                               const char *timezone, std::string &error) {
  // Never erase live radio state or job callbacks during recomposition.
  const auto jobs = network_job_manager_internal().snapshot();
  if (network_service_internal().snapshot().service_started ||
      network_service_internal().retirement_pending() ||
      jobs.active_count != 0 || jobs.retirement_window_count != 0) {
    error = "network_runtime_not_quiescent";
    return false;
  }
  if (!reset_network_local_gatt_binding(error)) return false;
  if (!reset_network_bluetooth_profiles(error)) return false;
  reset_network_service_internal();
  reset_network_wifi_scan_internal();
  reset_network_wifi_survey_internal();
  if (!setup_network_job_manager_internal(policy.jobs, error)) {
    return false;
  }
  if (!configure_network_service(runtime, policy, timezone, error)) {
    return false;
  }
  error.clear();
  return true;
}

ServiceNode network_service_node(DeviceApiContext &device) {
  static constexpr ServiceDependency kDependencies[] = {
      {kKernelReadyServiceId, true},
  };
  return {
      {kNetworkRuntimeServiceId, "Network Runtime", kNetworkServiceExecutor,
       ServiceCriticality::Optional, kDependencies,
       sizeof(kDependencies) / sizeof(kDependencies[0])},
      start_network_runtime,
      stop_network_runtime,
      &device,
  };
}

} // namespace lunaredu::lunaros
