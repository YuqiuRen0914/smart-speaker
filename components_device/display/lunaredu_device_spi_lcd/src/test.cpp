#include "lunaredu_device_spi_lcd_test.h"

#include "sdkconfig.h"
#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD
#include "lunaredu_lunaros/esp_idf/device_test.h"
#include <esp_log.h>
#include <cstdint>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_io.h>
#include "ili9488_panel.h"

namespace lunaredu::device::spi_lcd {
namespace {
constexpr const char *kTag = "test_spi_lcd";
DeviceSpiLcdPanel panel;
uint16_t *pixels = nullptr;
uint16_t row = 0;
bool owned = false, faulted = false;
[[maybe_unused]] uint32_t reported_ms = 0;
[[maybe_unused]] uint32_t phase = 0;

esp_err_t initialize(const void *opaque) {
  [[maybe_unused]] const auto &config = *static_cast<const TestConfig *>(opaque);
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

const lunaros::esp_idf::DeviceTest<initialize, tick, shutdown> definition{kTag, 20};
}  // namespace

esp_err_t start_test(const TestConfig &config) { return definition.start(&config); }
esp_err_t stop_test() { return definition.stop(); }

}  // namespace lunaredu::device::spi_lcd
#else
namespace lunaredu::device::spi_lcd {
esp_err_t start_test(const TestConfig &) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t stop_test() { return ESP_OK; }
}  // namespace lunaredu::device::spi_lcd
#endif
