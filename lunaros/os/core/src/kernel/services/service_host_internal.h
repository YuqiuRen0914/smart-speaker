#pragma once

#include <array>
#include <atomic>
#include <mutex>

#include "kernel/services/service_registry_internal.h"
#include "kernel/execution/service_executor_internal.h"
#include "kernel/service.h"

namespace lunaredu::lunaros {

using ServiceLifecycleAction = bool (*)(void *context, std::string &error);
using ServiceLifecycleDispatcher = bool (*)(
    const char *owner_executor, ServiceLifecycleAction action, void *context,
    std::string &error);

class ServiceHost {
public:
  explicit ServiceHost(ServiceRegistry &registry,
                       ServiceExecutorRegistry *executors = nullptr)
      : registry_(registry), executors_(executors) {}
  ServiceHost(ServiceRegistry &registry, ServiceLifecycleDispatcher dispatcher)
      : registry_(registry), dispatcher_(dispatcher) {}

  void reset();
  bool install(const ServiceNode &node, std::string &error);
  bool start(const std::string &service_id, std::string &error);
  bool restart(const std::string &service_id, std::string &error);
  bool activate(const std::string &service_id, std::string &error);
  bool start_all(std::string &error);
  bool report_ready(const std::string &service_id, const char *detail,
                    std::string &error);
  bool report_degraded(const std::string &service_id, const char *detail,
                       std::string &error);
  bool report_failed(const std::string &service_id, const char *detail,
                     std::string &error);
  bool stop_all(std::string &error);
  bool prepare(std::string &error);
  bool reconcile(std::string &error);
  bool reserve_resource(const char *resource, const char *owner,
                        ServiceResourceWindow &window, std::string &error);
  bool release_resource(ServiceResourceWindow &window, std::string &error);
  bool request_use(const char *service, const char *owner, ServiceUse &, std::string &);
  bool release_use(const char *owner, ServiceUse &, std::string &);
  void describe_uses(ServiceRegistrySnapshot &) const;

private:
  bool publish_start_result(const std::string &,const ServiceStartResult &,bool activation,std::string &);
  bool stop_one(const std::string &service_id, std::string &error);
  const ServiceNode *find_node(const std::string &service_id) const;
  const ServiceNode *find_node(const char *service_id) const;
  bool dispatch(const ServiceNode &node, ServiceLifecycleAction action,
                void *context, std::string &error) const;

  ServiceRegistry &registry_;
  ServiceExecutorRegistry *executors_ = nullptr;
  ServiceLifecycleDispatcher dispatcher_ = nullptr;
  struct Node : ServiceNode { size_t slot = 0; };
  RegistrationTable<Node> nodes_;
  static size_t node_slot(const ServiceNode &node) { return static_cast<const Node &>(node).slot; }
  size_t node_count_ = 0;
  // Nodes occupy stable slots until reset. Prepare computes their dependency
  // order once; installation invalidates it before any subsequent use.
  RegistrationArray<size_t> start_order_, registry_slots_;
  RegistrationArray<RegistrationArray<size_t>> required_nodes_, consuming_nodes_;
  size_t start_order_count_ = 0;
  bool prepared_ = false;
  bool reconciling_ = false;
  // Cache only a successfully settled graph. Demand predicates are still
  // observed on every reconciliation; their previous answers are never reused.
  bool settled_ = false;
  uint32_t settled_registry_revision_ = 0,settled_demand_revision_=0;
  std::atomic<uint32_t> demand_revision_{0};
  RegistrationArray<uint8_t> retiring_;
  struct ResourceWindow {
    std::string resource, owner;
    uint64_t generation = 0;
  };
  RegistrationArray<ResourceWindow> windows_;
  std::atomic<unsigned> window_count_{0};
  mutable std::mutex window_mutex_;
  uint64_t next_window_generation_ = 0;
  bool resource_blocked(const ServiceNode &) const;
  bool resource_dependency_blocked(const ServiceNode &, size_t depth=0) const;
  struct Use { const ServiceNode *node = nullptr; std::string owner; uint64_t generation = 0; };
  RegistrationArray<Use> uses_;
  std::atomic<unsigned> use_count_{0};
  mutable std::mutex use_mutex_;
  uint64_t next_use_generation_ = 0;
  bool explicitly_used(const ServiceNode &) const;
  using DemandObservations = RegistrationArray<uint8_t>;
  DemandObservations observations_, requested_observations_, settled_requests_;
  RegistrationArray<ServiceState> observed_states_;
  bool requested(const ServiceNode &, size_t depth = 0,DemandObservations * = nullptr) const;
  bool requested_impl(const ServiceNode &,size_t,DemandObservations *) const;
  bool execution_wanted(const ServiceNode &, size_t depth = 0,
                        DemandObservations *observations = nullptr) const;
  bool dependencies_ready(const ServiceNode &) const;
  bool waits_for_deferred_dependency(const ServiceNode &, size_t depth = 0) const;
};


} // namespace lunaredu::lunaros
