#include "lunaredu_device_spi_lcd_example.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD
#include "lunaredu_device_spi_lcd.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_io.h>
#include "ili9488_panel.h"

namespace lunaredu::device::spi_lcd {
namespace {
using ExampleConfig = DeviceSpiLcdConfig;
constexpr const char *kTag = "example_spi_lcd";
DeviceSpiLcdPanel panel;
uint16_t *pixels = nullptr;
uint16_t row = 0;
bool owned = false, faulted = false;
[[maybe_unused]] uint32_t reported_ms = 0;
[[maybe_unused]] uint32_t phase = 0;

esp_err_t initialize(const void *opaque) {
  [[maybe_unused]] const auto &config = *static_cast<const ExampleConfig *>(opaque);
  reported_ms = 0;
  phase = 0;
  if (device_spi_lcd_status().initialized) return ESP_ERR_INVALID_STATE;
  if (config.width == 0 || config.width > 1024 || config.height == 0 || config.height > 1024) return ESP_ERR_INVALID_ARG;
  auto driver = config; driver.enabled = true;
  owned = true; faulted = false; row = 0;
  const auto result = device_spi_lcd_create_panel(driver, &panel);
  if (result != ESP_OK) return result;
  pixels = static_cast<uint16_t *>(heap_caps_malloc(static_cast<size_t>(panel.width) * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  return pixels ? ESP_OK : ESP_ERR_NO_MEM;
}

void tick([[maybe_unused]] uint32_t elapsed_ms) {
  if (faulted) return;
  constexpr uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0xffff, 0};
  const uint16_t color = colors[phase % 5];
  const uint16_t wire = panel.rgb565_panel_byte_swap ? __builtin_bswap16(color) : color;
  for (uint16_t x = 0; x < panel.width; ++x) pixels[x] = wire;
  esp_err_t result = esp_lcd_panel_draw_bitmap(panel.panel, panel.offset_x, panel.offset_y + row, panel.offset_x + panel.width, panel.offset_y + row + 1, pixels);
  // Use the existing panel fence without replacing the ILI9488 DMA callback.
  if (result == ESP_OK && panel.controller == DeviceSpiLcdController::kIli9488) result = lunaredu_esp_lcd_ili9488_wait_idle(panel.panel);
  else if (result == ESP_OK && panel.panel_io) result = esp_lcd_panel_io_tx_param(panel.panel_io, -1, nullptr, 0);
  if (result != ESP_OK) { faulted = true; ESP_LOGE(kTag, "draw/fence=%s; stop required", esp_err_to_name(result)); return; }
  if (++row == panel.height) { row = 0; ++phase; ESP_LOGI(kTag, "frame rgb565=0x%04x", color); }
}

esp_err_t shutdown() {
  if (owned) {
    const auto result = device_spi_lcd_destroy_panel(&panel);
    if (result != ESP_OK) return result;
  }
  heap_caps_free(pixels); pixels = nullptr;
  owned = false;
  return ESP_OK;
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
    constexpr uint32_t interval_ms = 1;
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

esp_err_t run_wiring_example(DeviceSpiLcdController controller, int mosi_gpio, int sclk_gpio, int cs_gpio, int dc_gpio, int reset_gpio, int backlight_gpio) {
  if (!GPIO_IS_VALID_GPIO(mosi_gpio)) return ESP_ERR_INVALID_ARG;
  if (!GPIO_IS_VALID_GPIO(sclk_gpio)) return ESP_ERR_INVALID_ARG;
  if (!GPIO_IS_VALID_GPIO(cs_gpio)) return ESP_ERR_INVALID_ARG;
  if (!GPIO_IS_VALID_GPIO(dc_gpio)) return ESP_ERR_INVALID_ARG;
  if (reset_gpio != -1 && !GPIO_IS_VALID_GPIO(reset_gpio)) return ESP_ERR_INVALID_ARG;
  if (backlight_gpio != -1 && !GPIO_IS_VALID_GPIO(backlight_gpio)) return ESP_ERR_INVALID_ARG;

  ExampleConfig config; config.mosi_gpio = mosi_gpio; config.sclk_gpio = sclk_gpio; config.cs_gpio = cs_gpio; config.dc_gpio = dc_gpio;
  config.claim_bsp_spi_resource = false;
  config.reset_gpio = reset_gpio; config.backlight_gpio = backlight_gpio;
  if (controller == DeviceSpiLcdController::kIli9488) {
    config.controller = controller;
    config.width = 480; config.height = 320;
    config.offset_x = 0; config.offset_y = 0;
    config.swap_xy = true; config.mirror_y = true; config.rgb_order_bgr = true;
  }
  return run_example(config);
}
} // namespace

esp_err_t example(int mosi_gpio, int sclk_gpio, int cs_gpio, int dc_gpio, int reset_gpio, int backlight_gpio) {
  return run_wiring_example(DeviceSpiLcdController::kSt7789, mosi_gpio, sclk_gpio, cs_gpio, dc_gpio, reset_gpio, backlight_gpio);
}
esp_err_t example_ili9488(int mosi_gpio, int sclk_gpio, int cs_gpio, int dc_gpio, int reset_gpio, int backlight_gpio) {
  return run_wiring_example(DeviceSpiLcdController::kIli9488, mosi_gpio, sclk_gpio, cs_gpio, dc_gpio, reset_gpio, backlight_gpio);
}
} // namespace lunaredu::device::spi_lcd
#else
namespace lunaredu::device::spi_lcd {
esp_err_t example([[maybe_unused]] int mosi_gpio, [[maybe_unused]] int sclk_gpio, [[maybe_unused]] int cs_gpio, [[maybe_unused]] int dc_gpio, [[maybe_unused]] int reset_gpio, [[maybe_unused]] int backlight_gpio) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t example_ili9488([[maybe_unused]] int mosi_gpio, [[maybe_unused]] int sclk_gpio, [[maybe_unused]] int cs_gpio, [[maybe_unused]] int dc_gpio, [[maybe_unused]] int reset_gpio, [[maybe_unused]] int backlight_gpio) { return ESP_ERR_NOT_SUPPORTED; }
}
#endif
