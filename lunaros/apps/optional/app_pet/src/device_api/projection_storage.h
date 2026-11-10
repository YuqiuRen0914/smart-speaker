#pragma once
#include "device_api/device_operation.h"
#include <array>
#include <memory>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif
namespace lunaredu::pet::internal {
constexpr size_t kProjectionOperationCapacity=32;
using ProjectionOperations=std::array<lunaros::DeviceOperationDefinition,kProjectionOperationCapacity>;
struct ProjectionOperationsDeleter {
  void operator()(ProjectionOperations *value) const {
#ifdef ESP_PLATFORM
    if(value){value->~ProjectionOperations();heap_caps_free(value);}
#else
    delete value;
#endif
  }
};
using ProjectionOperationsOwner=std::unique_ptr<ProjectionOperations,ProjectionOperationsDeleter>;
// Projection registry entries borrow this fixed table. Its owner retains a
// stable address until registry teardown; retries never replace a live table.
// External OOM is a registration failure, not an implicit SRAM fallback.
inline bool ensure_projection_operations(ProjectionOperationsOwner &owner) {
  if(owner)return true;
#ifdef ESP_PLATFORM
  const uint32_t caps=(heap_caps_get_total_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)
      ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL)|MALLOC_CAP_8BIT;
  void *memory=heap_caps_malloc(sizeof(ProjectionOperations),caps);
  if(!memory)return false;
  owner.reset(new(memory) ProjectionOperations{});
#else
  owner.reset(new(std::nothrow) ProjectionOperations{});
#endif
  return bool(owner);
}
}
