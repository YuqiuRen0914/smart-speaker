#include "ili9488_panel.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <sys/cdefs.h>

#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_commands.h>
#include <esp_lcd_panel_interface.h>
#include <esp_lcd_panel_io.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#ifndef CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_ILI9488_PIPELINE_ROWS
#define CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_ILI9488_PIPELINE_ROWS 4
#endif

namespace {

constexpr const char *kTag = "lcd_panel.ili9488";
// Two bounded buffers preserve conversion/SPI overlap. The row count is a
// Kconfig trade-off: larger buffers reduce command/transaction overhead, but
// permanently reserve more internal DMA SRAM for the Wi-Fi/assistant product.
constexpr size_t kRgb666BufferCount = 2;
constexpr uint32_t kRgb666BufferWaitMs = 500;
constexpr size_t kRgb666BufferRows =
    CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_ILI9488_PIPELINE_ROWS;
constexpr size_t kRgb666BufferPixels = 480U * kRgb666BufferRows;
constexpr uint16_t kNativeScrollLines = 480;
constexpr uint8_t kVerticalScrollDefinitionCommand = 0x33;
constexpr uint8_t kVerticalScrollStartCommand = 0x37;
struct Ili9488Panel {
  esp_lcd_panel_t base;
  esp_lcd_panel_io_handle_t io = nullptr;
  gpio_num_t reset_gpio_num = GPIO_NUM_NC;
  bool reset_level = false;
  int x_gap = 0;
  int y_gap = 0;
  uint8_t madctl_val = 0;
  LunareduIli9488PixelMode pixel_mode =
      LunareduIli9488PixelMode::kRgb666;
  std::array<uint8_t *, kRgb666BufferCount> rgb666_buffers = {};
  size_t next_rgb666_buffer = 0;
  uint16_t scroll_top_fixed_lines = 0;
  uint16_t scroll_lines = kNativeScrollLines;
  uint16_t scroll_bottom_fixed_lines = 0;
  StaticSemaphore_t free_buffer_slots_storage = {};
  SemaphoreHandle_t free_buffer_slots = nullptr;
  uint32_t submitted_transfers = 0;
  uint32_t completed_transfers = 0;
  uint32_t buffer_wait_count = 0;
  uint8_t max_in_flight = 0;
};

bool IRAM_ATTR on_color_transfer_done(
    esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *,
    void *user_context) {
  auto *ili9488 = static_cast<Ili9488Panel *>(user_context);
  if (ili9488 == nullptr || ili9488->free_buffer_slots == nullptr) {
    return false;
  }
  __atomic_add_fetch(&ili9488->completed_transfers, 1U, __ATOMIC_RELAXED);
  BaseType_t higher_priority_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(ili9488->free_buffer_slots,
                        &higher_priority_task_woken);
  return higher_priority_task_woken == pdTRUE;
}

void note_in_flight(Ili9488Panel *ili9488) {
  const uint32_t submitted =
      __atomic_load_n(&ili9488->submitted_transfers, __ATOMIC_RELAXED);
  const uint32_t completed =
      __atomic_load_n(&ili9488->completed_transfers, __ATOMIC_RELAXED);
  const uint8_t in_flight = static_cast<uint8_t>(
      std::min<uint32_t>(kRgb666BufferCount, submitted - completed));
  ili9488->max_in_flight =
      std::max<uint8_t>(ili9488->max_in_flight, in_flight);
}

esp_err_t acquire_rgb666_buffer_slot(Ili9488Panel *ili9488) {
  ESP_RETURN_ON_FALSE(ili9488->free_buffer_slots != nullptr,
                      ESP_ERR_INVALID_STATE, kTag,
                      "RGB666 buffer slots unavailable");
  if (xSemaphoreTake(ili9488->free_buffer_slots, 0) == pdTRUE) {
    return ESP_OK;
  }
  ++ili9488->buffer_wait_count;
  ESP_RETURN_ON_FALSE(
      xSemaphoreTake(ili9488->free_buffer_slots,
                     pdMS_TO_TICKS(kRgb666BufferWaitMs)) == pdTRUE,
      ESP_ERR_TIMEOUT, kTag, "RGB666 buffer wait failed");
  return ESP_OK;
}

esp_err_t wait_for_rgb666_pipeline_idle(Ili9488Panel *ili9488) {
  ESP_RETURN_ON_FALSE(ili9488->free_buffer_slots != nullptr,
                      ESP_ERR_INVALID_STATE, kTag,
                      "RGB666 buffer slots unavailable");
  size_t acquired_slots = 0;
  for (; acquired_slots < kRgb666BufferCount; ++acquired_slots) {
    if (xSemaphoreTake(ili9488->free_buffer_slots,
                       pdMS_TO_TICKS(kRgb666BufferWaitMs)) != pdTRUE) {
      while (acquired_slots > 0) {
        xSemaphoreGive(ili9488->free_buffer_slots);
        --acquired_slots;
      }
      ESP_LOGE(kTag, "RGB666 pipeline idle wait timed out submitted=%u completed=%u",
               static_cast<unsigned>(__atomic_load_n(
                   &ili9488->submitted_transfers, __ATOMIC_RELAXED)),
               static_cast<unsigned>(__atomic_load_n(
                   &ili9488->completed_transfers, __ATOMIC_RELAXED)));
      return ESP_ERR_TIMEOUT;
    }
  }
  while (acquired_slots > 0) {
    xSemaphoreGive(ili9488->free_buffer_slots);
    --acquired_slots;
  }
  return ESP_OK;
}

esp_err_t recycle_completed_spi_transactions(Ili9488Panel *ili9488) {
  // Once both panel-owned slots have been returned by the DMA callback, every
  // queued color transaction is already complete. The generic parameter
  // barrier can therefore only recycle completed descriptors and cannot enter
  // its otherwise-unbounded portMAX_DELAY wait.
  ESP_RETURN_ON_ERROR(wait_for_rgb666_pipeline_idle(ili9488), kTag,
                      "RGB666 pipeline did not become idle");
  return esp_lcd_panel_io_tx_param(ili9488->io, -1, nullptr, 0);
}

esp_err_t submit_pixel_transfer(Ili9488Panel *ili9488, int command,
                                const uint8_t *pixels, size_t bytes) {
  __atomic_add_fetch(&ili9488->submitted_transfers, 1U, __ATOMIC_RELAXED);
  note_in_flight(ili9488);
  const esp_err_t result =
      esp_lcd_panel_io_tx_color(ili9488->io, command, pixels, bytes);
  if (result != ESP_OK) {
    __atomic_sub_fetch(&ili9488->submitted_transfers, 1U, __ATOMIC_RELAXED);
    xSemaphoreGive(ili9488->free_buffer_slots);
  }
  return result;
}

uint8_t rgb565_to_r6(uint16_t pixel) {
  return static_cast<uint8_t>(((pixel >> 11) & 0x1F) << 3);
}

uint8_t rgb565_to_g6(uint16_t pixel) {
  return static_cast<uint8_t>(((pixel >> 5) & 0x3F) << 2);
}

uint8_t rgb565_to_b6(uint16_t pixel) {
  return static_cast<uint8_t>((pixel & 0x1F) << 3);
}

void free_rgb666_buffers(Ili9488Panel *ili9488) {
  for (uint8_t *&buffer : ili9488->rgb666_buffers) {
    if (buffer != nullptr) {
      heap_caps_free(buffer);
      buffer = nullptr;
    }
  }
  ili9488->next_rgb666_buffer = 0;
}

esp_err_t ensure_rgb666_buffers(Ili9488Panel *ili9488) {
  if (std::all_of(ili9488->rgb666_buffers.begin(),
                  ili9488->rgb666_buffers.end(),
                  [](const uint8_t *buffer) { return buffer != nullptr; })) {
    return ESP_OK;
  }

  free_rgb666_buffers(ili9488);
  for (uint8_t *&buffer : ili9488->rgb666_buffers) {
    buffer = static_cast<uint8_t *>(heap_caps_malloc(
        kRgb666BufferPixels * 3,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
      free_rgb666_buffers(ili9488);
      ESP_LOGE(kTag, "no mem for RGB666 pipeline buffers");
      return ESP_ERR_NO_MEM;
    }
  }
  ESP_LOGI(kTag, "RGB666 pipeline buffers ready count=%u rows=%u pixels=%u bytes=%u",
           static_cast<unsigned>(kRgb666BufferCount),
           static_cast<unsigned>(kRgb666BufferRows),
           static_cast<unsigned>(kRgb666BufferPixels),
           static_cast<unsigned>(kRgb666BufferCount * kRgb666BufferPixels * 3));
  return ESP_OK;
}

void convert_rgb565_to_rgb666(const uint16_t *src, size_t pixels,
                              uint8_t *dst) {
  for (size_t index = 0; index < pixels; ++index) {
    const uint16_t pixel = src[index];
    const uint8_t r = rgb565_to_r6(pixel);
    const uint8_t g = rgb565_to_g6(pixel);
    const uint8_t b = rgb565_to_b6(pixel);
    dst[index * 3 + 0] = r;
    dst[index * 3 + 1] = g;
    dst[index * 3 + 2] = b;
  }
}

void normalize_rgb666(const uint8_t *src, size_t pixels,
                      LunareduIli9488Rgb666Order order, uint8_t *dst) {
  for (size_t index = 0; index < pixels; ++index) {
    const uint8_t *pixel = src + index * 3;
    const uint8_t red = order == LunareduIli9488Rgb666Order::kRgb
                            ? pixel[0]
                            : pixel[2];
    const uint8_t green = pixel[1];
    const uint8_t blue = order == LunareduIli9488Rgb666Order::kRgb
                             ? pixel[2]
                             : pixel[0];
    dst[index * 3] = static_cast<uint8_t>(red & 0xFCU);
    dst[index * 3 + 1] = static_cast<uint8_t>(green & 0xFCU);
    dst[index * 3 + 2] = static_cast<uint8_t>(blue & 0xFCU);
  }
}

esp_err_t tx_param(esp_lcd_panel_io_handle_t io, uint8_t cmd,
                   const uint8_t *data, size_t size) {
  return esp_lcd_panel_io_tx_param(io, cmd, data, size);
}

esp_err_t tx_param1(esp_lcd_panel_io_handle_t io, uint8_t cmd, uint8_t value) {
  return tx_param(io, cmd, &value, 1);
}

template <size_t N>
esp_err_t tx_param_array(esp_lcd_panel_io_handle_t io, uint8_t cmd,
                         const uint8_t (&data)[N]) {
  return tx_param(io, cmd, data, N);
}

esp_err_t panel_ili9488_del(esp_lcd_panel_t *panel) {
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  // A pipelined draw may return with the final DMA transfer still queued.
  // Drain it before releasing the panel-owned buffers.
  const esp_err_t wait_result = recycle_completed_spi_transactions(ili9488);
  if (wait_result != ESP_OK) {
    ESP_LOGW(kTag, "wait before panel delete failed: %s",
             esp_err_to_name(wait_result));
  }
  const esp_lcd_panel_io_callbacks_t callbacks = {};
  const esp_err_t callback_result =
      esp_lcd_panel_io_register_event_callbacks(ili9488->io, &callbacks,
                                                nullptr);
  if (callback_result != ESP_OK) {
    ESP_LOGW(kTag, "clear DMA callback failed: %s",
             esp_err_to_name(callback_result));
  }
  if (ili9488->reset_gpio_num >= 0) {
    gpio_reset_pin(ili9488->reset_gpio_num);
  }
  free_rgb666_buffers(ili9488);
  std::free(ili9488);
  return ESP_OK;
}

esp_err_t panel_ili9488_reset(esp_lcd_panel_t *panel) {
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  ili9488->scroll_top_fixed_lines = 0;
  ili9488->scroll_lines = kNativeScrollLines;
  ili9488->scroll_bottom_fixed_lines = 0;
  if (ili9488->reset_gpio_num >= 0) {
    gpio_set_level(ili9488->reset_gpio_num, ili9488->reset_level ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(15));
    gpio_set_level(ili9488->reset_gpio_num, ili9488->reset_level ? 0 : 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
  }

  ESP_RETURN_ON_ERROR(tx_param(ili9488->io, LCD_CMD_SWRESET, nullptr, 0), kTag,
                      "software reset failed");
  vTaskDelay(pdMS_TO_TICKS(120));
  return ESP_OK;
}

esp_err_t panel_ili9488_init(esp_lcd_panel_t *panel) {
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  esp_lcd_panel_io_handle_t io = ili9488->io;

  // Reserve the panel's fixed DMA requirement before resident application
  // tasks fragment internal SRAM. Drawing must not depend on a late allocation.
  ESP_RETURN_ON_ERROR(ensure_rgb666_buffers(ili9488), kTag,
                      "RGB666 buffers allocation failed");

  const uint8_t power_setting[] = {0xA9, 0x51, 0x2C, 0x82};
  const uint8_t power_control1[] = {0x11, 0x09};
  const uint8_t vcom_control[] = {0x00, 0x0A, 0x80};
  const uint8_t frame_rate[] = {0xB0, 0x11};
  const uint8_t display_function[] = {0x02, 0x22};
  const uint8_t pwm_frequency[] = {0x00, 0x04};
  const uint8_t positive_gamma[] = {
      0x00, 0x07, 0x10, 0x09, 0x17, 0x0B, 0x41, 0x89,
      0x4B, 0x0A, 0x0C, 0x0E, 0x18, 0x1B, 0x0F,
  };
  const uint8_t negative_gamma[] = {
      0x00, 0x17, 0x1A, 0x04, 0x0E, 0x06, 0x2F, 0x45,
      0x43, 0x02, 0x0A, 0x09, 0x32, 0x36, 0x0F,
  };

  ESP_RETURN_ON_ERROR(tx_param_array(io, 0xF7, power_setting), kTag,
                      "F7 failed");
  ESP_RETURN_ON_ERROR(tx_param_array(io, 0xC0, power_control1), kTag,
                      "C0 failed");
  ESP_RETURN_ON_ERROR(tx_param1(io, 0xC1, 0x41), kTag, "C1 failed");
  ESP_RETURN_ON_ERROR(tx_param_array(io, 0xC5, vcom_control), kTag,
                      "C5 failed");
  ESP_RETURN_ON_ERROR(tx_param_array(io, 0xB1, frame_rate), kTag,
                      "B1 failed");
  ESP_RETURN_ON_ERROR(tx_param1(io, 0xB4, 0x02), kTag, "B4 failed");
  ESP_RETURN_ON_ERROR(tx_param_array(io, 0xB6, display_function), kTag,
                      "B6 failed");
  ESP_RETURN_ON_ERROR(tx_param1(io, 0xB7, 0xC6), kTag, "B7 failed");
  ESP_RETURN_ON_ERROR(tx_param_array(io, 0xBE, pwm_frequency), kTag,
                      "BE failed");
  ESP_RETURN_ON_ERROR(tx_param1(io, 0xE9, 0x00), kTag, "E9 failed");
  ESP_RETURN_ON_ERROR(tx_param1(io, LCD_CMD_MADCTL, ili9488->madctl_val),
                      kTag, "MADCTL failed");
  ESP_RETURN_ON_ERROR(tx_param1(io, LCD_CMD_COLMOD, 0x66), kTag,
                      "COLMOD failed");
  const uint8_t full_scroll_area[] = {0x00, 0x00, 0x01,
                                      0xE0, 0x00, 0x00};
  const uint8_t initial_scroll_start[] = {0x00, 0x00};
  ESP_RETURN_ON_ERROR(
      tx_param_array(io, kVerticalScrollDefinitionCommand, full_scroll_area),
      kTag, "initial VSCRDEF failed");
  ESP_RETURN_ON_ERROR(
      tx_param_array(io, kVerticalScrollStartCommand, initial_scroll_start),
      kTag, "initial VSCRSADD failed");
  ESP_RETURN_ON_ERROR(tx_param_array(io, 0xE0, positive_gamma), kTag,
                      "E0 failed");
  ESP_RETURN_ON_ERROR(tx_param_array(io, 0xE1, negative_gamma), kTag,
                      "E1 failed");
  ESP_RETURN_ON_ERROR(tx_param(io, LCD_CMD_SLPOUT, nullptr, 0), kTag,
                      "sleep out failed");
  vTaskDelay(pdMS_TO_TICKS(120));
  ESP_RETURN_ON_ERROR(tx_param(io, LCD_CMD_DISPON, nullptr, 0), kTag,
                      "display on failed");
  return ESP_OK;
}

esp_err_t draw_bitmap_internal(Ili9488Panel *ili9488, int x_start,
                               int y_start, int x_end, int y_end,
                               const void *color_data, bool source_rgb666,
                               LunareduIli9488Rgb666Order rgb666_order) {
  ESP_RETURN_ON_FALSE(color_data != nullptr, ESP_ERR_INVALID_ARG, kTag,
                      "color data missing");
  ESP_RETURN_ON_FALSE(x_end > x_start && y_end > y_start, ESP_ERR_INVALID_ARG,
                      kTag, "invalid draw area");
  ESP_RETURN_ON_FALSE(
      !source_rgb666 ||
          ili9488->pixel_mode == LunareduIli9488PixelMode::kRgb666,
      ESP_ERR_INVALID_STATE, kTag,
      "RGB888 source requires RGB666 panel mode");

  x_start += ili9488->x_gap;
  x_end += ili9488->x_gap;
  y_start += ili9488->y_gap;
  y_end += ili9488->y_gap;

  const size_t width = static_cast<size_t>(x_end - x_start);
  const size_t height = static_cast<size_t>(y_end - y_start);
  ESP_RETURN_ON_FALSE(width <= kRgb666BufferPixels, ESP_ERR_NOT_SUPPORTED, kTag,
                      "draw width exceeds RGB666 pipeline buffer");
  ESP_RETURN_ON_ERROR(ensure_rgb666_buffers(ili9488), kTag,
                      "pixel pipeline buffers failed");

  const size_t rows_per_chunk =
      std::max<size_t>(1, kRgb666BufferPixels / width);
  const uint16_t *rgb565 = static_cast<const uint16_t *>(color_data);
  const uint8_t *rgb666 = static_cast<const uint8_t *>(color_data);
  // CASET/RASET are polling SPI transactions. ESP-IDF drains preceding queued
  // color transfers with portMAX_DELAY before sending them. First prove through
  // our bounded completion semaphore that the queue is already idle, so those
  // polling calls can only recycle descriptors and return immediately.
  ESP_RETURN_ON_ERROR(recycle_completed_spi_transactions(ili9488), kTag,
                      "wait before address window failed");
  const uint8_t caset[] = {
      static_cast<uint8_t>(x_start >> 8),
      static_cast<uint8_t>(x_start),
      static_cast<uint8_t>((x_end - 1) >> 8),
      static_cast<uint8_t>(x_end - 1),
  };
  const uint8_t raset[] = {
      static_cast<uint8_t>(y_start >> 8),
      static_cast<uint8_t>(y_start),
      static_cast<uint8_t>((y_end - 1) >> 8),
      static_cast<uint8_t>(y_end - 1),
  };
  ESP_RETURN_ON_ERROR(tx_param_array(ili9488->io, LCD_CMD_CASET, caset), kTag,
                      "CASET failed");
  ESP_RETURN_ON_ERROR(tx_param_array(ili9488->io, LCD_CMD_RASET, raset), kTag,
                      "RASET failed");
  size_t row = 0;
  bool first_chunk = true;
  while (row < height) {
    const size_t chunk_rows = std::min(rows_per_chunk, height - row);
    const size_t chunk_pixels = width * chunk_rows;
    ESP_RETURN_ON_ERROR(acquire_rgb666_buffer_slot(ili9488), kTag,
                        "RGB666 pipeline wait failed");
    const size_t buffer_index = ili9488->next_rgb666_buffer;
    uint8_t *const transfer_buffer = ili9488->rgb666_buffers[buffer_index];

    // Convert before setting the address window. On the first chunk this CPU
    // work overlaps the final DMA transfer of the preceding LVGL flush. After
    // RAMWR, the ILI9488 retains its frame-memory write state while CS is high,
    // so later data-only transactions can remain queued without a polling
    // command draining the DMA queue.
    size_t transfer_bytes = chunk_pixels * 3;
    if (source_rgb666) {
      normalize_rgb666(rgb666 + row * width * 3, chunk_pixels,
                       rgb666_order, transfer_buffer);
    } else if (ili9488->pixel_mode == LunareduIli9488PixelMode::kRgb666) {
      convert_rgb565_to_rgb666(rgb565 + row * width, chunk_pixels,
                               transfer_buffer);
    } else {
      // ILI9488 RGB565 is always sent MSB first on SPI. The public surface
      // owns canonical little-endian RGB565, so pack the two wire bytes here.
      for (size_t index = 0; index < chunk_pixels; ++index) {
        const uint16_t pixel = rgb565[row * width + index];
        transfer_buffer[index * 2] = static_cast<uint8_t>(pixel >> 8);
        transfer_buffer[index * 2 + 1] = static_cast<uint8_t>(pixel);
      }
      transfer_bytes = chunk_pixels * 2;
    }

    ESP_RETURN_ON_ERROR(
        submit_pixel_transfer(ili9488, first_chunk ? LCD_CMD_RAMWR : -1,
                              transfer_buffer, transfer_bytes),
        kTag, "pixel DMA submit failed");
    ili9488->next_rgb666_buffer =
        (buffer_index + 1) % kRgb666BufferCount;
    row += chunk_rows;
    first_chunk = false;
  }
  return ESP_OK;
}

esp_err_t panel_ili9488_draw_bitmap(esp_lcd_panel_t *panel, int x_start,
                                    int y_start, int x_end, int y_end,
                                    const void *color_data) {
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  return draw_bitmap_internal(ili9488, x_start, y_start, x_end, y_end,
                              color_data, false,
                              LunareduIli9488Rgb666Order::kRgb);
}

esp_err_t panel_ili9488_invert_color(esp_lcd_panel_t *panel,
                                     bool invert_color_data) {
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  return tx_param(ili9488->io, invert_color_data ? LCD_CMD_INVON
                                                 : LCD_CMD_INVOFF,
                  nullptr, 0);
}

esp_err_t panel_ili9488_mirror(esp_lcd_panel_t *panel, bool mirror_x,
                               bool mirror_y) {
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  if (mirror_x) {
    ili9488->madctl_val |= LCD_CMD_MX_BIT;
  } else {
    ili9488->madctl_val &= ~LCD_CMD_MX_BIT;
  }
  if (mirror_y) {
    ili9488->madctl_val |= LCD_CMD_MY_BIT;
  } else {
    ili9488->madctl_val &= ~LCD_CMD_MY_BIT;
  }
  return tx_param1(ili9488->io, LCD_CMD_MADCTL, ili9488->madctl_val);
}

esp_err_t panel_ili9488_swap_xy(esp_lcd_panel_t *panel, bool swap_axes) {
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  if (swap_axes) {
    ili9488->madctl_val |= LCD_CMD_MV_BIT;
  } else {
    ili9488->madctl_val &= ~LCD_CMD_MV_BIT;
  }
  return tx_param1(ili9488->io, LCD_CMD_MADCTL, ili9488->madctl_val);
}

esp_err_t panel_ili9488_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap) {
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  ili9488->x_gap = x_gap;
  ili9488->y_gap = y_gap;
  return ESP_OK;
}

esp_err_t panel_ili9488_disp_on_off(esp_lcd_panel_t *panel, bool on_off) {
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  return tx_param(ili9488->io, on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF,
                  nullptr, 0);
}

esp_err_t panel_ili9488_sleep(esp_lcd_panel_t *panel, bool sleep) {
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  ESP_RETURN_ON_ERROR(tx_param(ili9488->io,
                               sleep ? LCD_CMD_SLPIN : LCD_CMD_SLPOUT,
                               nullptr, 0),
                      kTag, "sleep command failed");
  vTaskDelay(pdMS_TO_TICKS(120));
  return ESP_OK;
}

} // namespace

esp_err_t lunaredu_esp_lcd_new_panel_ili9488(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel) {
  esp_err_t ret = ESP_OK;
  Ili9488Panel *ili9488 = nullptr;
  esp_lcd_panel_io_callbacks_t callbacks = {};
  ESP_GOTO_ON_FALSE(io != nullptr && panel_dev_config != nullptr &&
                        ret_panel != nullptr,
                    ESP_ERR_INVALID_ARG, err, kTag, "invalid argument");

  ili9488 = static_cast<Ili9488Panel *>(std::calloc(1, sizeof(Ili9488Panel)));
  ESP_GOTO_ON_FALSE(ili9488 != nullptr, ESP_ERR_NO_MEM, err, kTag,
                    "no mem for ILI9488 panel");

  if (panel_dev_config->reset_gpio_num >= 0) {
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num;
    ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, kTag,
                      "configure reset GPIO failed");
  }

