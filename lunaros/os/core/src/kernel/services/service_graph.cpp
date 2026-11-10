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

bool ServiceHost::prepare(std::string &error) {
  settled_ = false;
  prepared_ = false;
  const auto allocation_failed = [&] { error = "service_graph_storage_unavailable"; return false; };
  if (!start_order_.resize(node_count_) || !registry_slots_.resize(node_count_) ||
      !required_nodes_.resize(node_count_) || !consuming_nodes_.resize(node_count_) ||
      !retiring_.resize(node_count_) || !observations_.resize(node_count_) ||
      !requested_observations_.resize(node_count_) || !settled_requests_.resize(node_count_) ||
      !observed_states_.resize(registry_.size())) return allocation_failed();
  for (auto &edges : required_nodes_) (void)edges.resize(0);
  for (auto &edges : consuming_nodes_) (void)edges.resize(0);
  error.clear();
  if (!registry_.validate_graph(error)) return false;
  RegistrationArray<uint8_t> emitted;
  if (!emitted.resize(node_count_)) return allocation_failed();
  start_order_count_ = 0;
  while (start_order_count_ < node_count_) {
    bool progressed = false;
    for (size_t index = 0; index < node_count_; ++index) {
      if (emitted[index]) continue;
      const auto &service = nodes_[index].definition;
      bool ready = true;
      for (size_t dependency = 0; dependency < service.dependency_count;
           ++dependency) {
        if (!service.dependencies[dependency].required) continue;
        bool found = false;
        for (size_t candidate = 0; candidate < node_count_;
             ++candidate) {
          if (nodes_[candidate].definition.id != nullptr &&
              service.dependencies[dependency].id != nullptr &&
              std::strcmp(nodes_[candidate].definition.id,
                          service.dependencies[dependency].id) == 0) {
            found = true;
            ready = ready && emitted[candidate];
            break;
          }
        }
        if (!found) ready = false;
      }
      if (!ready) continue;
      emitted[index] = true;
      start_order_[start_order_count_++] = index;
      progressed = true;
    }
    if (!progressed) {
      start_order_count_ = 0;
      error = "service_dependency_cycle";
      return false;
    }
  }
  prepared_ = true;
  for(size_t slot=0;slot<node_count_;++slot){
    const auto &node=nodes_[slot];const int registered=registry_.state_slot(node.definition.id);
    if(registered<0){prepared_=false;error="service_registry_slot_missing";return false;}
    registry_slots_[slot]=size_t(registered);
    for(size_t i=0;i<node.definition.dependency_count;++i){
      const auto &dependency=node.definition.dependencies[i];if(!dependency.required)continue;
      const auto *provider=find_node(dependency.id);
      if(!provider){prepared_=false;error="service_dependency_missing";return false;}
      auto &required = required_nodes_[slot];
      auto &consumers = consuming_nodes_[node_slot(*provider)];
      if (!required.resize(required.size() + 1) || !consumers.resize(consumers.size() + 1)) {
        prepared_ = false; return allocation_failed();
      }
      required[required.size() - 1] = node_slot(*provider);
      consumers[consumers.size() - 1] = slot;
    }
  }
  return true;
}

