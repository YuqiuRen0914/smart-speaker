#pragma once

#include <cstddef>
#include <cstdint>
#ifdef ESP_PLATFORM
#include <esp_timer.h>
#else
#include <chrono>
#endif

// Platform primitives shared through the public Core API.
// Products select service policy through their composition APIs.
namespace lunaredu::lunaros {

uint32_t system_monotonic_ms();
inline uint64_t system_monotonic_us() {
#ifdef ESP_PLATFORM
  return static_cast<uint64_t>(esp_timer_get_time());
#else
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
void system_delay_ms(uint32_t delay_ms);
bool system_read_factory_mac(uint8_t *buffer, size_t size);
uint32_t system_random_u32();
const char *system_firmware_version();
[[noreturn]] void system_restart();
[[noreturn]] void system_restart_after_delay(uint32_t delay_ms);

} // namespace lunaredu::lunaros
