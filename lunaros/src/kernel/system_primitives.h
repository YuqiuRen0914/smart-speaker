#pragma once

#include <cstddef>
#include <cstdint>

// Narrow ESP32-S3 primitives shared by LunarOS implementation components.
// This is an internal component contract, not a product-facing service API.
namespace lunaredu::lunaros::internal {

uint32_t system_monotonic_ms();
void system_delay_ms(uint32_t delay_ms);
bool system_read_factory_mac(uint8_t *buffer, size_t size);
void system_fill_random(uint8_t *buffer, size_t size);
uint32_t system_random_u32();
const char *system_firmware_version();
[[noreturn]] void system_restart();
[[noreturn]] void system_restart_after_delay(uint32_t delay_ms);

} // namespace lunaredu::lunaros::internal