bool ServiceHost::waits_for_deferred_dependency(const ServiceNode &node, size_t depth) const {
  if (depth >= node_count_) return false;
  bool waiting = false;
  for (size_t i = 0; i < node.definition.dependency_count; ++i) {
    const auto &dependency = node.definition.dependencies[i];
    if (!dependency.required || registry_.service_available(dependency.id)) continue;
    const auto *provider = find_node(dependency.id); ServiceState state;
    if (!provider || !registry_.state(dependency.id, state)) return false;
    if (state == ServiceState::Starting || (state == ServiceState::Registered &&
        waits_for_deferred_dependency(*provider, depth + 1))) waiting = true;
    else return false;
  }
  return waiting;
}
bool ServiceHost::dependencies_ready(const ServiceNode &node) const {
  for (size_t i = 0; i < node.definition.dependency_count; ++i) {
    const auto &dependency = node.definition.dependencies[i];
    if (dependency.required && !registry_.service_available(dependency.id)) return false;
  }
  return true;
}
bool ServiceHost::start_all(std::string &error) {
  if (!prepared_ && !prepare(error)) return false;
  for (size_t i = 0; i < start_order_count_; ++i) {
    const auto *node = &nodes_[start_order_[i]]; ServiceState state;
    // Admission policy is only relevant to candidates for a new start. Live
    // demand for ready services belongs to reconciliation's retirement pass;
    // repeating recursive policy observation here adds no start decision.
    if (!registry_.state(node->definition.id, state) ||
        state != ServiceState::Registered || !execution_wanted(*node) ||
        !dependencies_ready(*node)) continue;
    std::string detail;
    if (!start(node->definition.id, detail) && node->definition.criticality == ServiceCriticality::Required) {
      error = detail; return false;
    }
  }
  for (size_t i = 0; i < node_count_; ++i) {
    const auto &node = nodes_[i]; ServiceState state;
    if (node.definition.criticality == ServiceCriticality::Required &&
        registry_.state(node.definition.id, state) && state == ServiceState::Registered &&
        !waits_for_deferred_dependency(node)) {
      error = "required_service_start_blocked:" + std::string(node.definition.id); return false;
    }
  }
  error.clear(); return true;
}

bool ServiceHost::resource_blocked(const ServiceNode &node) const {
  // Immutable composition metadata: a node with no execution resources cannot
  // match any reservation. Avoid taking a cross-core mutex on every owner tick.
  if (!node.definition.execution_resource_count ||
      !window_count_.load(std::memory_order_acquire)) return false;
  std::lock_guard<std::mutex> lock(window_mutex_);
  for (size_t i = 0; i < node.definition.execution_resource_count; ++i)
    for (const auto &window : windows_)
      if (window.generation && window.resource == node.definition.execution_resources[i])
        return true;
  return false;
}
bool ServiceHost::resource_dependency_blocked(const ServiceNode &node,size_t depth) const {
  if(depth>=node_count_ || resource_blocked(node))return true;
  for(size_t i=0;i<node.definition.dependency_count;++i){
    const auto &dependency=node.definition.dependencies[i];
    if(!dependency.required)continue;
    const auto *provider=find_node(dependency.id);
    if(provider&&resource_dependency_blocked(*provider,depth+1))return true;
  }
  return false;
}