  switch (panel_dev_config->rgb_ele_order) {
  case LCD_RGB_ELEMENT_ORDER_RGB:
    ili9488->madctl_val = 0x00;
    break;
  case LCD_RGB_ELEMENT_ORDER_BGR:
    ili9488->madctl_val = LCD_CMD_BGR_BIT;
    break;
  default:
    ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, kTag,
                      "unsupported RGB element order");
    break;
  }

  ESP_GOTO_ON_FALSE(panel_dev_config->bits_per_pixel == 16,
                    ESP_ERR_NOT_SUPPORTED, err, kTag,
                    "ILI9488 adapter expects RGB565 framebuffer input");

  ili9488->io = io;
  ili9488->reset_gpio_num = panel_dev_config->reset_gpio_num;
  ili9488->reset_level = panel_dev_config->flags.reset_active_high;
  ili9488->scroll_lines = kNativeScrollLines;
  ili9488->free_buffer_slots = xSemaphoreCreateCountingStatic(
      kRgb666BufferCount, kRgb666BufferCount,
      &ili9488->free_buffer_slots_storage);
  ESP_GOTO_ON_FALSE(ili9488->free_buffer_slots != nullptr,
                    ESP_ERR_NO_MEM, err, kTag,
                    "create RGB666 buffer semaphore failed");
  callbacks.on_color_trans_done = on_color_transfer_done;
  ESP_GOTO_ON_ERROR(
      esp_lcd_panel_io_register_event_callbacks(io, &callbacks, ili9488),
      err, kTag, "register RGB666 DMA callback failed");
  ili9488->base.del = panel_ili9488_del;
  ili9488->base.reset = panel_ili9488_reset;
  ili9488->base.init = panel_ili9488_init;
  ili9488->base.draw_bitmap = panel_ili9488_draw_bitmap;
  ili9488->base.invert_color = panel_ili9488_invert_color;
  ili9488->base.mirror = panel_ili9488_mirror;
  ili9488->base.swap_xy = panel_ili9488_swap_xy;
  ili9488->base.set_gap = panel_ili9488_set_gap;
  ili9488->base.disp_on_off = panel_ili9488_disp_on_off;
  ili9488->base.disp_sleep = panel_ili9488_sleep;
  *ret_panel = &ili9488->base;
  return ESP_OK;

