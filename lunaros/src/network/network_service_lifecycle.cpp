#include "network/network_job_manager_internal.h"
#include "network/network_scan_internal.h"
#include "network/network_service_internal.h"
#include "lunaredu_lunaros/network/network.h"

#include "lunaredu_lunaros/network/network_jobs.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "lunaredu_lunaros/kernel/service_host.h"
#include "lunaredu_lunaros/kernel/service_ids.h"

namespace lunaredu::lunaros {
namespace {

ServiceStartResult start_network_runtime(void *) {
  if (!network_service_internal().snapshot().driver_bound) {
    return {ServiceStartDisposition::Failed,
            "network_platform_driver_not_bound"};
  }
  std::string error;
  if (!network_service_internal().start(error)) {
    return {ServiceStartDisposition::Failed,
            error.empty() ? "network_start_failed" : error};
  }
  if (!set_network_capability_available(true, error)) {
    std::string stop_error;
    (void)network_service_internal().stop(stop_error);
    return {ServiceStartDisposition::Failed,
            error.empty() ? "network_capability_activation_failed" : error};
  }
  return {ServiceStartDisposition::Ready, "network_executor_ready"};
}

bool stop_network_runtime(void *, std::string &error) {
  const NetworkJobManagerSnapshot jobs = network_job_manager_internal().snapshot();
  if (jobs.active_count != 0) {
    error = "network_jobs_still_active";
    return false;
  }
  const NetworkJobPolicy policy = network_service_internal().policy().jobs;
  if (!network_service_internal().stop(error)) {
    return false;
  }
  std::string projection_error;
  (void)set_network_capability_available(false, projection_error);
  reset_network_job_manager_internal(policy);
  error.clear();
  return true;
}

} // namespace

bool configure_network_runtime(const NetworkPolicy &policy,
                               const char *timezone, std::string &error) {
  reset_network_service_internal();
  reset_network_wifi_scan_internal();
  if (!setup_network_job_manager_internal(policy.jobs, error)) {
    return false;
  }
  if (!configure_network_service(policy, timezone, error)) {
    return false;
  }
  error.clear();
  return true;
}

ServiceNode network_service_node() {
  static constexpr ServiceDependency kDependencies[] = {
      {kSettingsServiceId, true},
  };
  return {
      {kNetworkRuntimeServiceId, "Network Runtime", kNetworkServiceExecutor,
       ServiceCriticality::Optional, kDependencies,
       sizeof(kDependencies) / sizeof(kDependencies[0])},
      start_network_runtime,
      stop_network_runtime,
      nullptr,
  };
}

} // namespace lunaredu::lunaros