bool ServiceHost::explicitly_used(const ServiceNode &node) const {
  if (!use_count_.load(std::memory_order_acquire)) return false;
  std::lock_guard<std::mutex> lock(use_mutex_);
  for (const auto &use : uses_) if (use.generation && use.node == &node) return true;
  return false;
}
void ServiceHost::describe_uses(ServiceRegistrySnapshot &snapshot) const {
  std::lock_guard<std::mutex> lock(use_mutex_);
  for(size_t i=0;i<snapshot.service_count;++i){
    auto &service=snapshot.services[i];const auto *node=find_node(service.id);
    if(!node)continue;
    service.on_demand=node->on_demand;service.has_admission_policy=node->admitted!=nullptr;
    for(const auto &use:uses_)if(use.generation&&use.node==node)++service.explicit_uses;
  }
}
bool ServiceHost::request_use(const char *id, const char *owner,
    ServiceUse &token, std::string &error) {
  error.clear();
  const auto *node=find_node(id);
  if (!node) { error="service_not_found"; return false; }
  if (!owner || !*owner || std::strlen(owner)>64) { error="invalid_service_use_owner"; return false; }
  std::lock_guard<std::mutex> lock(use_mutex_);
  if (token.generation) {
    for (const auto &use : uses_) if (use.generation==token.generation) {
      if (use.node==node && use.owner==owner) return true;
      error="service_use_owner_mismatch"; return false;
    }
    error="stale_service_use"; return false;
  }
  if (next_use_generation_ == UINT64_MAX) { error = "service_use_generation_exhausted"; return false; }
  Use *available = nullptr;
  for (auto &use : uses_) if (!use.generation) { available = &use; break; }
  if (!available) {
    if (!uses_.resize(uses_.size() + 1)) { error = "service_use_storage_unavailable"; return false; }
    available = &uses_[uses_.size() - 1];
  }
  available->node = node; available->owner = owner;
  token.generation = available->generation = ++next_use_generation_;
  use_count_.fetch_add(1,std::memory_order_release); demand_revision_.fetch_add(1,std::memory_order_release); return true;
}
bool ServiceHost::release_use(const char *owner, ServiceUse &token, std::string &error) {
  error.clear(); if (!token.generation) return true;
  std::lock_guard<std::mutex> lock(use_mutex_);
  for (auto &use : uses_) if (use.generation==token.generation) {
    if (!owner || use.owner!=owner) { error="service_use_owner_mismatch"; return false; }
    use={}; token.generation=0; use_count_.fetch_sub(1,std::memory_order_release); demand_revision_.fetch_add(1,std::memory_order_release); return true;
  }
  error="stale_service_use"; return false;
}
bool ServiceHost::requested(const ServiceNode &node,size_t depth,DemandObservations *observations) const {
  if(!observations||depth>=node_count_)return requested_impl(node,depth,observations);
  auto &value=(*observations)[node_slot(node)];
  if(value)return value==2;
  value=1;const bool wanted=requested_impl(node,depth,observations);value=wanted?2:1;return wanted;
}
bool ServiceHost::requested_impl(const ServiceNode &node, size_t depth,DemandObservations *observations) const {
  if (depth>=node_count_ || resource_blocked(node)) return false;
  bool admitted=true,demanded=false,have_policy=false;
  if(node.policy_key&&node.policy_memo){
    const auto key=node.policy_key(node.context);auto &memo=*node.policy_memo;
    const auto revision=registry_.revision(),demand=demand_revision_.load(std::memory_order_acquire);
    if(!memo.valid||memo.owner!=&node||!(memo.key==key)||memo.registry_revision!=revision||memo.demand_revision!=demand){
      memo.admitted=!node.admitted||node.admitted(node.context);
      memo.demanded=memo.admitted&&(node.demanded?node.demanded==node.admitted||node.demanded(node.context):!node.on_demand);
      memo.key=key;memo.owner=&node;memo.registry_revision=revision;memo.demand_revision=demand;memo.valid=true;
    }
    admitted=memo.admitted;demanded=memo.demanded;have_policy=true;
  }
  if(!admitted||(!have_policy&&node.admitted&&!node.admitted(node.context)))return false;
  // Ordinary resident services are already requested. Explicit use only adds
  // demand, so looking through its synchronized registry cannot change this
  // result. Resource/admission constraints above still apply on every call.
  if (!node.demanded && !node.on_demand) return true;
  // A shared admission/demand predicate was already observed successfully.
  if(node.demanded && node.demanded==node.admitted)return true;
  if (explicitly_used(node) || (have_policy?demanded:(node.demanded ? node.demanded(node.context) : !node.on_demand))) return true;
  // Only explicitly on-demand providers inherit consumer demand. Existing
  // policy callbacks remain constraints until their owner opts into this model.
  if (!node.on_demand) return false;
  if (prepared_) {
    for (const auto index : consuming_nodes_[node_slot(node)])
      if (requested(nodes_[index], depth + 1, observations)) return true;
    return false;
  }
  for (size_t i=0;i<node_count_;++i) {
    const auto &consumer=nodes_[i];
    for (size_t d=0;d<consumer.definition.dependency_count;++d) {
      const auto &dependency=consumer.definition.dependencies[d];
      if (dependency.required && dependency.id &&
          std::strcmp(dependency.id,node.definition.id)==0 && requested(consumer,depth+1,observations)) return true;
    }
  }
  return false;
}