err:
  if (ili9488 != nullptr) {
    if (panel_dev_config != nullptr && panel_dev_config->reset_gpio_num >= 0) {
      gpio_reset_pin(panel_dev_config->reset_gpio_num);
    }
    std::free(ili9488);
  }
  return ret;
}

esp_err_t lunaredu_esp_lcd_ili9488_configure_vertical_scroll(
    esp_lcd_panel_handle_t panel, uint16_t top_fixed_lines,
    uint16_t scroll_lines, uint16_t bottom_fixed_lines) {
  ESP_RETURN_ON_FALSE(panel != nullptr, ESP_ERR_INVALID_ARG, kTag,
                      "panel missing");
  ESP_RETURN_ON_FALSE(scroll_lines > 0, ESP_ERR_INVALID_ARG, kTag,
                      "scroll area is empty");
  const uint32_t total_lines = static_cast<uint32_t>(top_fixed_lines) +
                               scroll_lines + bottom_fixed_lines;
  ESP_RETURN_ON_FALSE(total_lines == kNativeScrollLines, ESP_ERR_INVALID_ARG,
                      kTag, "scroll areas must cover 480 native lines");

  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  const uint8_t definition[] = {
      static_cast<uint8_t>(top_fixed_lines >> 8),
      static_cast<uint8_t>(top_fixed_lines),
      static_cast<uint8_t>(scroll_lines >> 8),
      static_cast<uint8_t>(scroll_lines),
      static_cast<uint8_t>(bottom_fixed_lines >> 8),
      static_cast<uint8_t>(bottom_fixed_lines),
  };
  ESP_RETURN_ON_ERROR(
      tx_param_array(ili9488->io, kVerticalScrollDefinitionCommand,
                     definition),
      kTag, "VSCRDEF failed");
  ili9488->scroll_top_fixed_lines = top_fixed_lines;
  ili9488->scroll_lines = scroll_lines;
  ili9488->scroll_bottom_fixed_lines = bottom_fixed_lines;
  return ESP_OK;
}

