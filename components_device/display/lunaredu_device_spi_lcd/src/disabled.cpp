#include "sdkconfig.h"
#include <string>
#include <utility>
#include "lunaredu_device_spi_lcd.h"
#include "lunaredu_device_spi_lcd_surface.h"
#include "spi_lcd_surface_local_api.h"

namespace {
[[maybe_unused]] constexpr const char *kDisabledError = "disabled";
[[maybe_unused]] void set_error(std::string &error, const char *message = kDisabledError) {
  error = message == nullptr ? kDisabledError : message;
}
}

#if !CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD

namespace lunaredu::device {

DeviceSpiLcdConfig device_spi_lcd_config_from_kconfig() { return {}; }

void device_spi_lcd_log_config(const DeviceSpiLcdConfig &) {}

esp_err_t device_spi_lcd_create_panel(const DeviceSpiLcdConfig &,
                                      DeviceSpiLcdPanel *panel) {
  if (panel != nullptr) {
    *panel = DeviceSpiLcdPanel();
  }
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t device_spi_lcd_destroy_panel(DeviceSpiLcdPanel *panel) {
  if (panel != nullptr) {
    *panel = DeviceSpiLcdPanel();
  }
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t device_spi_lcd_release_adopted_panel(DeviceSpiLcdPanel *panel) {
  if (panel != nullptr) {
    *panel = DeviceSpiLcdPanel();
  }
  return ESP_ERR_NOT_SUPPORTED;
}

DeviceSpiLcdStatus device_spi_lcd_status() {
  DeviceSpiLcdStatus status;
  status.last_error = kDisabledError;
  return status;
}

bool device_spi_lcd_surface_setup() { return false; }

bool device_spi_lcd_surface_ready() { return false; }

bool device_spi_lcd_surface_get_panel(DeviceSpiLcdPanel *panel) {
  if (panel != nullptr) {
    *panel = DeviceSpiLcdPanel();
  }
  return false;
}

bool device_spi_lcd_surface_draw_pattern(const std::string &, uint16_t,
                                         std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_fill(uint16_t, std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_fill_rect(uint16_t, uint16_t, uint16_t, uint16_t,
                                      uint16_t, std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

void device_spi_lcd_surface_set_canvas_override(bool) {}

bool device_spi_lcd_surface_canvas_override_active() { return false; }

bool device_spi_lcd_surface_draw_rgb565_cover(const uint16_t *, uint16_t,
                                              uint16_t, bool,
                                              std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_draw_rgb565_bitmap(uint16_t, uint16_t, uint16_t,
                                               uint16_t, const uint16_t *,
                                               bool, std::string *error,
                                               DeviceSpiLcdTransferMode) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_draw_rgb666_bitmap(
    uint16_t, uint16_t, uint16_t, uint16_t, const uint8_t *,
    DeviceSpiLcdRgb666Order, std::string *error, DeviceSpiLcdTransferMode) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_fill_rgb666(uint32_t, std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_fill_rect_rgb666(
    uint16_t, uint16_t, uint16_t, uint16_t, uint32_t, std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_wait_idle(std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_set_pixel_mode(DeviceSpiLcdPixelMode,
                                            std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

DeviceSpiLcdPixelMode device_spi_lcd_surface_pixel_mode() {
  return DeviceSpiLcdPixelMode::kRgb666;
}

DeviceSpiLcdSourcePixelFormat device_spi_lcd_surface_source_pixel_format() {
  return DeviceSpiLcdSourcePixelFormat::kRgb565;
}

bool device_spi_lcd_surface_benchmark_full_frame(
    DeviceSpiLcdPixelMode, uint16_t, DeviceSpiLcdFullFrameBenchmark *,
    std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_show_full_frame(DeviceSpiLcdPixelMode,
                                             std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_configure_hardware_scroll(
    DeviceSpiLcdHardwareScrollAxis, uint16_t, uint16_t, uint16_t,
    std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_set_hardware_scroll_offset(
    uint16_t, std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_reset_hardware_scroll(std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

bool device_spi_lcd_surface_draw_rgb565_bitmap_background(
    uint16_t, uint16_t, uint16_t, uint16_t, const uint16_t *, bool,
    std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}


bool device_spi_lcd_surface_draw_rgb666_bitmap_background(
    uint16_t, uint16_t, uint16_t, uint16_t, const uint8_t *,
    DeviceSpiLcdRgb666Order, std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return false;
}

DeviceSpiLcdSurfaceStatus device_spi_lcd_surface_status() {
  DeviceSpiLcdSurfaceStatus status;
  status.last_error = kDisabledError;
  status.pixel_mode = DeviceSpiLcdPixelMode::kRgb666;
  return status;
}

esp_err_t device_spi_lcd_surface_write_bmp(
    const DeviceSpiLcdSurfaceBmpWriter &, std::string *error) {
  if (error != nullptr) {
    set_error(*error);
  }
  return ESP_ERR_NOT_SUPPORTED;
}

} // namespace lunaredu::device

#endif

