#include "platform/system.h"

#include <esp_app_desc.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace lunaredu::lunaros {

uint32_t system_monotonic_ms() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void system_delay_ms(uint32_t delay_ms) {
  const TickType_t ticks = pdMS_TO_TICKS(delay_ms);
  vTaskDelay(ticks == 0 ? 1 : ticks);
}

bool system_read_factory_mac(uint8_t *buffer, size_t size) {
  return buffer != nullptr && size >= 6 &&
         esp_efuse_mac_get_default(buffer) == ESP_OK;
}

uint32_t system_random_u32() { return esp_random(); }

const char *system_firmware_version() {
  const esp_app_desc_t *description = esp_app_get_description();
  return description != nullptr && description->version[0] != '\0'
             ? description->version
             : "unknown";
}

[[noreturn]] void system_restart() {
  esp_restart();
  __builtin_unreachable();
}

[[noreturn]] void system_restart_after_delay(uint32_t delay_ms) {
  vTaskDelay(pdMS_TO_TICKS(delay_ms));
  system_restart();
}

} // namespace lunaredu::lunaros
