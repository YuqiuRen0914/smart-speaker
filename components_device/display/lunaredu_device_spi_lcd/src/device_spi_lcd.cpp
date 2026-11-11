#include "lunaredu_device_spi_lcd.h"

#include <algorithm>
#include <mutex>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_check.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>

#include "ili9488_panel.h"
#include "bsp/lunaredu_bsp.h"
#include "sdkconfig.h"

#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_3WIRE_9BIT
#include "st7789_3wire_panel.h"
#endif

namespace lunaredu::device {
namespace {

constexpr const char *kLogTag = "device_spi_lcd";
constexpr const char *kOwner = "device_spi_lcd";

DeviceSpiLcdStatus g_status;
std::recursive_mutex g_mutex;
bool g_bsp_spi_bus_initialized = false;
bool g_local_spi_bus_initialized = false;
spi_host_device_t g_local_spi_host = SPI2_HOST;

const char *controller_name(DeviceSpiLcdController controller) {
  switch (controller) {
  case DeviceSpiLcdController::kIli9488:
    return "ILI9488";
  case DeviceSpiLcdController::kSt7789:
  default:
    return "ST7789";
  }
}

bool is_valid_gpio(int gpio) { return gpio >= 0 && gpio < GPIO_NUM_MAX; }

gpio_num_t to_gpio(int gpio) {
  return is_valid_gpio(gpio) ? static_cast<gpio_num_t>(gpio) : GPIO_NUM_NC;
}

esp_err_t set_backlight(const DeviceSpiLcdConfig &config, bool enabled,
                        bool release_pin = true) {
  if (!is_valid_gpio(config.backlight_gpio)) {
    return ESP_OK;
  }

  const bool level_on = enabled != config.backlight_invert;
  // Set the output latch before enabling the output driver, including when
  // the board uses an active-low backlight.
  esp_err_t result = gpio_set_level(to_gpio(config.backlight_gpio), level_on);
  if (result != ESP_OK) {
    return result;
  }

  gpio_config_t io_config = {};
  io_config.pin_bit_mask = 1ULL << config.backlight_gpio;
  io_config.mode = GPIO_MODE_OUTPUT;
  io_config.pull_up_en = GPIO_PULLUP_DISABLE;
  io_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_config.intr_type = GPIO_INTR_DISABLE;
  result = gpio_config(&io_config);
  if (result != ESP_OK) {
    return result;
  }

  if (!enabled && release_pin) {
    gpio_reset_pin(to_gpio(config.backlight_gpio));
  }
  return result;
}

// The ST7789 display-off command gates scanout, not GRAM writes. Clear in
// small strips so setup doesn't need a second framebuffer. The caller keeps
// the buffer until SPI has drained or the failed panel has been torn down.
constexpr int kStartupClearRows = 8;
esp_err_t clear_st7789_gram(esp_lcd_panel_handle_t panel,
                           esp_lcd_panel_io_handle_t io,
                           const DeviceSpiLcdConfig &config,
                           const uint16_t *black) {
  for (int y = 0; y < config.height; y += kStartupClearRows) {
    const int end = std::min<int>(y + kStartupClearRows, config.height);
    const esp_err_t result = esp_lcd_panel_draw_bitmap(
        panel, config.offset_x, y + config.offset_y,
        config.offset_x + config.width, end + config.offset_y, black);
    if (result != ESP_OK) {
      return result;
    }
  }
  // The three-wire driver writes synchronously. On the four-wire path, a
  // commandless polling transaction waits for the final queued color DMA.
  return io == nullptr ? ESP_OK : esp_lcd_panel_io_tx_param(io, -1, nullptr, 0);
}

spi_host_device_t to_spi_host(DeviceSpiLcdHost host) {
  switch (host) {
  case DeviceSpiLcdHost::kSpi3:
    return SPI3_HOST;
  case DeviceSpiLcdHost::kSpi2:
  default:
    return SPI2_HOST;
  }
}

void set_last_error(const char *error) {
  g_status.last_error = error == nullptr ? "unknown" : error;
}

void clear_last_error() { g_status.last_error.clear(); }

void release_setup_resources(esp_lcd_panel_io_handle_t panel_io,
                             esp_lcd_panel_handle_t panel,
                             bool release_panel_handles) {
  if (release_panel_handles && panel != nullptr) {
    esp_lcd_panel_del(panel);
  }
  if (release_panel_handles && panel_io != nullptr) {
    esp_lcd_panel_io_del(panel_io);
  }

  if (g_local_spi_bus_initialized) {
    const esp_err_t result = spi_bus_free(g_local_spi_host);
    if (result != ESP_OK) {
      ESP_LOGW(kLogTag, "SPI bus free failed: %s", esp_err_to_name(result));
    }
    g_local_spi_bus_initialized = false;
    g_local_spi_host = SPI2_HOST;
  }

  if (g_bsp_spi_bus_initialized) {
    const esp_err_t result = bsp_spi_deinit();
    if (result != ESP_OK) {
      ESP_LOGW(kLogTag, "BSP SPI deinit failed: %s", esp_err_to_name(result));
    }
    g_bsp_spi_bus_initialized = false;
  }
}

esp_err_t validate_config(const DeviceSpiLcdConfig &config) {
  if (!config.enabled) {
    set_last_error("disabled");
    return ESP_ERR_NOT_SUPPORTED;
  }
  if (!is_valid_gpio(config.mosi_gpio) || !is_valid_gpio(config.sclk_gpio) ||
      !is_valid_gpio(config.cs_gpio) ||
      (!config.three_wire_9bit && !is_valid_gpio(config.dc_gpio))) {
    set_last_error("invalid_gpio");
    return ESP_ERR_INVALID_ARG;
  }
  if (config.width == 0 || config.height == 0) {
    set_last_error("invalid_size");
    return ESP_ERR_INVALID_ARG;
  }
  return ESP_OK;
}

lcd_rgb_element_order_t rgb_order(const DeviceSpiLcdConfig &config) {
  return config.rgb_order_bgr ? LCD_RGB_ELEMENT_ORDER_BGR
                              : LCD_RGB_ELEMENT_ORDER_RGB;
}

bool pin_matches_bsp_spi_resource(int pin) {
  return pin < 0 || bsp_resource_contains_pin(BSP_RESOURCE_EXPANSION_SPI1, pin);
}

esp_err_t validate_bsp_spi_pins(const DeviceSpiLcdConfig &config) {
  if (!pin_matches_bsp_spi_resource(config.mosi_gpio) ||
      !pin_matches_bsp_spi_resource(config.miso_gpio) ||
      !pin_matches_bsp_spi_resource(config.sclk_gpio) ||
      !pin_matches_bsp_spi_resource(config.cs_gpio) ||
      !pin_matches_bsp_spi_resource(config.dc_gpio)) {
    set_last_error("bsp_spi_pin_mismatch");
    return ESP_ERR_INVALID_ARG;
  }
  return ESP_OK;
}

bsp_spi_bus_t make_bsp_spi_bus(const DeviceSpiLcdConfig &config) {
  return {
      .id = kOwner,
      .host = to_spi_host(config.spi_host),
      .mosi_pin = config.mosi_gpio,
      .miso_pin = config.miso_gpio,
      .sclk_pin = config.sclk_gpio,
      .max_transfer_sz = static_cast<int>(config.width) *
                         static_cast<int>(config.height) *
                         static_cast<int>(sizeof(uint16_t)),
      .resource = BSP_RESOURCE_EXPANSION_SPI1,
  };
}

} // namespace

DeviceSpiLcdConfig device_spi_lcd_config_from_kconfig() {
  DeviceSpiLcdConfig config;
#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD
  config.enabled = true;
  config.controller = DeviceSpiLcdController::kSt7789;
#ifdef CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_3WIRE_9BIT
  config.three_wire_9bit =
      CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_3WIRE_9BIT;
#endif
#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_ILI9488
  config.controller = DeviceSpiLcdController::kIli9488;
#endif
#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_SPI3_HOST
  config.spi_host = DeviceSpiLcdHost::kSpi3;
#else
  config.spi_host = DeviceSpiLcdHost::kSpi2;
#endif
  config.mosi_gpio = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_MOSI;
  config.sclk_gpio = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_SCLK;
  config.miso_gpio = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_MISO;
  config.cs_gpio = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_CS;
#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_3WIRE_9BIT
  config.dc_gpio = -1;
#else
  config.dc_gpio = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_DC;
#endif
  config.reset_gpio = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_RESET;
  config.backlight_gpio = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_BACKLIGHT;
#ifdef CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_BACKLIGHT_INVERT
  config.backlight_invert =
      CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_BACKLIGHT_INVERT;
#endif
  config.pixel_clock_hz =
      CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_PIXEL_CLOCK_HZ;
  config.spi_mode = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_SPI_MODE;
  config.width = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_WIDTH;
  config.height = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_HEIGHT;
  config.offset_x = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_OFFSET_X;
  config.offset_y = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_OFFSET_Y;
#ifdef CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_MIRROR_X
  config.mirror_x = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_MIRROR_X;
#endif
#ifdef CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_MIRROR_Y
  config.mirror_y = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_MIRROR_Y;
#endif
#ifdef CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_SWAP_XY
  config.swap_xy = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_SWAP_XY;
#endif
  config.invert_color = false;
#ifdef CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_INVERT_COLOR
  config.invert_color = CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_INVERT_COLOR;
#endif
#ifdef CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_RGB_ORDER_BGR
  config.rgb_order_bgr = true;
#endif
  config.claim_bsp_spi_resource = false;
#ifdef CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_CLAIM_BSP_SPI
  config.claim_bsp_spi_resource =
      CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_CLAIM_BSP_SPI;
#endif
#else
  config.enabled = false;
#endif
  return config;
}

void device_spi_lcd_log_config(const DeviceSpiLcdConfig &config) {
  const spi_host_device_t spi_host = to_spi_host(config.spi_host);
  ESP_LOGI(kLogTag,
           "enabled=%d wire=%s host=%d mosi=%d sclk=%d miso=%d cs=%d dc=%d rst=%d "
           "bl=%d bl_invert=%d pclk=%lu mode=%d size=%ux%u offset=%d,%d "
           "mirror=%d,%d swap_xy=%d invert=%d rgb_order=%s claim_bsp=%d "
           "controller=%s",
           config.enabled, config.three_wire_9bit ? "3wire-9bit" : "4wire",
           spi_host, config.mosi_gpio, config.sclk_gpio, config.miso_gpio,
           config.cs_gpio, config.dc_gpio,
           config.reset_gpio, config.backlight_gpio, config.backlight_invert,
           static_cast<unsigned long>(config.pixel_clock_hz), config.spi_mode,
           static_cast<unsigned>(config.width),
           static_cast<unsigned>(config.height), config.offset_x,
           config.offset_y, config.mirror_x, config.mirror_y, config.swap_xy,
           config.invert_color, config.rgb_order_bgr ? "BGR" : "RGB",
           config.claim_bsp_spi_resource, controller_name(config.controller));
}

esp_err_t device_spi_lcd_create_panel(const DeviceSpiLcdConfig &config,
                                      DeviceSpiLcdPanel *panel_out) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (panel_out == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  if (g_status.initialized) {
    set_last_error("already_initialized");
    return ESP_ERR_INVALID_STATE;
  }

  *panel_out = DeviceSpiLcdPanel();
  g_status = DeviceSpiLcdStatus();
  clear_last_error();
  device_spi_lcd_log_config(config);

  g_status.mosi_gpio = config.mosi_gpio;
  g_status.three_wire_9bit = config.three_wire_9bit;
  g_status.sclk_gpio = config.sclk_gpio;
  g_status.cs_gpio = config.cs_gpio;
  g_status.dc_gpio = config.dc_gpio;
  g_status.width = config.width;
  g_status.height = config.height;
  g_status.controller = config.controller;

  ESP_RETURN_ON_ERROR(validate_config(config), kLogTag,
                      "SPI LCD config is invalid");
  const bool st7789 = config.controller == DeviceSpiLcdController::kSt7789;

  const spi_host_device_t spi_host = to_spi_host(config.spi_host);

  if (config.claim_bsp_spi_resource) {
    ESP_RETURN_ON_ERROR(validate_bsp_spi_pins(config), kLogTag,
                        "display pins do not match BSP SPI resource");
    const bsp_spi_bus_t bus = make_bsp_spi_bus(config);
    const esp_err_t result = bsp_spi_init(&bus);
    if (result != ESP_OK) {
      const bsp_resource_claim_snapshot_t claim =
          bsp_resource_last_claim_snapshot();
      ESP_LOGE(kLogTag,
               "failed to initialize BSP SPI bus: %s owner=%s pin=%s",
               esp_err_to_name(result),
               claim.conflict_owner[0] != '\0' ? claim.conflict_owner
                                                : "(none)",
               claim.conflict_pin[0] != '\0' ? claim.conflict_pin : "(none)");
      set_last_error(esp_err_to_name(result));
      return result;
    }
    g_bsp_spi_bus_initialized = true;
  } else {
    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num = config.mosi_gpio;
    bus_config.miso_io_num = to_gpio(config.miso_gpio);
    bus_config.sclk_io_num = config.sclk_gpio;
    bus_config.quadwp_io_num = GPIO_NUM_NC;
    bus_config.quadhd_io_num = GPIO_NUM_NC;
    bus_config.max_transfer_sz =
        static_cast<int>(config.width) * static_cast<int>(config.height) *
        static_cast<int>(sizeof(uint16_t));

    const esp_err_t result =
        spi_bus_initialize(spi_host, &bus_config, SPI_DMA_CH_AUTO);
    // A local bus is owned by this panel. An occupied host gives no proof that
    // its wiring or DMA capacity matches this configuration; do not adopt it.
    if (result != ESP_OK) {
      set_last_error(result == ESP_ERR_INVALID_STATE ? "spi_bus_in_use"
                                                     : esp_err_to_name(result));
      release_setup_resources(nullptr, nullptr, true);
      return result;
    }
    g_local_spi_bus_initialized = true;
    g_local_spi_host = spi_host;
  }

  esp_lcd_panel_io_handle_t panel_io = nullptr;
  esp_lcd_panel_handle_t panel = nullptr;
  esp_err_t result = ESP_OK;

  if (config.three_wire_9bit) {
#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_3WIRE_9BIT
    result = device_st7789_3wire_create_panel(spi_host, config, &panel);
#else
    result = ESP_ERR_NOT_SUPPORTED;
#endif
  } else {
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = to_gpio(config.cs_gpio);
    io_config.dc_gpio_num = to_gpio(config.dc_gpio);
    io_config.spi_mode = config.spi_mode;
    io_config.pclk_hz = config.pixel_clock_hz;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    result = esp_lcd_new_panel_io_spi(spi_host, &io_config, &panel_io);
    if (result == ESP_OK) {
      esp_lcd_panel_dev_config_t panel_config = {};
      panel_config.reset_gpio_num = to_gpio(config.reset_gpio);
      panel_config.rgb_ele_order = rgb_order(config);
      panel_config.bits_per_pixel = 16;

      switch (config.controller) {
      case DeviceSpiLcdController::kIli9488:
        result = lunaredu_esp_lcd_new_panel_ili9488(panel_io, &panel_config,
                                                    &panel);
        break;
      case DeviceSpiLcdController::kSt7789:
      default:
        result = esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel);
        break;
      }
    }
  }
  if (result != ESP_OK) {
    set_last_error(esp_err_to_name(result));
    release_setup_resources(panel_io, panel, true);
    return result;
  }