esp_err_t lunaredu_esp_lcd_ili9488_set_vertical_scroll_start(
    esp_lcd_panel_handle_t panel, uint16_t start_line) {
  ESP_RETURN_ON_FALSE(panel != nullptr, ESP_ERR_INVALID_ARG, kTag,
                      "panel missing");
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  const uint16_t scroll_end = static_cast<uint16_t>(
      ili9488->scroll_top_fixed_lines + ili9488->scroll_lines);
  ESP_RETURN_ON_FALSE(start_line >= ili9488->scroll_top_fixed_lines &&
                          start_line < scroll_end,
                      ESP_ERR_INVALID_ARG, kTag,
                      "scroll start enters a fixed area");
  const uint8_t start[] = {
      static_cast<uint8_t>(start_line >> 8),
      static_cast<uint8_t>(start_line),
  };
  return tx_param_array(ili9488->io, kVerticalScrollStartCommand, start);
}

esp_err_t lunaredu_esp_lcd_ili9488_get_dma_pipeline_status(
    esp_lcd_panel_handle_t panel,
    LunareduIli9488DmaPipelineStatus *status) {
  ESP_RETURN_ON_FALSE(panel != nullptr && status != nullptr,
                      ESP_ERR_INVALID_ARG, kTag, "DMA status argument missing");
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  status->submitted_transfers =
      __atomic_load_n(&ili9488->submitted_transfers, __ATOMIC_RELAXED);
  status->completed_transfers =
      __atomic_load_n(&ili9488->completed_transfers, __ATOMIC_RELAXED);
  status->buffer_wait_count = ili9488->buffer_wait_count;
  status->max_in_flight = ili9488->max_in_flight;
  status->buffer_count = kRgb666BufferCount;
  status->buffer_rows = kRgb666BufferRows;
  status->buffer_pixels = kRgb666BufferPixels;
  return ESP_OK;
}