bool ServiceHost::execution_wanted(const ServiceNode &node, size_t depth,
                                   DemandObservations *observations) const {
  if (depth >= node_count_) return false;
  const auto slot = node_slot(node);
  if (observations && (*observations)[slot])
    return (*observations)[slot] == 2;
  // Memoize only within an unchanged retirement sweep. No demand survives a
  // lifecycle callback or owner tick; start admission still reads fresh policy.
  if (observations) (*observations)[slot] = 1;
  if (!requested(node)) return false;
  if(prepared_){
    for (const auto index : required_nodes_[slot])
      if (!execution_wanted(nodes_[index], depth + 1, observations)) return false;
  }else for(size_t i=0;i<node.definition.dependency_count;++i){
    const auto &dependency=node.definition.dependencies[i];if(!dependency.required)continue;
    const auto *provider=find_node(dependency.id);
    if(!provider||!execution_wanted(*provider,depth+1,observations))return false;
  }
  if (observations) (*observations)[slot] = 2;
  return true;
}

bool ServiceHost::reserve_resource(const char *resource, const char *owner,
    ServiceResourceWindow &token, std::string &error) {
  error.clear();
  if (!resource || !*resource || std::strlen(resource) > 64 ||
      !owner || !*owner || std::strlen(owner) > 64) {
    error = "invalid_service_resource_window"; return false;
  }
  ResourceWindow *held = nullptr;
  {
  std::lock_guard<std::mutex> lock(window_mutex_);
  for (auto &window : windows_) {
    if (window.generation && window.generation == token.generation) held = &window;
    else if (window.generation && window.resource == resource) {
      error = "service_resource_owned_by:" + window.owner; return false;
    }
  }
  if (token.generation) {
    if (!held || held->owner != owner || held->resource != resource) {
      error = "stale_service_resource_window"; return false;
    }
  } else {
    for (auto &window : windows_) if (!window.generation) { held = &window; break; }
    if (next_window_generation_ == UINT64_MAX) {
      error = "service_resource_window_generation_exhausted"; return false;
    }
    if (!held) {
      if (!windows_.resize(windows_.size() + 1)) { error = "service_resource_window_storage_unavailable"; return false; }
      held = &windows_[windows_.size() - 1];
    }
    held->resource = resource; held->owner = owner;
    held->generation = token.generation = ++next_window_generation_;
    window_count_.fetch_add(1,std::memory_order_release); demand_revision_.fetch_add(1,std::memory_order_release);
  }
  }
  // Reservation closes admission immediately, including during boot. Owner
  // reconciliation performs retirement separately; never block a UI request.
  for (size_t i = 0; i < node_count_; ++i) {
    const auto &node = nodes_[i]; ServiceState state;
    if (!registry_.state(node.definition.id, state) || state == ServiceState::Registered ||
        state == ServiceState::Stopped) continue;
    if (resource_dependency_blocked(node)) {
      error = "service_resource_pending:" + std::string(node.definition.id); return false;
    }
  }
  return true;
}

bool ServiceHost::release_resource(ServiceResourceWindow &token, std::string &error) {
  std::lock_guard<std::mutex> lock(window_mutex_);
  error.clear();
  if (!token.generation) return true;
  for (auto &window : windows_) if (window.generation == token.generation) {
    window = {}; token.generation = 0; window_count_.fetch_sub(1,std::memory_order_release); demand_revision_.fetch_add(1,std::memory_order_release); return true;
  }
  error = "stale_service_resource_window"; return false;
}

