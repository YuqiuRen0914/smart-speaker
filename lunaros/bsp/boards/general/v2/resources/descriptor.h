#pragma once
#include <array>
#include "bsp/boards/general/v2.h"
namespace lunaredu::bsp_v2 {
struct ResourceDescriptor {bsp_v2_resource_t id;const char *name;uint64_t pin_mask;bool available;};
using ResourceTable=std::array<ResourceDescriptor,BSP_V2_RESOURCE_COUNT>;
const ResourceTable &resources();
void fill_resources(ResourceTable &table);
size_t resource_signals(bsp_v2_resource_t,bsp_v2_signal_t *out,size_t capacity);
}