esp_err_t lunaredu_esp_lcd_ili9488_wait_idle(
    esp_lcd_panel_handle_t panel) {
  ESP_RETURN_ON_FALSE(panel != nullptr, ESP_ERR_INVALID_ARG, kTag,
                      "panel missing");
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  return recycle_completed_spi_transactions(ili9488);
}

esp_err_t lunaredu_esp_lcd_ili9488_set_pixel_mode(
    esp_lcd_panel_handle_t panel, LunareduIli9488PixelMode mode) {
  ESP_RETURN_ON_FALSE(panel != nullptr, ESP_ERR_INVALID_ARG, kTag,
                      "panel missing");
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  if (ili9488->pixel_mode == mode) {
    return ESP_OK;
  }
  // COLMOD must not be changed while a previous RAMWR transaction is still
  // using the panel's write state or its DMA backing buffer.
  ESP_RETURN_ON_ERROR(recycle_completed_spi_transactions(ili9488), kTag,
                      "wait before pixel mode change failed");
  const uint8_t colmod = mode == LunareduIli9488PixelMode::kRgb565 ? 0x55 : 0x66;
  ESP_RETURN_ON_ERROR(tx_param1(ili9488->io, LCD_CMD_COLMOD, colmod), kTag,
                      "COLMOD mode change failed");
  ili9488->pixel_mode = mode;
  ESP_LOGI(kTag, "pixel mode switched to %s",
           mode == LunareduIli9488PixelMode::kRgb565 ? "RGB565" : "RGB666");
  return ESP_OK;
}

esp_err_t lunaredu_esp_lcd_ili9488_get_pixel_mode(
    esp_lcd_panel_handle_t panel, LunareduIli9488PixelMode *mode) {
  ESP_RETURN_ON_FALSE(panel != nullptr && mode != nullptr, ESP_ERR_INVALID_ARG,
                      kTag, "pixel mode argument missing");
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  *mode = ili9488->pixel_mode;
  return ESP_OK;
}


esp_err_t lunaredu_esp_lcd_ili9488_draw_bitmap_rgb666(
    esp_lcd_panel_handle_t panel, int x_start, int y_start, int x_end,
    int y_end, const uint8_t *pixels, LunareduIli9488Rgb666Order order) {
  ESP_RETURN_ON_FALSE(panel != nullptr, ESP_ERR_INVALID_ARG, kTag,
                      "panel missing");
  Ili9488Panel *ili9488 = __containerof(panel, Ili9488Panel, base);
  return draw_bitmap_internal(ili9488, x_start, y_start, x_end, y_end, pixels,
                              true, order);
}
