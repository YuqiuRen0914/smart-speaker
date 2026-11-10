#include "lunaros_download_service_id.h"
#include "lunaros_download_lifecycle.h"
#include "lunaredu_download.h"

#include "kernel/service.h"
#include "kernel/service_executor.h"

namespace lunaredu::lunaros {
namespace {
ServiceStartResult start_download_service(void *context) {
  const auto *ports = static_cast<const DownloadServicePorts *>(context);
  if (!ports || !download_service_start(ports->transport)) {
    return {ServiceStartDisposition::Failed, "download_transport_unavailable"};
  }
  return {ServiceStartDisposition::Ready, "download_ready"};
}

bool stop_download_service(void *, std::string &error) {
  download_service_stop();
  error.clear();
  return true;
}
}  // namespace

ServiceNode download_service_node(DownloadServicePorts &ports) {
  static constexpr ServiceDependency kDependencies[] = {
      {kKernelReadyServiceId, true},
  };
  return {
      {kDownloadServiceId, "Download", kMainServiceExecutor,
       ServiceCriticality::Optional, kDependencies,
       sizeof(kDependencies) / sizeof(kDependencies[0])},
      start_download_service, stop_download_service, &ports,
  };
}
}  // namespace lunaredu::lunaros

