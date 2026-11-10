#pragma once

#include <array>

#include "kernel/services/service_registry_internal.h"
#include "lunaredu_lunaros/kernel/service_host.h"

namespace lunaredu::lunaros {

using ServiceLifecycleAction = bool (*)(void *context, std::string &error);
using ServiceLifecycleDispatcher = bool (*)(
    const char *owner_executor, ServiceLifecycleAction action, void *context,
    std::string &error);

class ServiceHost {
public:
  explicit ServiceHost(ServiceRegistry &registry,
                       ServiceLifecycleDispatcher dispatcher = nullptr)
      : registry_(registry), dispatcher_(dispatcher) {}

  void reset();
  bool install(const ServiceNode &node, std::string &error);
  bool start(const std::string &service_id, std::string &error);
  bool activate(const std::string &service_id, std::string &error);
  bool start_all(std::string &error);
  bool report_ready(const std::string &service_id, const char *detail,
                    std::string &error);
  bool report_degraded(const std::string &service_id, const char *detail,
                       std::string &error);
  bool report_failed(const std::string &service_id, const char *detail,
                     std::string &error);
  bool stop_all(std::string &error);

private:
  const ServiceNode *find_node(const std::string &service_id) const;
  bool dispatch(const ServiceNode &node, ServiceLifecycleAction action,
                void *context, std::string &error) const;

  ServiceRegistry &registry_;
  ServiceLifecycleDispatcher dispatcher_ = nullptr;
  std::array<ServiceNode, kServiceCapacity> nodes_{};
  size_t node_count_ = 0;
};

ServiceHost &service_host_internal();
void reset_service_host_internal();

} // namespace lunaredu::lunaros
