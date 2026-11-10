#include "lunaredu_device_sdcard_example.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#if (CONFIG_LUNAREDU_DEVICE_STORAGE_SDCARD || CONFIG_LUNAREDU_DEVICE_ENABLE_TFCARD) && SOC_SDMMC_HOST_SUPPORTED
#include "lunaredu_device_sdcard_storage.h"
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdint>
#include <initializer_list>

namespace lunaredu::device::sdcard {
namespace {


using ExampleConfig = Wiring;
constexpr const char *kTag = "example_sdcard";
sdmmc_card_t *card = nullptr;
constexpr const char *mount_path = "/sdcard-example";
esp_err_t initialize(const void *opaque) {
  const auto &w = *static_cast<const ExampleConfig *>(opaque);
  if (device_sdcard_storage_mounted()) return ESP_ERR_INVALID_STATE;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk = static_cast<gpio_num_t>(w.clk); slot.cmd = static_cast<gpio_num_t>(w.cmd);
  slot.d0 = static_cast<gpio_num_t>(w.d0); slot.d1 = GPIO_NUM_NC; slot.d2 = GPIO_NUM_NC; slot.d3 = GPIO_NUM_NC;
  slot.width = 1; slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_vfs_fat_sdmmc_mount_config_t mount{};
  mount.format_if_mount_failed = false; mount.max_files = 2; mount.allocation_unit_size = 16384;
  return esp_vfs_fat_sdmmc_mount(mount_path, &host, &slot, &mount, &card);
}
void tick(uint32_t) {
  // Minimum test reads card identity/capacity; it does not write user storage.
  ESP_LOGI(kTag, "name=%s sectors=%llu sector_bytes=%lu", card->cid.name,
    static_cast<unsigned long long>(card->csd.capacity), static_cast<unsigned long>(card->csd.sector_size));
}
esp_err_t shutdown() {
  if (!card) return ESP_OK;
  const auto result = esp_vfs_fat_sdcard_unmount(mount_path, card);
  if (result == ESP_OK) card = nullptr;
  return result;
}

// This blocking example runs on the caller's ESP-IDF task, without an OS service.
std::atomic_flag running = ATOMIC_FLAG_INIT;
bool cleanup_pending = false;
esp_err_t run_example(const ExampleConfig &config) {
  if (running.test_and_set(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
  if (cleanup_pending) {
    const auto retry = shutdown();
    if (retry != ESP_OK) { running.clear(std::memory_order_release); return retry; }
    cleanup_pending = false;
  }
  const auto result = initialize(&config);
  if (result == ESP_OK) {
    const int64_t started_us = esp_timer_get_time();
    constexpr uint32_t duration_ms = 10000;
    constexpr uint32_t interval_ms = 3000;
    while (static_cast<uint64_t>(esp_timer_get_time() - started_us) < duration_ms * 1000ULL) {
      tick(static_cast<uint32_t>((esp_timer_get_time() - started_us) / 1000));
      const TickType_t delay = pdMS_TO_TICKS(interval_ms);
      vTaskDelay(delay > 0 ? delay : 1);
    }
  }
  const auto cleanup = shutdown();
  cleanup_pending = cleanup != ESP_OK;
  running.clear(std::memory_order_release);
  // Initialization errors retain their native code, even if rollback also fails.
  return result != ESP_OK ? result : cleanup;
}

} // namespace

esp_err_t example(const Wiring &wiring) {
  for (int pin : {wiring.clk, wiring.cmd, wiring.d0}) if (!GPIO_IS_VALID_GPIO(pin)) return ESP_ERR_INVALID_ARG;
const ExampleConfig &config = wiring;
  return run_example(config);
}
} // namespace lunaredu::device::sdcard
#else
namespace lunaredu::device::sdcard {
esp_err_t example([[maybe_unused]] const Wiring &wiring) { return ESP_ERR_NOT_SUPPORTED; }
}
#endif