  if (st7789) {
    result = set_backlight(config, false, false);
  }
  if (result == ESP_OK) {
    result = esp_lcd_panel_reset(panel);
  }
  if (result == ESP_OK) {
    result = esp_lcd_panel_init(panel);
  }
  if (result == ESP_OK) {
    result = esp_lcd_panel_invert_color(panel, config.invert_color);
  }
  if (result == ESP_OK) {
    result = esp_lcd_panel_swap_xy(panel, config.swap_xy);
  }
  if (result == ESP_OK) {
    result = esp_lcd_panel_mirror(panel, config.mirror_x, config.mirror_y);
  }
  uint16_t *black = nullptr;
  if (result == ESP_OK && st7789) {
    result = esp_lcd_panel_disp_on_off(panel, false);
    if (result == ESP_OK) {
      black = static_cast<uint16_t *>(heap_caps_calloc(
          static_cast<size_t>(config.width) *
              std::min<int>(kStartupClearRows, config.height),
          sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
      result = black == nullptr ? ESP_ERR_NO_MEM :
          clear_st7789_gram(panel, panel_io, config, black);
    }
    if (result == ESP_OK) {
      ESP_LOGI(kLogTag, "ST7789 startup black frame transferred before display-on");
    }
  }
  if (result == ESP_OK) {
    result = esp_lcd_panel_disp_on_off(panel, true);
    if (result == ESP_ERR_NOT_SUPPORTED) {
      ESP_LOGW(kLogTag, "panel does not support display on/off");
      result = ESP_OK;
    }
  }
  if (result == ESP_OK) {
    result = set_backlight(config, true);
    if (result == ESP_OK && is_valid_gpio(config.backlight_gpio)) {
      ESP_LOGI(kLogTag, "backlight on gpio=%d level=%d",
               config.backlight_gpio, config.backlight_invert ? 0 : 1);
    }
  }
  if (result != ESP_OK) {
    set_last_error(esp_err_to_name(result));
    release_setup_resources(panel_io, panel, true);
    if (black != nullptr) {
      heap_caps_free(black);
    }
    return result;
  }
  if (black != nullptr) {
    heap_caps_free(black);
  }

  panel_out->panel_io = panel_io;
  panel_out->panel = panel;
  panel_out->controller = config.controller;
  panel_out->width = config.width;
  panel_out->height = config.height;
  panel_out->offset_x = config.offset_x;
  panel_out->offset_y = config.offset_y;
  panel_out->mirror_x = config.mirror_x;
  panel_out->mirror_y = config.mirror_y;
  panel_out->swap_xy = config.swap_xy;
  panel_out->rgb565_panel_byte_swap =
      config.controller == DeviceSpiLcdController::kSt7789;
  panel_out->backlight_gpio = config.backlight_gpio;
  panel_out->backlight_invert = config.backlight_invert;

  g_status.initialized = true;
  clear_last_error();
  ESP_LOGI(kLogTag, "SPI LCD panel ready controller=%s",
           controller_name(config.controller));
  return ESP_OK;
}

esp_err_t device_spi_lcd_destroy_panel(DeviceSpiLcdPanel *panel) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (panel == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  if (panel->panel != nullptr) {
    const esp_err_t result = esp_lcd_panel_disp_on_off(panel->panel, false);
    if (result != ESP_OK && result != ESP_ERR_NOT_SUPPORTED) {
      ESP_LOGW(kLogTag, "panel display-off failed before destroy: %s",
               esp_err_to_name(result));
    }
  }
  if (panel->backlight_gpio >= 0) {
    DeviceSpiLcdConfig config;
    config.backlight_gpio = panel->backlight_gpio;
    config.backlight_invert = panel->backlight_invert;
    const esp_err_t result = set_backlight(config, false);
    if (result != ESP_OK) {
      ESP_LOGW(kLogTag, "backlight off failed before destroy: %s",
               esp_err_to_name(result));
    }
  }

  release_setup_resources(panel->panel_io, panel->panel, true);
  *panel = DeviceSpiLcdPanel();
  g_status = DeviceSpiLcdStatus();
  clear_last_error();
  return ESP_OK;
}

esp_err_t device_spi_lcd_release_adopted_panel(DeviceSpiLcdPanel *panel) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (panel == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  release_setup_resources(nullptr, nullptr, false);
  *panel = DeviceSpiLcdPanel();
  g_status = DeviceSpiLcdStatus();
  clear_last_error();
  return ESP_OK;
}

DeviceSpiLcdStatus device_spi_lcd_status() {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  return g_status;
}

} // namespace lunaredu::device
