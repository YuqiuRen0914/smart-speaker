#pragma once

#include <string>
#include <initializer_list>
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "lunaredu_lunaros/kernel/service_ids.h"

namespace lunaredu::lunaros {

// Initializes LunarOS registries, task admission and explicitly declared service executors.
// A failed setup leaves the product unbootable; callers must stop composition
// instead of continuing with partially initialized kernel state.
bool setup_core_runtime(std::initializer_list<ServiceExecutorDefinition> executors,
                        std::string &error);

} // namespace lunaredu::lunaros
