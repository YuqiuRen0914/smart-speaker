#pragma once
#include <array>
#include "bsp/lunaredu_bsp.h"

namespace lunaredu::bsp {
struct ResourceDescriptor {
  bsp_resource_t id;
  const char *name;
  uint64_t pin_mask;
  bool available;
};
using ResourceTable = std::array<ResourceDescriptor, BSP_RESOURCE_COUNT>;
const ResourceTable &board_resources();
bool uses_legacy_baseboard();
ResourceTable legacy_baseboard_resources();
const bsp_baseboard_group_t *legacy_baseboard_groups(size_t *count);
const bsp_i2c_bus_t *legacy_mpu_i2c_bus();
const bsp_i2c_bus_t *legacy_expansion_i2c1_bus();
} // namespace lunaredu::bsp
