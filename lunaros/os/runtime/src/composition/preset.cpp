#include "runtime/preset.h"
#include "kernel/service.h"
#include <cstring>
namespace lunaredu::lunaros {
bool validate_runtime_preset(const RuntimePreset &preset, std::string &error) {
  error.clear();
  if (!preset.id || !*preset.id ||
      (preset.service_count && !preset.services) || (preset.projection_count && !preset.projections)) {
    error="invalid_runtime_preset"; return false;
  }
  if ((preset.health.reachability || preset.health.platform || preset.health.device_api) && !preset.observe_health) {
    error="runtime_health_observer_missing"; return false;
  }
  for(size_t i=0;i<preset.service_count;++i) {
    if(!preset.services[i]) {error="invalid_runtime_service_factory";return false;}
    for(size_t j=0;j<i;++j) if(preset.services[i]==preset.services[j]) {error="duplicate_runtime_service_factory";return false;}
  }
  for(size_t i=0;i<preset.projection_count;++i) {
    const auto *entry=preset.projections[i];
    if(!entry || !entry->id || !*entry->id || !entry->install) {error="invalid_runtime_projection";return false;}
    for(size_t j=0;j<i;++j) if(!std::strcmp(entry->id,preset.projections[j]->id)) {error="duplicate_runtime_projection";return false;}
  }
  for(size_t i=0;i<preset.execution.size();++i) {
    const auto *entry=preset.execution[i];
    if(!entry || !entry->id || !*entry->id || (entry->service_id && !*entry->service_id) ||
        (entry->create_default && !entry->service_id) ||
        (entry->executor.id && (!*entry->executor.id || !entry->service_id || !entry->executor.owner_service_id ||
          std::strcmp(entry->executor.owner_service_id,entry->service_id) || !entry->executor.queue_depth))) {
      error="invalid_runtime_execution_contribution";return false;
    }
    for(size_t j=0;j<i;++j)if(!std::strcmp(entry->id,preset.execution[j]->id) ||
        (entry->service_id && preset.execution[j]->service_id && !std::strcmp(entry->service_id,preset.execution[j]->service_id)) ||
        (entry->executor.id && preset.execution[j]->executor.id && !std::strcmp(entry->executor.id,preset.execution[j]->executor.id))) {
      error="duplicate_runtime_execution_contribution";return false;
    }
  }
  return true;
}
}
