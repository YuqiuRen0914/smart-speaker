#include "st7789_3wire_panel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_interface.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace lunaredu::device {
namespace {

constexpr const char *kTag = "st7789_3wire";
constexpr uint8_t kCmdSoftwareReset = 0x01;
constexpr uint8_t kCmdSleepIn = 0x10;
constexpr uint8_t kCmdSleepOut = 0x11;
constexpr uint8_t kCmdNormalOn = 0x13;
constexpr uint8_t kCmdInversionOff = 0x20;
constexpr uint8_t kCmdInversionOn = 0x21;
constexpr uint8_t kCmdDisplayOff = 0x28;
constexpr uint8_t kCmdDisplayOn = 0x29;
constexpr uint8_t kCmdColumnAddress = 0x2A;
constexpr uint8_t kCmdRowAddress = 0x2B;
constexpr uint8_t kCmdMemoryWrite = 0x2C;
constexpr uint8_t kCmdMemoryAccess = 0x36;
constexpr uint8_t kCmdPixelFormat = 0x3A;

constexpr uint8_t kMadctlMirrorY = 0x80;
constexpr uint8_t kMadctlMirrorX = 0x40;
constexpr uint8_t kMadctlSwapXy = 0x20;
constexpr uint8_t kMadctlBgr = 0x08;

// A small internal DMA buffer leaves internal SRAM available for TLS and the
// Workbench HTTP task. The full frame may live in PSRAM; transfers are packed
// and sent synchronously in bounded chunks.
constexpr size_t kRawChunkBytes = 2048;
constexpr size_t kPackedChunkBytes = (kRawChunkBytes * 9 + 7) / 8;

struct St7789ThreeWirePanel {
  esp_lcd_panel_t base = {};
  spi_device_handle_t spi = nullptr;
  uint8_t *packed = nullptr;
  int reset_gpio = -1;
  int x_gap = 0;
  int y_gap = 0;
  bool mirror_x = false;
  bool mirror_y = false;
  bool swap_xy = false;
  bool rgb_order_bgr = false;
};

St7789ThreeWirePanel *to_panel(esp_lcd_panel_t *base) {
  return reinterpret_cast<St7789ThreeWirePanel *>(base);
}

bool valid_gpio(int gpio) { return gpio >= 0 && gpio < GPIO_NUM_MAX; }

size_t pack_9bit(const uint8_t *source, size_t source_size, bool data,
                 uint8_t *target, size_t *bit_count) {
  const size_t packed_bytes = (source_size * 9 + 7) / 8;
  std::memset(target, 0, packed_bytes);

  const uint8_t dc = data ? 1 : 0;
  size_t source_index = 0;
  size_t target_index = 0;

  // Eight input bytes become exactly nine output bytes. Keeping this hot path
  // byte based is substantially cheaper than setting every individual bit.
  while (source_index + 8 <= source_size) {
    const uint8_t b0 = source[source_index + 0];
    const uint8_t b1 = source[source_index + 1];
    const uint8_t b2 = source[source_index + 2];
    const uint8_t b3 = source[source_index + 3];
    const uint8_t b4 = source[source_index + 4];
    const uint8_t b5 = source[source_index + 5];
    const uint8_t b6 = source[source_index + 6];
    const uint8_t b7 = source[source_index + 7];
    target[target_index + 0] = static_cast<uint8_t>((dc << 7) | (b0 >> 1));
    target[target_index + 1] = static_cast<uint8_t>(
        ((b0 & 0x01) << 7) | (dc << 6) | (b1 >> 2));
    target[target_index + 2] = static_cast<uint8_t>(
        ((b1 & 0x03) << 6) | (dc << 5) | (b2 >> 3));
    target[target_index + 3] = static_cast<uint8_t>(
        ((b2 & 0x07) << 5) | (dc << 4) | (b3 >> 4));
    target[target_index + 4] = static_cast<uint8_t>(
        ((b3 & 0x0F) << 4) | (dc << 3) | (b4 >> 5));
    target[target_index + 5] = static_cast<uint8_t>(
        ((b4 & 0x1F) << 3) | (dc << 2) | (b5 >> 6));
    target[target_index + 6] = static_cast<uint8_t>(
        ((b5 & 0x3F) << 2) | (dc << 1) | (b6 >> 7));
    target[target_index + 7] =
        static_cast<uint8_t>(((b6 & 0x7F) << 1) | dc);
    target[target_index + 8] = b7;
    source_index += 8;
    target_index += 9;
  }

  size_t target_bit = target_index * 8;
  for (; source_index < source_size; ++source_index) {
    const uint16_t word = static_cast<uint16_t>(
        (data ? 0x100U : 0x000U) | source[source_index]);
    for (int bit = 8; bit >= 0; --bit) {
      if ((word & (1U << bit)) != 0) {
        target[target_bit / 8] |=
            static_cast<uint8_t>(0x80U >> (target_bit % 8));
      }
      ++target_bit;
    }
  }

  *bit_count = source_size * 9;
  return packed_bytes;
}

esp_err_t write_9bit(St7789ThreeWirePanel *panel, const uint8_t *source,
                     size_t source_size, bool data) {
  if (source == nullptr || source_size == 0) {
    return ESP_OK;
  }

  size_t offset = 0;
  while (offset < source_size) {
    const size_t chunk =
        std::min(kRawChunkBytes, static_cast<size_t>(source_size - offset));
    size_t bit_count = 0;
    const size_t packed_bytes =
        pack_9bit(source + offset, chunk, data, panel->packed, &bit_count);
    if (packed_bytes > kPackedChunkBytes) {
      return ESP_ERR_INVALID_SIZE;
    }

    spi_transaction_t transaction = {};
    // ESP32-S3's SPI master rejects non-byte-aligned transfer lengths. The
    // panel samples complete 9-bit words while CS is active and discards the
    // final padding bits when CS rises, matching the known-good board driver.
    transaction.length = packed_bytes * 8;
    transaction.tx_buffer = panel->packed;
    const esp_err_t result =
        spi_device_polling_transmit(panel->spi, &transaction);
    if (result != ESP_OK) {
      return result;
    }
    offset += chunk;
  }
  return ESP_OK;
}

esp_err_t send_command(St7789ThreeWirePanel *panel, uint8_t command,
                       const uint8_t *parameters = nullptr,
                       size_t parameter_count = 0) {
  esp_err_t result = write_9bit(panel, &command, 1, false);
  if (result == ESP_OK && parameters != nullptr && parameter_count > 0) {
    result = write_9bit(panel, parameters, parameter_count, true);
  }
  return result;
}

uint8_t madctl(const St7789ThreeWirePanel *panel) {
  uint8_t value = panel->rgb_order_bgr ? kMadctlBgr : 0;
  if (panel->mirror_x) {
    value |= kMadctlMirrorX;
  }
  if (panel->mirror_y) {
    value |= kMadctlMirrorY;
  }
  if (panel->swap_xy) {
    value |= kMadctlSwapXy;
  }
  return value;
}

esp_err_t apply_madctl(St7789ThreeWirePanel *panel) {
  const uint8_t value = madctl(panel);
  return send_command(panel, kCmdMemoryAccess, &value, 1);
}

esp_err_t set_window(St7789ThreeWirePanel *panel, int x_start, int y_start,
                     int x_end, int y_end) {
  x_start += panel->x_gap;
  x_end += panel->x_gap;
  y_start += panel->y_gap;
  y_end += panel->y_gap;

  const uint8_t columns[] = {
      static_cast<uint8_t>(x_start >> 8), static_cast<uint8_t>(x_start),
      static_cast<uint8_t>((x_end - 1) >> 8),
      static_cast<uint8_t>(x_end - 1),
  };
  esp_err_t result =
      send_command(panel, kCmdColumnAddress, columns, sizeof(columns));
  if (result != ESP_OK) {
    return result;
  }

  const uint8_t rows[] = {
      static_cast<uint8_t>(y_start >> 8), static_cast<uint8_t>(y_start),
      static_cast<uint8_t>((y_end - 1) >> 8),
      static_cast<uint8_t>(y_end - 1),
  };
  result = send_command(panel, kCmdRowAddress, rows, sizeof(rows));
  if (result != ESP_OK) {
    return result;
  }
  return send_command(panel, kCmdMemoryWrite);
}

esp_err_t panel_reset(esp_lcd_panel_t *base) {
  auto *panel = to_panel(base);
  if (valid_gpio(panel->reset_gpio)) {
    gpio_set_level(static_cast<gpio_num_t>(panel->reset_gpio), 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(static_cast<gpio_num_t>(panel->reset_gpio), 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
  }
  const esp_err_t result = send_command(panel, kCmdSoftwareReset);
  vTaskDelay(pdMS_TO_TICKS(120));
  return result;
}

esp_err_t panel_init(esp_lcd_panel_t *base) {
  auto *panel = to_panel(base);
  esp_err_t result = send_command(panel, kCmdSoftwareReset);
  if (result != ESP_OK) {
    return result;
  }
  vTaskDelay(pdMS_TO_TICKS(120));

  result = send_command(panel, kCmdSleepOut);
  if (result != ESP_OK) {
    return result;
  }
  vTaskDelay(pdMS_TO_TICKS(120));

  const uint8_t pixel_format = 0x55;
  result = send_command(panel, kCmdPixelFormat, &pixel_format, 1);
  if (result == ESP_OK) {
    result = apply_madctl(panel);
  }
  if (result == ESP_OK) {
    result = send_command(panel, kCmdInversionOff);
  }
  if (result == ESP_OK) {
    result = send_command(panel, kCmdNormalOn);
  }
  // The shared panel owner turns display on after the startup GRAM clear.
  vTaskDelay(pdMS_TO_TICKS(60));
  return result;
}

esp_err_t panel_del(esp_lcd_panel_t *base) {
  auto *panel = to_panel(base);
  if (panel->spi != nullptr) {
    (void)send_command(panel, kCmdDisplayOff);
    (void)spi_bus_remove_device(panel->spi);
    panel->spi = nullptr;
  }
  if (panel->packed != nullptr) {
    heap_caps_free(panel->packed);
    panel->packed = nullptr;
  }
  if (valid_gpio(panel->reset_gpio)) {
    gpio_reset_pin(static_cast<gpio_num_t>(panel->reset_gpio));
  }
  std::free(panel);
  return ESP_OK;
}

esp_err_t panel_draw_bitmap(esp_lcd_panel_t *base, int x_start, int y_start,
                            int x_end, int y_end, const void *color_data) {
  auto *panel = to_panel(base);
  if (color_data == nullptr || x_start < 0 || y_start < 0 ||
      x_end <= x_start || y_end <= y_start) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t result = set_window(panel, x_start, y_start, x_end, y_end);
  if (result != ESP_OK) {
    return result;
  }

  const size_t width = static_cast<size_t>(x_end - x_start);
  const size_t height = static_cast<size_t>(y_end - y_start);
  if (width > SIZE_MAX / height || width * height > SIZE_MAX / 2) {
    return ESP_ERR_INVALID_SIZE;
  }
  const size_t byte_count = width * height * 2;

  // The shared LCD surface supplies RGB565 as an MSB-first byte stream. Pack
  // those bytes directly so each one receives its own in-band D/C=1 bit.
  return write_9bit(panel, static_cast<const uint8_t *>(color_data),
                    byte_count, true);
}

esp_err_t panel_mirror(esp_lcd_panel_t *base, bool mirror_x, bool mirror_y) {
  auto *panel = to_panel(base);
  panel->mirror_x = mirror_x;
  panel->mirror_y = mirror_y;
  return apply_madctl(panel);
}

esp_err_t panel_swap_xy(esp_lcd_panel_t *base, bool swap_axes) {
  auto *panel = to_panel(base);
  panel->swap_xy = swap_axes;
  return apply_madctl(panel);
}

esp_err_t panel_set_gap(esp_lcd_panel_t *base, int x_gap, int y_gap) {
  auto *panel = to_panel(base);
  panel->x_gap = x_gap;
  panel->y_gap = y_gap;
  return ESP_OK;
}

esp_err_t panel_invert(esp_lcd_panel_t *base, bool invert) {
  return send_command(to_panel(base),
                      invert ? kCmdInversionOn : kCmdInversionOff);
}

esp_err_t panel_display(esp_lcd_panel_t *base, bool on) {
  return send_command(to_panel(base), on ? kCmdDisplayOn : kCmdDisplayOff);
}

esp_err_t panel_sleep(esp_lcd_panel_t *base, bool sleep) {
  const esp_err_t result =
      send_command(to_panel(base), sleep ? kCmdSleepIn : kCmdSleepOut);
  vTaskDelay(pdMS_TO_TICKS(120));
  return result;
}

} // namespace

esp_err_t device_st7789_3wire_create_panel(
    spi_host_device_t spi_host, const DeviceSpiLcdConfig &config,
    esp_lcd_panel_handle_t *panel_out) {
  if (panel_out == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  *panel_out = nullptr;

  auto *panel = static_cast<St7789ThreeWirePanel *>(
      std::calloc(1, sizeof(St7789ThreeWirePanel)));
  if (panel == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  panel->packed = static_cast<uint8_t *>(heap_caps_malloc(
      kPackedChunkBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (panel->packed == nullptr) {
    std::free(panel);
    return ESP_ERR_NO_MEM;
  }

  spi_device_interface_config_t device = {};
  device.mode = config.spi_mode;
  device.clock_speed_hz = static_cast<int>(config.pixel_clock_hz);
  device.spics_io_num = static_cast<gpio_num_t>(config.cs_gpio);
  device.queue_size = 1;
  device.flags = SPI_DEVICE_HALFDUPLEX;
  esp_err_t result = spi_bus_add_device(spi_host, &device, &panel->spi);
  if (result != ESP_OK) {
    heap_caps_free(panel->packed);
    std::free(panel);
    return result;
  }

  panel->reset_gpio = config.reset_gpio;
  panel->rgb_order_bgr = config.rgb_order_bgr;
  panel->base.reset = panel_reset;
  panel->base.init = panel_init;
  panel->base.del = panel_del;
  panel->base.draw_bitmap = panel_draw_bitmap;
  panel->base.draw_bitmap_2d = nullptr;
  panel->base.mirror = panel_mirror;
  panel->base.swap_xy = panel_swap_xy;
  panel->base.set_gap = panel_set_gap;
  panel->base.invert_color = panel_invert;
  panel->base.disp_on_off = panel_display;
  panel->base.disp_sleep = panel_sleep;
  panel->base.set_brightness = nullptr;
  panel->base.user_data = nullptr;

  if (valid_gpio(panel->reset_gpio)) {
    gpio_config_t reset = {};
    reset.pin_bit_mask = 1ULL << panel->reset_gpio;
    reset.mode = GPIO_MODE_OUTPUT;
    reset.pull_up_en = GPIO_PULLUP_DISABLE;
    reset.pull_down_en = GPIO_PULLDOWN_DISABLE;
    reset.intr_type = GPIO_INTR_DISABLE;
    result = gpio_config(&reset);
    if (result != ESP_OK) {
      (void)panel_del(&panel->base);
      return result;
    }
  }

  *panel_out = &panel->base;
  ESP_LOGI(kTag,
           "panel facade ready host=%d mode=%d clock=%lu cs=%d rst=%d",
           static_cast<int>(spi_host), config.spi_mode,
           static_cast<unsigned long>(config.pixel_clock_hz), config.cs_gpio,
           config.reset_gpio);
  return ESP_OK;
}

} // namespace lunaredu::device
