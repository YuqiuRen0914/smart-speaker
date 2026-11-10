#pragma once

#include <string>

#include "lunaredu_lunaros/kernel/task_registry.h"

struct tskTaskControlBlock;

namespace lunaredu::lunaros {

using ManagedTaskFunction = void (*)(void *);
using ManagedTaskHandle = tskTaskControlBlock *;

bool create_managed_task(const ManagedTaskDefinition &definition,
                         ManagedTaskFunction function, void *context,
                         ManagedTaskHandle *handle, std::string &error);

}  // namespace lunaredu::lunaros
