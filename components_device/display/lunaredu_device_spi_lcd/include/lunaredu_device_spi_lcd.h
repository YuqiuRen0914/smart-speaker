#pragma once

#include <stdint.h>
#include <string>

#include <esp_err.h>

typedef struct esp_lcd_panel_io_t *esp_lcd_panel_io_handle_t;
typedef struct esp_lcd_panel_t *esp_lcd_panel_handle_t;

namespace lunaredu::device {

enum class DeviceSpiLcdController {
  kSt7789,
  kIli9488,
};

enum class DeviceSpiLcdHost {
  kSpi2,
  kSpi3,
};

struct DeviceSpiLcdConfig {
  bool enabled = false;
  DeviceSpiLcdController controller = DeviceSpiLcdController::kSt7789;
  DeviceSpiLcdHost spi_host = DeviceSpiLcdHost::kSpi2;
  // Three-wire ST7789 panels carry the D/C value as the ninth bit of every
  // command/data byte and therefore do not expose a dedicated D/C GPIO.
  bool three_wire_9bit = false;
  int mosi_gpio = -1;
  int sclk_gpio = -1;
  int miso_gpio = -1;
  int cs_gpio = -1;
  int dc_gpio = -1;
  int reset_gpio = -1;
  int backlight_gpio = -1;
  bool backlight_invert = false;
  uint32_t pixel_clock_hz = 20 * 1000 * 1000;
  int spi_mode = 0;
  uint16_t width = 160;
  uint16_t height = 80;
  int offset_x = 1;
  int offset_y = 26;
  bool mirror_x = false;
  bool mirror_y = false;
  bool swap_xy = false;
  bool invert_color = false;
  bool rgb_order_bgr = false;
  bool claim_bsp_spi_resource = true;
};

struct DeviceSpiLcdPanel {
  esp_lcd_panel_io_handle_t panel_io = nullptr;
  esp_lcd_panel_handle_t panel = nullptr;
  DeviceSpiLcdController controller = DeviceSpiLcdController::kSt7789;
  uint16_t width = 0;
  uint16_t height = 0;
  int offset_x = 0;
  int offset_y = 0;
  bool mirror_x = false;
  bool mirror_y = false;
  bool swap_xy = false;
  bool rgb565_panel_byte_swap = true;
  int backlight_gpio = -1;
  bool backlight_invert = false;
};

struct DeviceSpiLcdStatus {
  bool initialized = false;
  bool three_wire_9bit = false;
  int mosi_gpio = -1;
  int sclk_gpio = -1;
  int cs_gpio = -1;
  int dc_gpio = -1;
  uint16_t width = 0;
  uint16_t height = 0;
  DeviceSpiLcdController controller = DeviceSpiLcdController::kSt7789;
  std::string last_error;
};

DeviceSpiLcdConfig device_spi_lcd_config_from_kconfig();
void device_spi_lcd_log_config(const DeviceSpiLcdConfig &config);
esp_err_t device_spi_lcd_create_panel(const DeviceSpiLcdConfig &config,
                                      DeviceSpiLcdPanel *panel);
esp_err_t device_spi_lcd_destroy_panel(DeviceSpiLcdPanel *panel);
esp_err_t device_spi_lcd_release_adopted_panel(DeviceSpiLcdPanel *panel);
DeviceSpiLcdStatus device_spi_lcd_status();

} // namespace lunaredu::device