bool ServiceHost::reconcile(std::string &error) {
  if (reconciling_) { error = "service_reconciliation_busy"; return false; }
  if (!prepared_ && !prepare(error)) return false;
  reconciling_ = true;
  struct Guard { bool &flag; ~Guard() { flag = false; } } guard{reconciling_};
  std::string pending;
  auto &observations = observations_; observations.fill(0);
  const auto revision_before = registry_.revision();
  const auto demand_before=demand_revision_.load(std::memory_order_acquire);
  auto &requested_observations = requested_observations_; requested_observations.fill(0);
  for (size_t i=0;i<node_count_;++i){
    // Share this observation pass only. Versions are checked once afterwards;
    // if a callback changed lifecycle/resource state, these answers are not
    // used for settlement or the execution/retirement observations below.
    (void)requested(nodes_[i],0,&requested_observations);
  }
  const auto revision = registry_.revision();
  // A stable policy snapshot and registry need no dependency/retirement scan.
  // Check both sides of policy observation: callbacks may complete lifecycle
  // work. Failed/partial retirement is deliberately never memoized.
  if (settled_ && revision_before == revision &&
      settled_registry_revision_ == revision && std::equal(settled_requests_.begin(), settled_requests_.end(), requested_observations.begin()) &&
      settled_demand_revision_==demand_before&&demand_revision_.load(std::memory_order_acquire)==demand_before) {
    error.clear(); return true;
  }
  settled_ = false;
  if (revision_before == revision&&demand_revision_.load(std::memory_order_acquire)==demand_before) {
    for (size_t i=0;i<start_order_count_;++i) {
      const auto slot=start_order_[i];
      bool wanted = requested_observations[slot] == 2;
      for (const auto provider : required_nodes_[slot]) wanted = wanted && observations[provider] == 2;
      observations[slot]=wanted?2:1;
    }
  }
  bool retired = false;
  auto &states = observed_states_;
  auto present=registry_.read_states(states);
  // Dependents always retire before providers. Stopping is retried even if
  // intent was restored meanwhile; a partial teardown cannot resume in place.
  for (size_t offset = 0; offset < start_order_count_; ++offset) {
    const auto slot = start_order_[start_order_count_ - offset - 1];
    const auto *node = &nodes_[slot];
    const char *id = node->definition.id; const auto state=states[registry_slots_[slot]];
    if (state == ServiceState::Registered ||
        state == ServiceState::Stopped) continue;
    if (state != ServiceState::Stopping && !retiring_[slot] &&
        execution_wanted(*node, 0, &observations)) continue;
    retiring_[slot] = true;
    retired = true;
    std::string detail;
    if (stop_one(id, detail)) retiring_[slot] = false;
    else if (pending.empty()) pending = detail;
    present=registry_.read_states(states);
    observations.fill(0); // Teardown can change another service's demand.
  }
  if (!pending.empty()) { error = pending; return false; }
  // Only intentionally retired services restart. Failed startup requires an
  // explicit recovery, avoiding an allocation/retry storm at every owner tick.
  bool restarted = false;
  if (present & (uint32_t(1) << static_cast<unsigned>(ServiceState::Stopped))) {
    for (size_t i = 0; i < start_order_count_; ++i) {
      const auto slot=start_order_[i];const auto *node = &nodes_[slot];const auto state=states[registry_slots_[slot]];
      if (
          state != ServiceState::Stopped || !execution_wanted(*node)) continue;
      if (dependencies_ready(*node)) {
        if (!start(node->definition.id, error)) return false;
        present=registry_.read_states(states);
        restarted = true;
      }
    }
  }
  if (restarted || (present & (uint32_t(1) << static_cast<unsigned>(ServiceState::Registered)))) {
    if (!start_all(error)) return false;
  }
  if (!retired && revision_before == revision && registry_.revision() == revision&&demand_revision_.load(std::memory_order_acquire)==demand_before) {
    std::copy(requested_observations.begin(), requested_observations.end(), settled_requests_.begin());settled_registry_revision_=revision;settled_demand_revision_=demand_before;settled_=true;
  }
  error.clear(); return true;
}

} // namespace lunaredu::lunaros
