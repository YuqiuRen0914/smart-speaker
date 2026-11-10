#include "kernel/services/service_host_internal.h"
#include "kernel/runtime.h"

#include <cstring>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include "kernel/service_executor.h"
#endif


namespace lunaredu::lunaros {

void ServiceHost::reset() {
  settled_ = false;
  { std::lock_guard<std::mutex> lock(use_mutex_); uses_.clear(); use_count_.store(0,std::memory_order_release); }
  nodes_.clear();
  node_count_ = 0;
  start_order_.clear();
  start_order_count_ = 0;
  prepared_ = false;
  reconciling_ = false;
  {std::lock_guard<std::mutex> lock(window_mutex_);windows_.clear();window_count_.store(0,std::memory_order_release);}
  demand_revision_.fetch_add(1,std::memory_order_release);
  retiring_.clear();
}

bool ServiceHost::install(const ServiceNode &node, std::string &error) {
  error.clear();
  const ServiceDefinition &definition = node.definition;
  if(bool(node.policy_key)!=bool(node.policy_memo)){error="service_policy_contract_incomplete";return false;}
  for(size_t i=0;node.policy_memo&&i<node_count_;++i)
    if(nodes_[i].policy_memo==node.policy_memo){error="service_policy_memo_owner_conflict";return false;}
  if(node.policy_memo)node.policy_memo->valid=false;
  if (definition.id == nullptr || definition.id[0] == '\0' ||
      node.start == nullptr) {
    error = "invalid_service_node";
    return false;
  }
  if (find_node(definition.id) != nullptr) {
    error = "service_node_already_installed";
    return false;
  }
  if ((definition.execution_resource_count && !definition.execution_resources) ||
      (definition.criticality == ServiceCriticality::Required &&
       (node.demanded || node.admitted || node.on_demand || definition.execution_resource_count))) {
    error = "invalid_service_execution_policy";
    return false;
  }
  for (size_t i = 0; i < definition.execution_resource_count; ++i) {
    const char *resource = definition.execution_resources[i];
    if (!resource || !*resource || std::strlen(resource) > 64) {
      error = "invalid_service_resource"; return false;
    }
  }
  auto *stored = nodes_.append();
  if (!stored) { error = "service_node_storage_unavailable"; return false; }
  if (!registry_.register_service(definition, error)) {
    nodes_.pop_back(); return false;
  }
  static_cast<ServiceNode &>(*stored) = node;
  stored->slot = node_count_++;
  prepared_ = false;
  return true;
}

bool ServiceHost::report_ready(const std::string &service_id,
                               const char *detail, std::string &error) {
  if (!registry_.mark_ready(service_id, detail, error)) {
    return false;
  }
  return start_all(error);
}

bool ServiceHost::report_degraded(const std::string &service_id,
                                  const char *detail, std::string &error) {
  if (!registry_.mark_degraded(service_id, detail, error)) {
    return false;
  }
  return start_all(error);
}

bool ServiceHost::report_failed(const std::string &service_id,
                                const char *detail, std::string &error) {
  return registry_.mark_failed(service_id, detail, error);
}

const ServiceNode *ServiceHost::find_node(
    const std::string &service_id) const {
  return find_node(service_id.c_str());
}

const ServiceNode *ServiceHost::find_node(const char *service_id) const {
  if (!service_id) return nullptr;
  for (size_t index = 0; index < node_count_; ++index) {
    if (nodes_[index].definition.id != nullptr &&
        std::strcmp(service_id, nodes_[index].definition.id) == 0) {
      return &nodes_[index];
    }
  }
  return nullptr;
}

} // namespace lunaredu::lunaros
