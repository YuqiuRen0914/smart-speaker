#pragma once
#include "runtime/module_maintenance.h"
#include <string>
#include <cstring>
#include <vector>
#include <utility>

namespace lunaredu::lunaros {
// Configured once before executor admission. Metadata and executor names remain
// borrowed from the boot-lived module and immutable service registrations.
class ModuleMaintenanceHost {
  struct Slot {
    const ModuleMaintenance *definition;
    const char *executor;
    uint32_t last = 0, delay = 0;
    bool polled = false;
  };
  std::vector<Slot> slots_;
 public:
  bool empty() const {return slots_.empty();}
  template<class Runtime> bool configure(const ModuleMaintenance *definitions, size_t count,
      const Runtime &runtime, std::string &error) {
    if (!slots_.empty()) { error="module_maintenance_already_configured"; return false; }
    if (count && !definitions) { error="invalid_module_maintenance"; return false; }
    std::vector<Slot> prepared;
    prepared.reserve(count);
    for (size_t i=0;i<count;++i) {
      const auto &definition=definitions[i];
      if (!definition.owner_service_id || !*definition.owner_service_id || !definition.step) {
        error="invalid_module_maintenance"; return false;
      }
      const auto *executor=runtime.service_owner_executor(definition.owner_service_id);
      if (!executor || !*executor) {
        error="module_maintenance_owner_missing:"+std::string(definition.owner_service_id); return false;
      }
      for (const auto &slot:prepared)
        if (std::strcmp(slot.definition->owner_service_id,definition.owner_service_id)==0 &&
            slot.definition->context==definition.context && slot.definition->step==definition.step) {
          error="duplicate_module_maintenance"; return false;
        }
      prepared.push_back({&definition,executor});
    }
    slots_=std::move(prepared);
    error.clear();return true;
  }
  template<class Runtime> void poll(Runtime &runtime,uint32_t now_ms) {
    for (auto &slot:slots_) {
      // Each mutable slot is touched only by its already-bound executor. A
      // stopped owner is not revived by maintenance; stop_step owns retirement.
      if (!runtime.current_task_owns_executor(slot.executor) ||
          !runtime.service_available(slot.definition->owner_service_id)) continue;
      if (slot.polled && now_ms-slot.last<slot.delay) continue;
      slot.last=now_ms;slot.polled=true;
      slot.delay=slot.definition->step(slot.definition->context,now_ms);
      if (slot.delay>UINT32_MAX/2) slot.delay=UINT32_MAX/2;
    }
  }
};
}
