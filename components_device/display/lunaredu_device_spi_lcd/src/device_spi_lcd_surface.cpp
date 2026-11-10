#include "lunaredu_device_spi_lcd_surface.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "ili9488_panel.h"
#include "spi_lcd_surface_local_api.h"

namespace lunaredu::device {
namespace {

constexpr const char *kTag = "device_spi_lcd_surface";

std::recursive_mutex g_mutex;
DeviceSpiLcdPanel g_panel;
bool g_panel_initialized = false;
std::string g_last_error;
std::string g_last_pattern;
uint32_t g_frame_count = 0;
std::vector<uint16_t> g_framebuffer;
bool g_framebuffer_valid = false;
bool g_framebuffer_disabled = true;
uint32_t g_framebuffer_generation = 0;
bool g_canvas_override_active = false;
uint16_t *g_panel_dma_pixels = nullptr;
size_t g_panel_dma_pixel_capacity = 0;
constexpr uint16_t kIli9488NativeScrollLines = 480;
bool g_hardware_scroll_active = false;
uint16_t g_hardware_scroll_fixed_start = 0;
uint16_t g_hardware_scroll_extent = kIli9488NativeScrollLines;
uint16_t g_hardware_scroll_fixed_end = 0;
uint16_t g_hardware_scroll_offset = 0;

void set_last_error(const std::string &error) {
  g_last_error = error;
  if (!error.empty()) {
    ESP_LOGW(kTag, "%s", error.c_str());
  }
}

void clear_error_out(std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
}

void copy_error_out(std::string *error) {
  if (error != nullptr) {
    *error = g_last_error;
  }
}

bool wait_for_panel_transfer(const char *context) {
  if (g_panel.panel_io == nullptr) {
    return true;
  }

  const esp_err_t result =
      g_panel.controller == DeviceSpiLcdController::kIli9488
          ? lunaredu_esp_lcd_ili9488_wait_idle(g_panel.panel)
          : esp_lcd_panel_io_tx_param(g_panel.panel_io, -1, nullptr, 0);
  if (result != ESP_OK) {
    set_last_error(std::string(context) + ":" + esp_err_to_name(result));
    return false;
  }
  return true;
}

uint16_t swap_rgb565_bytes(uint16_t pixel) {
  return static_cast<uint16_t>((pixel << 8) | (pixel >> 8));
}

uint16_t panel_rgb565(uint16_t pixel) {
  return g_panel.rgb565_panel_byte_swap ? swap_rgb565_bytes(pixel) : pixel;
}

uint16_t rgb666_to_rgb565(uint32_t color) {
  const uint8_t red = static_cast<uint8_t>((color >> 16) & 0xFCU);
  const uint8_t green = static_cast<uint8_t>((color >> 8) & 0xFCU);
  const uint8_t blue = static_cast<uint8_t>(color & 0xFCU);
  return static_cast<uint16_t>(((red & 0xF8U) << 8) |
                               ((green & 0xFCU) << 3) | (blue >> 3));
}

const char *controller_name(DeviceSpiLcdController controller) {
  switch (controller) {
  case DeviceSpiLcdController::kIli9488:
    return "ILI9488";
  case DeviceSpiLcdController::kSt7789:
  default:
    return "ST7789";
  }
}

DeviceSpiLcdHardwareScrollAxis hardware_scroll_axis_locked() {
  if (!g_panel_initialized ||
      g_panel.controller != DeviceSpiLcdController::kIli9488) {
    return DeviceSpiLcdHardwareScrollAxis::kNone;
  }
  return g_panel.swap_xy ? DeviceSpiLcdHardwareScrollAxis::kHorizontal
                         : DeviceSpiLcdHardwareScrollAxis::kVertical;
}

uint16_t hardware_scroll_axis_extent_locked() {
  switch (hardware_scroll_axis_locked()) {
  case DeviceSpiLcdHardwareScrollAxis::kHorizontal:
    return g_panel.width;
  case DeviceSpiLcdHardwareScrollAxis::kVertical:
    return g_panel.height;
  case DeviceSpiLcdHardwareScrollAxis::kNone:
  default:
    return 0;
  }
}

bool ensure_panel_dma_buffer(size_t pixel_count, bool report_error = true) {
  if (pixel_count == 0) {
    return false;
  }
  if (g_panel_dma_pixels != nullptr &&
      g_panel_dma_pixel_capacity >= pixel_count) {
    return true;
  }

  if (g_panel_dma_pixels != nullptr) {
    heap_caps_free(g_panel_dma_pixels);
    g_panel_dma_pixels = nullptr;
    g_panel_dma_pixel_capacity = 0;
  }

  g_panel_dma_pixels = static_cast<uint16_t *>(heap_caps_malloc(
      pixel_count * sizeof(uint16_t),
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (g_panel_dma_pixels == nullptr) {
    if (report_error) {
      set_last_error("lcd_dma_buffer_alloc_failed");
    }
    return false;
  }
  g_panel_dma_pixel_capacity = pixel_count;
  return true;
}

bool ensure_framebuffer() {
  if (g_framebuffer_disabled || !g_panel_initialized || g_panel.width == 0 ||
      g_panel.height == 0) {
    return false;
  }

  const size_t pixel_count = static_cast<size_t>(g_panel.width) * g_panel.height;
  if (g_framebuffer.size() != pixel_count) {
    std::vector<uint16_t> next;
    next.resize(pixel_count, 0x0000);
    if (next.size() != pixel_count) {
      g_framebuffer.clear();
      g_framebuffer.shrink_to_fit();
      g_framebuffer_valid = false;
      g_framebuffer_disabled = true;
      ESP_LOGW(kTag, "display mirror framebuffer disabled: no memory");
      return false;
    }
    g_framebuffer = std::move(next);
    g_framebuffer_valid = false;
    ++g_framebuffer_generation;
  }
  return true;
}

void mirror_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                 uint16_t color) {
  if (!ensure_framebuffer() || width == 0 || height == 0 || x >= g_panel.width ||
      y >= g_panel.height) {
    return;
  }

  const uint16_t x_end =
      std::min<uint16_t>(g_panel.width, static_cast<uint16_t>(x + width));
  const uint16_t y_end =
      std::min<uint16_t>(g_panel.height, static_cast<uint16_t>(y + height));
  for (uint16_t row = y; row < y_end; ++row) {
    auto begin = g_framebuffer.begin() +
                 static_cast<size_t>(row) * g_panel.width + x;
    std::fill(begin, begin + (x_end - x), color);
  }
  g_framebuffer_valid = true;
  ++g_framebuffer_generation;
}

void mirror_bitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                   const uint16_t *pixels) {
  if (!ensure_framebuffer() || pixels == nullptr || width == 0 || height == 0 ||
      x >= g_panel.width || y >= g_panel.height) {
    return;
  }

  const uint16_t copy_width =
      std::min<uint16_t>(width, static_cast<uint16_t>(g_panel.width - x));
  const uint16_t copy_height =
      std::min<uint16_t>(height, static_cast<uint16_t>(g_panel.height - y));
  for (uint16_t row = 0; row < copy_height; ++row) {
    const uint16_t *source = pixels + static_cast<size_t>(row) * width;
    auto target = g_framebuffer.begin() +
                  static_cast<size_t>(y + row) * g_panel.width + x;
    std::copy(source, source + copy_width, target);
  }
  g_framebuffer_valid = true;
  ++g_framebuffer_generation;
}

bool draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
               uint16_t color) {
  if (!g_panel_initialized || g_panel.panel == nullptr) {
    set_last_error("display_not_initialized");
    return false;
  }
  if (width == 0 || height == 0 || x >= g_panel.width || y >= g_panel.height) {
    return true;
  }

  const uint16_t x_end =
      std::min<uint16_t>(g_panel.width, static_cast<uint16_t>(x + width));
  const uint16_t y_end =
      std::min<uint16_t>(g_panel.height, static_cast<uint16_t>(y + height));
  if (x_end <= x || y_end <= y) {
    return true;
  }

  const uint16_t draw_width = static_cast<uint16_t>(x_end - x);
  std::vector<uint16_t> line(draw_width, panel_rgb565(color));
  for (uint16_t row = y; row < y_end; ++row) {
    const esp_err_t result = esp_lcd_panel_draw_bitmap(
        g_panel.panel, x + g_panel.offset_x, row + g_panel.offset_y,
        x_end + g_panel.offset_x, row + 1 + g_panel.offset_y, line.data());
    if (result != ESP_OK) {
      set_last_error(std::string("draw_failed:") + esp_err_to_name(result));
      return false;
    }
    if (!wait_for_panel_transfer("draw_wait_failed")) {
      return false;
    }
  }
  mirror_rect(x, y, draw_width, static_cast<uint16_t>(y_end - y), color);
  return true;
}

bool fill_screen(uint16_t color) {
  if (!draw_rect(0, 0, g_panel.width, g_panel.height, color)) {
    return false;
  }
  ++g_frame_count;
  return true;
}

bool draw_color_bars() {
  constexpr uint16_t colors[] = {
      0xF800, 0xFD20, 0xFFE0, 0x07E0, 0x07FF, 0x001F, 0xF81F, 0xFFFF,
  };
  const uint16_t bar_count =
      static_cast<uint16_t>(sizeof(colors) / sizeof(colors[0]));
  const uint16_t bar_width =
      std::max<uint16_t>(1, static_cast<uint16_t>(g_panel.width / bar_count));

  for (uint16_t index = 0; index < bar_count; ++index) {
    const uint16_t x = static_cast<uint16_t>(index * bar_width);
    const uint16_t width =
        index == bar_count - 1 ? static_cast<uint16_t>(g_panel.width - x)
                               : bar_width;
    if (!draw_rect(x, 0, width, g_panel.height, colors[index])) {
      return false;
    }
  }
  ++g_frame_count;
  return true;
}

bool draw_orientation_pattern() {
  if (!fill_screen(0x0000)) {
    return false;
  }

  const uint16_t w = g_panel.width;
  const uint16_t h = g_panel.height;
  const uint16_t edge = std::max<uint16_t>(4, std::min<uint16_t>(w, h) / 12);
  const uint16_t marker = std::max<uint16_t>(8, std::min<uint16_t>(w, h) / 5);

  if (!draw_rect(0, 0, w, edge, 0xF800) ||
      !draw_rect(0, static_cast<uint16_t>(h - edge), w, edge, 0x001F) ||
      !draw_rect(0, 0, edge, h, 0x07E0) ||
      !draw_rect(static_cast<uint16_t>(w - edge), 0, edge, h, 0xFFE0)) {
    return false;
  }

  if (!draw_rect(0, 0, marker, marker, 0xFFFF) ||
      !draw_rect(static_cast<uint16_t>(w - marker), 0, marker, marker,
                 0xF81F) ||
      !draw_rect(0, static_cast<uint16_t>(h - marker), marker, marker,
                 0x07FF) ||
      !draw_rect(static_cast<uint16_t>(w - marker),
                 static_cast<uint16_t>(h - marker), marker, marker,
                 0xFD20)) {
    return false;
  }

  ++g_frame_count;
  return true;
}

bool draw_grid_pattern() {
  if (!fill_screen(0x0000)) {
    return false;
  }

  const uint16_t w = g_panel.width;
  const uint16_t h = g_panel.height;
  const uint16_t major = std::max<uint16_t>(20, std::min<uint16_t>(w, h) / 6);
  const uint16_t minor = std::max<uint16_t>(5, major / 4);

  for (uint16_t x = 0; x < w; x = static_cast<uint16_t>(x + minor)) {
    const bool major_line = (x % major) == 0;
    if (!draw_rect(x, 0, major_line ? 2 : 1, h,
                   major_line ? 0xFFFF : 0x4208)) {
      return false;
    }
  }
  for (uint16_t y = 0; y < h; y = static_cast<uint16_t>(y + minor)) {
    const bool major_line = (y % major) == 0;
    if (!draw_rect(0, y, w, major_line ? 2 : 1,
                   major_line ? 0xFFFF : 0x4208)) {
      return false;
    }
  }

  const uint16_t cross = std::max<uint16_t>(12, std::min<uint16_t>(w, h) / 10);
  const uint16_t cx = static_cast<uint16_t>(w / 2);
  const uint16_t cy = static_cast<uint16_t>(h / 2);
  return draw_rect(static_cast<uint16_t>(cx - cross), cy,
                   static_cast<uint16_t>(cross * 2), 2, 0xF800) &&
         draw_rect(cx, static_cast<uint16_t>(cy - cross), 2,
                   static_cast<uint16_t>(cross * 2), 0x07E0);
}

bool draw_checker_pattern() {
  const uint16_t cell =
      std::max<uint16_t>(8, std::min<uint16_t>(g_panel.width, g_panel.height) /
                                12);

  for (uint16_t y = 0; y < g_panel.height; y = static_cast<uint16_t>(y + cell)) {
    const uint16_t height =
        std::min<uint16_t>(cell, static_cast<uint16_t>(g_panel.height - y));
    for (uint16_t x = 0; x < g_panel.width;
         x = static_cast<uint16_t>(x + cell)) {
      const uint16_t width =
          std::min<uint16_t>(cell, static_cast<uint16_t>(g_panel.width - x));
      const bool light = ((x / cell) + (y / cell)) % 2 == 0;
      if (!draw_rect(x, y, width, height, light ? 0xFFFF : 0x0000)) {
        return false;
      }
    }
  }

  ++g_frame_count;
  return true;
}

bool draw_shape_pattern() {
  if (!fill_screen(0x0841)) {
    return false;
  }

  const uint16_t w = g_panel.width;
  const uint16_t h = g_panel.height;
  const uint16_t margin = std::max<uint16_t>(8, std::min<uint16_t>(w, h) / 18);
  const uint16_t block = std::max<uint16_t>(28, std::min<uint16_t>(w, h) / 4);

  if (!draw_rect(margin, margin, block, block, 0xF800) ||
      !draw_rect(static_cast<uint16_t>(w - margin - block), margin, block,
                 block, 0x07E0) ||
      !draw_rect(margin, static_cast<uint16_t>(h - margin - block), block,
                 block, 0x001F) ||
      !draw_rect(static_cast<uint16_t>(w - margin - block),
                 static_cast<uint16_t>(h - margin - block), block, block,
                 0xFFE0)) {
    return false;
  }

  const uint16_t ring_outer =
      std::max<uint16_t>(56, std::min<uint16_t>(w, h) / 2);
  const uint16_t ring_inner = static_cast<uint16_t>(ring_outer / 2);
  const uint16_t ring_x = static_cast<uint16_t>((w - ring_outer) / 2);
  const uint16_t ring_y = static_cast<uint16_t>((h - ring_outer) / 2);
  if (!draw_rect(ring_x, ring_y, ring_outer, ring_outer, 0x07FF) ||
      !draw_rect(static_cast<uint16_t>(ring_x + (ring_outer - ring_inner) / 2),
                 static_cast<uint16_t>(ring_y + (ring_outer - ring_inner) / 2),
                 ring_inner, ring_inner, 0x0841)) {
    return false;
  }

  for (uint16_t offset = 0; offset < std::min<uint16_t>(w, h); ++offset) {
    if (!draw_rect(offset, offset, 1, 1, 0xFFFF) ||
        !draw_rect(static_cast<uint16_t>(w - 1 - offset), offset, 1, 1,
                   0xF81F)) {
      return false;
    }
  }

  ++g_frame_count;
  return true;
}

bool draw_driver_diagnostic_pattern() {
  if (!fill_screen(0x0000)) {
    return false;
  }

  const uint16_t w = g_panel.width;
  const uint16_t h = g_panel.height;

  if (!draw_rect(0, 0, w, 2, 0xFFFF) ||
      !draw_rect(0, static_cast<uint16_t>(h - 2), w, 2, 0xFFFF) ||
      !draw_rect(0, 0, 2, h, 0xFFFF) ||
      !draw_rect(static_cast<uint16_t>(w - 2), 0, 2, h, 0xFFFF)) {
    return false;
  }

  for (uint16_t x = 8; x < w - 8; x = static_cast<uint16_t>(x + 8)) {
    const uint16_t height = (x % 16 == 0) ? 22 : 12;
    if (!draw_rect(x, 8, 1, height, 0xFFFF)) {
      return false;
    }
  }

  constexpr uint16_t colors[] = {
      0xF800, 0x07E0, 0x001F, 0xFFE0, 0x07FF, 0xF81F, 0xFFFF,
  };
  const uint16_t color_count =
      static_cast<uint16_t>(sizeof(colors) / sizeof(colors[0]));
  const uint16_t swatch_width = static_cast<uint16_t>((w - 24) / color_count);
  for (uint16_t index = 0; index < color_count; ++index) {
    const uint16_t x = static_cast<uint16_t>(12 + index * swatch_width);
    const uint16_t width =
        index == color_count - 1 ? static_cast<uint16_t>(w - 12 - x)
                                 : swatch_width;
    if (!draw_rect(x, 42, width, 24, colors[index])) {
      return false;
    }
  }

  for (uint16_t index = 0; index < 7; ++index) {
    const uint16_t y = static_cast<uint16_t>(86 + index * 10);
    const uint16_t thickness = static_cast<uint16_t>(index + 1);
    if (!draw_rect(16, y, static_cast<uint16_t>(w - 32), thickness, 0xFFFF)) {
      return false;
    }
  }

  for (uint16_t y = 0; y < 8; ++y) {
    for (uint16_t x = 0; x < 12; ++x) {
      const bool light = ((x + y) % 2) == 0;
      if (!draw_rect(static_cast<uint16_t>(24 + x * 8),
                     static_cast<uint16_t>(168 + y * 8), 8, 8,
                     light ? 0xFFFF : 0x0000)) {
        return false;
      }
    }
  }

  for (uint16_t offset = 0; offset < 72; ++offset) {
    const uint16_t x = static_cast<uint16_t>(144 + offset);
    const uint16_t y = static_cast<uint16_t>(168 + offset / 2);
    if (!draw_rect(x, y, 1, 1, 0x07E0) ||
        !draw_rect(static_cast<uint16_t>(w - 1 - x), y, 1, 1, 0xF800)) {
      return false;
    }
  }

  ++g_frame_count;
  return true;
}

bool ensure_panel_initialized_locked() {
  if (g_panel_initialized) {
    return true;
  }

  const auto config = device_spi_lcd_config_from_kconfig();
  const esp_err_t result = device_spi_lcd_create_panel(config, &g_panel);
  if (result != ESP_OK) {
    set_last_error(std::string("display_init_failed:") +
                   esp_err_to_name(result));
    return false;
  }

  g_panel_initialized = true;
  g_hardware_scroll_active = false;
  g_hardware_scroll_fixed_start = 0;
  g_hardware_scroll_extent = kIli9488NativeScrollLines;
  g_hardware_scroll_fixed_end = 0;
  g_hardware_scroll_offset = 0;
  set_last_error("");
  ESP_LOGI(kTag, "%s panel initialized size=%ux%u offset=%d,%d",
           controller_name(config.controller),
           static_cast<unsigned>(g_panel.width),
           static_cast<unsigned>(g_panel.height), g_panel.offset_x,
           g_panel.offset_y);
  return true;
}

bool draw_pattern_locked(const std::string &pattern, uint16_t color) {
  if (!ensure_panel_initialized_locked()) {
    return false;
  }

  bool ok = false;
  if (pattern == "solid") {
    ok = fill_screen(color);
  } else if (pattern == "grid") {
    ok = draw_grid_pattern();
  } else if (pattern == "checker" || pattern == "checkerboard") {
    ok = draw_checker_pattern();
  } else if (pattern == "shapes" || pattern == "geometry") {
    ok = draw_shape_pattern();
  } else if (pattern == "driver_diagnostic" || pattern == "diagnostic" ||
             pattern == "sharpness") {
    ok = draw_driver_diagnostic_pattern();
  } else if (pattern == "orientation") {
    ok = draw_orientation_pattern();
  } else {
    ok = draw_color_bars();
  }

  if (ok) {
    g_last_pattern = pattern;
    set_last_error("");
  }
  return ok;
}

void put_le16(uint8_t *target, uint16_t value) {
  target[0] = static_cast<uint8_t>(value & 0xff);
  target[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

void put_le32(uint8_t *target, uint32_t value) {
  target[0] = static_cast<uint8_t>(value & 0xff);
  target[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  target[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  target[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

uint8_t scale_5_to_8(uint16_t value) {
  return static_cast<uint8_t>((value * 255 + 15) / 31);
}

uint8_t scale_6_to_8(uint16_t value) {
  return static_cast<uint8_t>((value * 255 + 31) / 63);
}

} // namespace

bool register_device_spi_lcd_surface_projection(std::string &error) {
  return install_spi_lcd_surface_projection(error);
}

bool set_pixel_mode_locked(DeviceSpiLcdPixelMode mode) {
  if (!ensure_panel_initialized_locked()) {
    return false;
  }
  if (g_panel.controller != DeviceSpiLcdController::kIli9488) {
    set_last_error("pixel_mode_not_supported_by_panel");
    return false;
  }
  const auto panel_mode = mode == DeviceSpiLcdPixelMode::kRgb565
                              ? LunareduIli9488PixelMode::kRgb565
                              : LunareduIli9488PixelMode::kRgb666;
  const esp_err_t result =
      lunaredu_esp_lcd_ili9488_set_pixel_mode(g_panel.panel, panel_mode);
  if (result != ESP_OK) {
    set_last_error(std::string("pixel_mode_switch_failed:") +
                   esp_err_to_name(result));
    return false;
  }
  set_last_error("");
  return true;
}

bool draw_full_frame_for_benchmark_locked(const uint16_t *pixels,
                                          std::string *error) {
  const esp_err_t result = esp_lcd_panel_draw_bitmap(
      g_panel.panel, g_panel.offset_x, g_panel.offset_y,
      g_panel.width + g_panel.offset_x, g_panel.height + g_panel.offset_y,
      pixels);
  if (result != ESP_OK) {
    set_last_error(std::string("benchmark_draw_failed:") +
                   esp_err_to_name(result));
    copy_error_out(error);
    return false;
  }
  if (!wait_for_panel_transfer("benchmark_wait_failed")) {
    copy_error_out(error);
    return false;
  }
  clear_error_out(error);
  return true;
}

bool draw_full_frame_for_benchmark_locked(const uint8_t *pixels,
                                          std::string *error) {
  const esp_err_t result = lunaredu_esp_lcd_ili9488_draw_bitmap_rgb666(
      g_panel.panel, g_panel.offset_x, g_panel.offset_y,
      g_panel.width + g_panel.offset_x, g_panel.height + g_panel.offset_y,
      pixels, LunareduIli9488Rgb666Order::kRgb);
  if (result != ESP_OK) {
    set_last_error(std::string("benchmark_draw_failed:") +
                   esp_err_to_name(result));
    copy_error_out(error);
    return false;
  }
  if (!wait_for_panel_transfer("benchmark_wait_failed")) {
    copy_error_out(error);
    return false;
  }
  clear_error_out(error);
  return true;
}

bool device_spi_lcd_surface_setup() {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  const bool ready = ensure_panel_initialized_locked();
  std::string projection_error;
  if (!set_spi_lcd_surface_projection_available(ready, projection_error)) {
    ESP_LOGE(kTag, "failed to update screen projection availability: %s",
             projection_error.c_str());
  }
  return ready;
}

bool device_spi_lcd_surface_ready() {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  return g_panel_initialized;
}

bool device_spi_lcd_surface_get_panel(DeviceSpiLcdPanel *panel) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (panel == nullptr || !ensure_panel_initialized_locked()) {
    return false;
  }
  *panel = g_panel;
  return true;
}

bool device_spi_lcd_surface_draw_pattern(const std::string &pattern,
                                         uint16_t color, std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  const bool ok = draw_pattern_locked(pattern, color);
  if (ok) {
    clear_error_out(error);
  } else {
    copy_error_out(error);
  }
  return ok;
}

bool device_spi_lcd_surface_fill(uint16_t color, std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!ensure_panel_initialized_locked()) {
    copy_error_out(error);
    return false;
  }

  const bool ok = fill_screen(color);
  if (ok) {
    g_last_pattern = "fill";
    set_last_error("");
    clear_error_out(error);
  } else {
    copy_error_out(error);
  }
  return ok;
}

bool device_spi_lcd_surface_fill_rect(uint16_t x, uint16_t y, uint16_t width,
                                      uint16_t height, uint16_t color,
                                      std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!ensure_panel_initialized_locked()) {
    copy_error_out(error);
    return false;
  }

  const bool ok = draw_rect(x, y, width, height, color);
  if (ok) {
    ++g_frame_count;
    g_last_pattern = "rect";
    set_last_error("");
    clear_error_out(error);
  } else {
    copy_error_out(error);
  }
  return ok;
}

void device_spi_lcd_surface_set_canvas_override(bool active) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  g_canvas_override_active = active;
}

bool device_spi_lcd_surface_canvas_override_active() {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  return g_canvas_override_active;
}

bool device_spi_lcd_surface_draw_rgb565_cover(const uint16_t *pixels,
                                              uint16_t src_width,
                                              uint16_t src_height,
                                              bool swap_bytes,
                                              std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!ensure_panel_initialized_locked()) {
    copy_error_out(error);
    return false;
  }
  if (pixels == nullptr || src_width == 0 || src_height == 0) {
    set_last_error("invalid_rgb565_frame");
    copy_error_out(error);
    return false;
  }

  const uint16_t target_width = g_panel.width;
  const uint16_t target_height = g_panel.height;
  if (target_width == 0 || target_height == 0) {
    set_last_error("display_size_invalid");
    copy_error_out(error);
    return false;
  }

  uint32_t crop_x = 0;
  uint32_t crop_y = 0;
  uint32_t crop_width = src_width;
  uint32_t crop_height = src_height;
  if (static_cast<uint32_t>(src_width) * target_height >
      static_cast<uint32_t>(src_height) * target_width) {
    crop_width =
        std::max<uint32_t>(1, static_cast<uint32_t>(src_height) *
                                  target_width / target_height);
    crop_x = (static_cast<uint32_t>(src_width) - crop_width) / 2;
  } else {
    crop_height =
        std::max<uint32_t>(1, static_cast<uint32_t>(src_width) *
                                  target_height / target_width);
    crop_y = (static_cast<uint32_t>(src_height) - crop_height) / 2;
  }

  std::vector<uint16_t> line(target_width);
  std::vector<uint16_t> panel_line(target_width);
  for (uint16_t y = 0; y < target_height; ++y) {
    const uint32_t src_y =
        crop_y + static_cast<uint32_t>(y) * crop_height / target_height;
    const uint16_t *src_row = pixels + src_y * src_width;
    for (uint16_t x = 0; x < target_width; ++x) {
      const uint32_t src_x =
          crop_x + static_cast<uint32_t>(x) * crop_width / target_width;
      uint16_t pixel = src_row[src_x];
      if (swap_bytes) {
        pixel = swap_rgb565_bytes(pixel);
      }
      line[x] = pixel;
      panel_line[x] = panel_rgb565(pixel);
    }

    const esp_err_t result = esp_lcd_panel_draw_bitmap(
        g_panel.panel, g_panel.offset_x, y + g_panel.offset_y,
        target_width + g_panel.offset_x, y + 1 + g_panel.offset_y,
        panel_line.data());
    if (result != ESP_OK) {
      set_last_error(std::string("rgb565_cover_draw_failed:") +
                     esp_err_to_name(result));
      copy_error_out(error);
      return false;
    }
    if (!wait_for_panel_transfer("rgb565_cover_wait_failed")) {
      copy_error_out(error);
      return false;
    }
    if (ensure_framebuffer()) {
      auto target = g_framebuffer.begin() + static_cast<size_t>(y) * target_width;
      std::copy(line.begin(), line.end(), target);
    }
  }

  ++g_frame_count;
  g_framebuffer_valid = true;
  ++g_framebuffer_generation;
  g_last_pattern = "camera_preview";
  set_last_error("");
  clear_error_out(error);
  return true;
}

bool device_spi_lcd_surface_draw_rgb565_bitmap(uint16_t x, uint16_t y,
                                               uint16_t width,
                                               uint16_t height,
                                               const uint16_t *pixels,
                                               bool swap_bytes,
                                               std::string *error,
                                               DeviceSpiLcdTransferMode mode) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!ensure_panel_initialized_locked()) {
    copy_error_out(error);
    return false;
  }
  if (pixels == nullptr || width == 0 || height == 0) {
    set_last_error("invalid_rgb565_bitmap");
    copy_error_out(error);
    return false;
  }
  if (x >= g_panel.width || y >= g_panel.height ||
      static_cast<uint32_t>(x) + width > g_panel.width ||
      static_cast<uint32_t>(y) + height > g_panel.height) {
    set_last_error("rgb565_bitmap_out_of_bounds");
    copy_error_out(error);
    return false;
  }

  const size_t pixel_count = static_cast<size_t>(width) * height;

  // The ILI9488 adapter consumes canonical RGB565 synchronously and performs
  // its required RGB666 conversion into its own DMA buffer. Passing the LVGL
  // buffer straight through avoids a second internal-RAM DMA buffer and a
  // redundant full-area RGB565 copy. Native RGB565 panels still use the
  // generic DMA-safe path below.
  if (g_panel.controller == DeviceSpiLcdController::kIli9488 &&
      !swap_bytes && !g_panel.rgb565_panel_byte_swap) {
    const esp_err_t result = esp_lcd_panel_draw_bitmap(
        g_panel.panel, x + g_panel.offset_x, y + g_panel.offset_y,
        x + width + g_panel.offset_x, y + height + g_panel.offset_y, pixels);
    if (result != ESP_OK) {
      set_last_error(std::string("rgb565_bitmap_draw_failed:") +
                     esp_err_to_name(result));
      copy_error_out(error);
      return false;
    }
    if (mode == DeviceSpiLcdTransferMode::kWaitForCompletion &&
        !wait_for_panel_transfer("rgb565_bitmap_wait_failed")) {
      copy_error_out(error);
      return false;
    }
    ++g_frame_count;
    mirror_bitmap(x, y, width, height, pixels);
    g_last_pattern = "rgb565_frame";
    set_last_error("");
    clear_error_out(error);
    return true;
  }

  const bool full_frame_dma = ensure_panel_dma_buffer(pixel_count, false);
  if (!full_frame_dma) {
    // A 240x240 RGB565 frame is 115200 bytes, larger than the free internal
    // SRAM on service-rich products. Fall back to a DMA-safe scanline buffer;
    // synchronous/custom panels can then perform their own bounded packing.
    if (!ensure_panel_dma_buffer(width)) {
      copy_error_out(error);
      return false;
    }

    std::vector<uint16_t> canonical_line(width);
    for (uint16_t row = 0; row < height; ++row) {
      const uint16_t *source = pixels + static_cast<size_t>(row) * width;
      for (uint16_t column = 0; column < width; ++column) {
        const uint16_t pixel =
            swap_bytes ? swap_rgb565_bytes(source[column]) : source[column];
        canonical_line[column] = pixel;
        g_panel_dma_pixels[column] = panel_rgb565(pixel);
      }

      const esp_err_t result = esp_lcd_panel_draw_bitmap(
          g_panel.panel, x + g_panel.offset_x,
          y + row + g_panel.offset_y, x + width + g_panel.offset_x,
          y + row + 1 + g_panel.offset_y, g_panel_dma_pixels);
      if (result != ESP_OK) {
        set_last_error(std::string("rgb565_bitmap_draw_failed:") +
                       esp_err_to_name(result));
        copy_error_out(error);
        return false;
      }
      if (!wait_for_panel_transfer("rgb565_bitmap_wait_failed")) {
        copy_error_out(error);
        return false;
      }
      mirror_bitmap(x, static_cast<uint16_t>(y + row), width, 1,
                    canonical_line.data());
    }

    ++g_frame_count;
    g_last_pattern = "rgb565_frame";
    set_last_error("");
    clear_error_out(error);
    return true;
  }

  const uint16_t *mirror_pixels = pixels;
  std::vector<uint16_t> canonical_pixels;
  if (swap_bytes) {
    canonical_pixels.resize(pixel_count);
    for (size_t index = 0; index < pixel_count; ++index) {
      canonical_pixels[index] = swap_rgb565_bytes(pixels[index]);
    }
    mirror_pixels = canonical_pixels.data();
  }

  for (size_t index = 0; index < pixel_count; ++index) {
    const uint16_t pixel = swap_bytes ? canonical_pixels[index] : pixels[index];
    g_panel_dma_pixels[index] = panel_rgb565(pixel);
  }

  const esp_err_t result = esp_lcd_panel_draw_bitmap(
      g_panel.panel, x + g_panel.offset_x, y + g_panel.offset_y,
      x + width + g_panel.offset_x, y + height + g_panel.offset_y,
      g_panel_dma_pixels);
  if (result != ESP_OK) {
    set_last_error(std::string("rgb565_bitmap_draw_failed:") +
                   esp_err_to_name(result));
    copy_error_out(error);
    return false;
  }
  if (!wait_for_panel_transfer("rgb565_bitmap_wait_failed")) {
    copy_error_out(error);
    return false;
  }

  ++g_frame_count;
  mirror_bitmap(x, y, width, height, mirror_pixels);
  g_last_pattern = "rgb565_frame";
  set_last_error("");
  clear_error_out(error);
  return true;
}

bool device_spi_lcd_surface_draw_rgb666_bitmap(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    const uint8_t *pixels, DeviceSpiLcdRgb666Order order, std::string *error,
    DeviceSpiLcdTransferMode mode) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!ensure_panel_initialized_locked()) {
    copy_error_out(error);
    return false;
  }
  if (pixels == nullptr || width == 0 || height == 0) {
    set_last_error("invalid_rgb666_bitmap");
    copy_error_out(error);
    return false;
  }
  if (x >= g_panel.width || y >= g_panel.height ||
      static_cast<uint32_t>(x) + width > g_panel.width ||
      static_cast<uint32_t>(y) + height > g_panel.height) {
    set_last_error("rgb666_bitmap_out_of_bounds");
    copy_error_out(error);
    return false;
  }
  if (g_panel.controller != DeviceSpiLcdController::kIli9488) {
    set_last_error("rgb666_bitmap_not_supported_by_panel");
    copy_error_out(error);
    return false;
  }

  const auto panel_order = order == DeviceSpiLcdRgb666Order::kBgr
                               ? LunareduIli9488Rgb666Order::kBgr
                               : LunareduIli9488Rgb666Order::kRgb;
  const esp_err_t result = lunaredu_esp_lcd_ili9488_draw_bitmap_rgb666(
      g_panel.panel, x + g_panel.offset_x, y + g_panel.offset_y,
      x + width + g_panel.offset_x, y + height + g_panel.offset_y, pixels,
      panel_order);
  if (result != ESP_OK) {
    set_last_error(std::string("rgb666_bitmap_draw_failed:") +
                   esp_err_to_name(result));
    copy_error_out(error);
    return false;
  }
  if (mode == DeviceSpiLcdTransferMode::kWaitForCompletion &&
      !wait_for_panel_transfer("rgb666_bitmap_wait_failed")) {
    copy_error_out(error);
    return false;
  }
  ++g_frame_count;
  g_last_pattern = "rgb666_frame";
  set_last_error("");
  clear_error_out(error);
  return true;
}

bool device_spi_lcd_surface_wait_idle(std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!ensure_panel_initialized_locked()) {
    copy_error_out(error);
    return false;
  }
  if (!wait_for_panel_transfer("display_wait_idle_failed")) {
    copy_error_out(error);
    return false;
  }
  clear_error_out(error);
  return true;
}

bool device_spi_lcd_surface_set_pixel_mode(DeviceSpiLcdPixelMode mode,
                                            std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  const bool ok = set_pixel_mode_locked(mode);
  if (ok) {
    clear_error_out(error);
  } else {
    copy_error_out(error);
  }
  return ok;
}

bool device_spi_lcd_surface_fill_rgb666(uint32_t color, std::string *error) {
  const DeviceSpiLcdSurfaceStatus status = device_spi_lcd_surface_status();
  return device_spi_lcd_surface_fill_rect_rgb666(
      0, 0, status.mirror_width, status.mirror_height, color, error);
}

bool device_spi_lcd_surface_fill_rect_rgb666(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color,
    std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!ensure_panel_initialized_locked()) {
    copy_error_out(error);
    return false;
  }
  if (width == 0 || height == 0 || x >= g_panel.width || y >= g_panel.height ||
      static_cast<uint32_t>(x) + width > g_panel.width ||
      static_cast<uint32_t>(y) + height > g_panel.height) {
    set_last_error("rgb666_fill_rect_out_of_bounds");
    copy_error_out(error);
    return false;
  }

  if (g_panel.controller != DeviceSpiLcdController::kIli9488) {
    return device_spi_lcd_surface_fill_rect(x, y, width, height,
                                            rgb666_to_rgb565(color), error);
  }

  std::vector<uint8_t> line(static_cast<size_t>(width) * 3);
  const uint8_t red = static_cast<uint8_t>((color >> 16) & 0xFCU);
  const uint8_t green = static_cast<uint8_t>((color >> 8) & 0xFCU);
  const uint8_t blue = static_cast<uint8_t>(color & 0xFCU);
  for (uint16_t column = 0; column < width; ++column) {
    line[static_cast<size_t>(column) * 3] = red;
    line[static_cast<size_t>(column) * 3 + 1] = green;
    line[static_cast<size_t>(column) * 3 + 2] = blue;
  }
  for (uint16_t row = 0; row < height; ++row) {
    const esp_err_t result = lunaredu_esp_lcd_ili9488_draw_bitmap_rgb666(
        g_panel.panel, x + g_panel.offset_x, y + row + g_panel.offset_y,
        x + width + g_panel.offset_x, y + row + 1 + g_panel.offset_y,
        line.data(), LunareduIli9488Rgb666Order::kRgb);
    if (result != ESP_OK) {
      set_last_error(std::string("rgb666_fill_rect_failed:") +
                     esp_err_to_name(result));
      copy_error_out(error);
      return false;
    }
  }
  if (!wait_for_panel_transfer("rgb666_fill_rect_wait_failed")) {
    copy_error_out(error);
    return false;
  }
  ++g_frame_count;
  g_last_pattern = "rgb666_rect";
  set_last_error("");
  clear_error_out(error);
  return true;
}

DeviceSpiLcdPixelMode device_spi_lcd_surface_pixel_mode() {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!g_panel_initialized ||
      g_panel.controller != DeviceSpiLcdController::kIli9488) {
    return DeviceSpiLcdPixelMode::kRgb666;
  }
  LunareduIli9488PixelMode mode = LunareduIli9488PixelMode::kRgb666;
  if (lunaredu_esp_lcd_ili9488_get_pixel_mode(g_panel.panel, &mode) != ESP_OK) {
    return DeviceSpiLcdPixelMode::kRgb666;
  }
  return mode == LunareduIli9488PixelMode::kRgb565
             ? DeviceSpiLcdPixelMode::kRgb565
             : DeviceSpiLcdPixelMode::kRgb666;
}

DeviceSpiLcdSourcePixelFormat device_spi_lcd_surface_source_pixel_format() {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  const DeviceSpiLcdController controller =
      g_panel_initialized ? g_panel.controller
                          : device_spi_lcd_config_from_kconfig().controller;
  return controller == DeviceSpiLcdController::kIli9488
             ? DeviceSpiLcdSourcePixelFormat::kRgb666U8x3
             : DeviceSpiLcdSourcePixelFormat::kRgb565;
}

bool device_spi_lcd_surface_benchmark_full_frame(
    DeviceSpiLcdPixelMode mode, uint16_t frame_count,
    DeviceSpiLcdFullFrameBenchmark *result, std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (result == nullptr) {
    set_last_error("benchmark_result_missing");
    copy_error_out(error);
    return false;
  }
  *result = {};
  result->mode = mode;
  if (!ensure_panel_initialized_locked()) {
    copy_error_out(error);
    return false;
  }
  if (frame_count == 0) {
    set_last_error("benchmark_frame_count_invalid");
    copy_error_out(error);
    return false;
  }
  if (!set_pixel_mode_locked(mode)) {
    copy_error_out(error);
    return false;
  }

  const size_t pixel_count = static_cast<size_t>(g_panel.width) * g_panel.height;
  std::vector<uint16_t> rgb565_pixels;
  std::vector<uint8_t> rgb666_pixels;
  if (mode == DeviceSpiLcdPixelMode::kRgb565) {
    rgb565_pixels.resize(pixel_count);
  } else {
    rgb666_pixels.resize(pixel_count * 3U);
  }
  if ((mode == DeviceSpiLcdPixelMode::kRgb565 &&
       rgb565_pixels.size() != pixel_count) ||
      (mode == DeviceSpiLcdPixelMode::kRgb666 &&
       rgb666_pixels.size() != pixel_count * 3U)) {
    set_last_error("benchmark_pattern_alloc_failed");
    copy_error_out(error);
    (void)set_pixel_mode_locked(DeviceSpiLcdPixelMode::kRgb666);
    return false;
  }
  for (uint16_t y = 0; y < g_panel.height; ++y) {
    for (uint16_t x = 0; x < g_panel.width; ++x) {
      const size_t index = static_cast<size_t>(y) * g_panel.width + x;
      if (mode == DeviceSpiLcdPixelMode::kRgb565) {
        const uint16_t r = static_cast<uint16_t>((x * 31U) / g_panel.width);
        const uint16_t g = static_cast<uint16_t>((y * 63U) / g_panel.height);
        const uint16_t b = static_cast<uint16_t>(
            ((x + y) * 31U) / (g_panel.width + g_panel.height));
        rgb565_pixels[index] =
            static_cast<uint16_t>((r << 11) | (g << 5) | b);
      } else {
        const uint8_t r6 = static_cast<uint8_t>((x * 63U) / g_panel.width);
        const uint8_t g6 = static_cast<uint8_t>((y * 63U) / g_panel.height);
        const uint8_t b6 = static_cast<uint8_t>(
            ((x + y) * 63U) / (g_panel.width + g_panel.height));
        rgb666_pixels[index * 3U] = static_cast<uint8_t>(r6 << 2);
        rgb666_pixels[index * 3U + 1U] = static_cast<uint8_t>(g6 << 2);
        rgb666_pixels[index * 3U + 2U] = static_cast<uint8_t>(b6 << 2);
      }
    }
  }

  result->width = g_panel.width;
  result->height = g_panel.height;
  result->frame_count = frame_count;
  result->bytes_per_frame = static_cast<uint32_t>(
      pixel_count * (mode == DeviceSpiLcdPixelMode::kRgb565 ? 2U : 3U));
  uint64_t total_us = 0;
  uint32_t min_us = UINT32_MAX;
  uint32_t max_us = 0;
  for (uint16_t frame = 0; frame < frame_count; ++frame) {
    const int64_t started_us = esp_timer_get_time();
    const bool drawn = mode == DeviceSpiLcdPixelMode::kRgb565
                           ? draw_full_frame_for_benchmark_locked(
                                 rgb565_pixels.data(), error)
                           : draw_full_frame_for_benchmark_locked(
                                 rgb666_pixels.data(), error);
    if (!drawn) {
      (void)set_pixel_mode_locked(DeviceSpiLcdPixelMode::kRgb666);
      return false;
    }
    const uint32_t elapsed_us = static_cast<uint32_t>(
        std::max<int64_t>(1, esp_timer_get_time() - started_us));
    total_us += elapsed_us;
    min_us = std::min(min_us, elapsed_us);
    max_us = std::max(max_us, elapsed_us);
  }
  result->total_us = total_us;
  result->min_frame_us = min_us;
  result->max_frame_us = max_us;
  result->average_frame_us = static_cast<uint32_t>(total_us / frame_count);
  result->frames_per_second_x100 = static_cast<uint32_t>(
      (static_cast<uint64_t>(frame_count) * 100000000ULL) /
      std::max<uint64_t>(1, total_us));
  // Factory diagnostics must leave the normal UI in its native 18-bit mode.
  if (!set_pixel_mode_locked(DeviceSpiLcdPixelMode::kRgb666)) {
    copy_error_out(error);
    return false;
  }
  set_last_error("");
  clear_error_out(error);
  return true;
}

bool device_spi_lcd_surface_show_full_frame(DeviceSpiLcdPixelMode mode,
                                             std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!ensure_panel_initialized_locked()) {
    copy_error_out(error);
    return false;
  }
  if (!set_pixel_mode_locked(mode)) {
    copy_error_out(error);
    return false;
  }

  const size_t pixel_count = static_cast<size_t>(g_panel.width) * g_panel.height;
  std::vector<uint16_t> rgb565_pixels;
  std::vector<uint8_t> rgb666_pixels;
  if (mode == DeviceSpiLcdPixelMode::kRgb565) {
    rgb565_pixels.resize(pixel_count);
  } else {
    rgb666_pixels.resize(pixel_count * 3U);
  }
  if ((mode == DeviceSpiLcdPixelMode::kRgb565 &&
       rgb565_pixels.size() != pixel_count) ||
      (mode == DeviceSpiLcdPixelMode::kRgb666 &&
       rgb666_pixels.size() != pixel_count * 3U)) {
    set_last_error("display_test_pattern_alloc_failed");
    (void)set_pixel_mode_locked(DeviceSpiLcdPixelMode::kRgb666);
    copy_error_out(error);
    return false;
  }
  for (uint16_t y = 0; y < g_panel.height; ++y) {
    for (uint16_t x = 0; x < g_panel.width; ++x) {
      const size_t index = static_cast<size_t>(y) * g_panel.width + x;
      if (mode == DeviceSpiLcdPixelMode::kRgb565) {
        const uint16_t r = static_cast<uint16_t>((x * 31U) / g_panel.width);
        const uint16_t g = static_cast<uint16_t>((y * 63U) / g_panel.height);
        const uint16_t b = static_cast<uint16_t>(
            ((x + y) * 31U) / (g_panel.width + g_panel.height));
        rgb565_pixels[index] =
            static_cast<uint16_t>((r << 11) | (g << 5) | b);
      } else {
        const uint8_t r6 = static_cast<uint8_t>((x * 63U) / g_panel.width);
        const uint8_t g6 = static_cast<uint8_t>((y * 63U) / g_panel.height);
        const uint8_t b6 = static_cast<uint8_t>(
            ((x + y) * 63U) / (g_panel.width + g_panel.height));
        rgb666_pixels[index * 3U] = static_cast<uint8_t>(r6 << 2);
        rgb666_pixels[index * 3U + 1U] = static_cast<uint8_t>(g6 << 2);
        rgb666_pixels[index * 3U + 2U] = static_cast<uint8_t>(b6 << 2);
      }
    }
  }
  const bool drawn = mode == DeviceSpiLcdPixelMode::kRgb565
                         ? draw_full_frame_for_benchmark_locked(
                               rgb565_pixels.data(), error)
                         : draw_full_frame_for_benchmark_locked(
                               rgb666_pixels.data(), error);
  if (!drawn) {
    (void)set_pixel_mode_locked(DeviceSpiLcdPixelMode::kRgb666);
    return false;
  }
  g_last_pattern = mode == DeviceSpiLcdPixelMode::kRgb565
                       ? "rgb565_full_frame"
                       : "rgb666_full_frame";
  set_last_error("");
  clear_error_out(error);
  return true;
}

bool device_spi_lcd_surface_configure_hardware_scroll(
    DeviceSpiLcdHardwareScrollAxis axis, uint16_t fixed_start,
    uint16_t extent, uint16_t fixed_end, std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!ensure_panel_initialized_locked()) {
    copy_error_out(error);
    return false;
  }
  const DeviceSpiLcdHardwareScrollAxis supported_axis =
      hardware_scroll_axis_locked();
  if (supported_axis == DeviceSpiLcdHardwareScrollAxis::kNone) {
    set_last_error("hardware_scroll_not_supported");
    copy_error_out(error);
    return false;
  }
  if (axis != supported_axis) {
    set_last_error("hardware_scroll_axis_mismatch");
    copy_error_out(error);
    return false;
  }
  const uint32_t total = static_cast<uint32_t>(fixed_start) + extent + fixed_end;
  if (extent == 0 || total != kIli9488NativeScrollLines ||
      hardware_scroll_axis_extent_locked() != kIli9488NativeScrollLines) {
    set_last_error("hardware_scroll_requires_full_480_line_axis");
    copy_error_out(error);
    return false;
  }

  const esp_err_t configure_result =
      lunaredu_esp_lcd_ili9488_configure_vertical_scroll(
          g_panel.panel, fixed_start, extent, fixed_end);
  if (configure_result != ESP_OK) {
    set_last_error(std::string("hardware_scroll_configure_failed:") +
                   esp_err_to_name(configure_result));
    copy_error_out(error);
    return false;
  }
  const esp_err_t start_result =
      lunaredu_esp_lcd_ili9488_set_vertical_scroll_start(g_panel.panel,
                                                         fixed_start);
  if (start_result != ESP_OK) {
    set_last_error(std::string("hardware_scroll_start_failed:") +
                   esp_err_to_name(start_result));
    copy_error_out(error);
    return false;
  }

  g_hardware_scroll_fixed_start = fixed_start;
  g_hardware_scroll_extent = extent;
  g_hardware_scroll_fixed_end = fixed_end;
  g_hardware_scroll_offset = 0;
  g_hardware_scroll_active = fixed_start != 0 ||
                             extent != kIli9488NativeScrollLines ||
                             fixed_end != 0;
  set_last_error("");
  clear_error_out(error);
  return true;
}

bool device_spi_lcd_surface_set_hardware_scroll_offset(
    uint16_t offset, std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!ensure_panel_initialized_locked()) {
    copy_error_out(error);
    return false;
  }
  if (hardware_scroll_axis_locked() ==
          DeviceSpiLcdHardwareScrollAxis::kNone ||
      g_hardware_scroll_extent == 0) {
    set_last_error("hardware_scroll_not_configured");
    copy_error_out(error);
    return false;
  }
  const uint16_t normalized =
      static_cast<uint16_t>(offset % g_hardware_scroll_extent);
  const uint16_t absolute_start = static_cast<uint16_t>(
      g_hardware_scroll_fixed_start + normalized);
  const esp_err_t result =
      lunaredu_esp_lcd_ili9488_set_vertical_scroll_start(g_panel.panel,
                                                         absolute_start);
  if (result != ESP_OK) {
    set_last_error(std::string("hardware_scroll_start_failed:") +
                   esp_err_to_name(result));
    copy_error_out(error);
    return false;
  }
  g_hardware_scroll_offset = normalized;
  g_hardware_scroll_active = normalized != 0 ||
                             g_hardware_scroll_fixed_start != 0 ||
                             g_hardware_scroll_extent !=
                                 kIli9488NativeScrollLines ||
                             g_hardware_scroll_fixed_end != 0;
  set_last_error("");
  clear_error_out(error);
  return true;
}

bool device_spi_lcd_surface_reset_hardware_scroll(std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!ensure_panel_initialized_locked()) {
    copy_error_out(error);
    return false;
  }
  const DeviceSpiLcdHardwareScrollAxis axis = hardware_scroll_axis_locked();
  if (axis == DeviceSpiLcdHardwareScrollAxis::kNone) {
    set_last_error("hardware_scroll_not_supported");
    copy_error_out(error);
    return false;
  }
  return device_spi_lcd_surface_configure_hardware_scroll(
      axis, 0, kIli9488NativeScrollLines, 0, error);
}

bool device_spi_lcd_surface_draw_rgb565_bitmap_background(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    const uint16_t *pixels, bool swap_bytes, std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (g_canvas_override_active) {
    if (error != nullptr) {
      *error = "canvas_override_active";
    }
    return false;
  }
  return device_spi_lcd_surface_draw_rgb565_bitmap(
      x, y, width, height, pixels, swap_bytes, error);
}

bool device_spi_lcd_surface_draw_rgb666_bitmap_background(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    const uint8_t *pixels, DeviceSpiLcdRgb666Order order, std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (g_canvas_override_active) {
    if (error != nullptr) {
      *error = "canvas_override_active";
    }
    return false;
  }
  return device_spi_lcd_surface_draw_rgb666_bitmap(
      x, y, width, height, pixels, order, error);
}

DeviceSpiLcdSurfaceStatus device_spi_lcd_surface_status() {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  DeviceSpiLcdSurfaceStatus status;
  status.config = device_spi_lcd_config_from_kconfig();
  status.configured = status.config.enabled;
  status.initialized = g_panel_initialized;
  status.device_status = device_spi_lcd_status();
  status.pixel_mode = device_spi_lcd_surface_pixel_mode();
  status.source_pixel_format = device_spi_lcd_surface_source_pixel_format();
  status.last_pattern = g_last_pattern;
  status.frame_count = g_frame_count;
  status.mirror_available = g_framebuffer_valid;
  status.mirror_generation = g_framebuffer_generation;
  status.mirror_width = g_panel_initialized ? g_panel.width : status.config.width;
  status.mirror_height =
      g_panel_initialized ? g_panel.height : status.config.height;
  status.rgb565_panel_byte_swap = g_panel_initialized
                                      ? g_panel.rgb565_panel_byte_swap
                                      : status.config.controller ==
                                            DeviceSpiLcdController::kSt7789;
  status.canvas_override_active = g_canvas_override_active;
  status.hardware_scroll_axis = hardware_scroll_axis_locked();
  status.hardware_scroll_supported =
      status.hardware_scroll_axis != DeviceSpiLcdHardwareScrollAxis::kNone &&
      hardware_scroll_axis_extent_locked() == kIli9488NativeScrollLines;
  status.hardware_scroll_native_lines = status.hardware_scroll_supported
                                            ? kIli9488NativeScrollLines
                                            : 0;
  if (status.hardware_scroll_supported) {
    status.hardware_scroll_active = g_hardware_scroll_active;
    status.hardware_scroll_fixed_start = g_hardware_scroll_fixed_start;
    status.hardware_scroll_extent = g_hardware_scroll_extent;
    status.hardware_scroll_fixed_end = g_hardware_scroll_fixed_end;
    status.hardware_scroll_offset = g_hardware_scroll_offset;
  }
  if (g_panel_initialized &&
      g_panel.controller == DeviceSpiLcdController::kIli9488) {
    LunareduIli9488DmaPipelineStatus pipeline;
    if (lunaredu_esp_lcd_ili9488_get_dma_pipeline_status(g_panel.panel,
                                                         &pipeline) ==
        ESP_OK) {
      status.dma_pipeline_enabled = pipeline.buffer_count > 1;
      status.dma_pipeline_buffer_count = pipeline.buffer_count;
      status.dma_pipeline_buffer_rows = pipeline.buffer_rows;
      status.dma_pipeline_buffer_pixels = pipeline.buffer_pixels;
      status.dma_pipeline_max_in_flight = pipeline.max_in_flight;
      status.dma_pipeline_submitted = pipeline.submitted_transfers;
      status.dma_pipeline_completed = pipeline.completed_transfers;
      status.dma_pipeline_buffer_waits = pipeline.buffer_wait_count;
    }
  }
  status.last_error =
      !g_last_error.empty() ? g_last_error : status.device_status.last_error;
  return status;
}

esp_err_t device_spi_lcd_surface_write_bmp(
    const DeviceSpiLcdSurfaceBmpWriter &writer, std::string *error) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!g_framebuffer_valid || g_framebuffer.empty() || g_panel.width == 0 ||
      g_panel.height == 0) {
    if (error != nullptr) {
      *error = "display_frame_not_available";
    }
    return ESP_ERR_NOT_FOUND;
  }
  if (writer.begin_stream == nullptr || writer.send_chunk == nullptr ||
      writer.end_stream == nullptr) {
    if (error != nullptr) {
      *error = "display_frame_writer_missing";
    }
    return ESP_ERR_INVALID_ARG;
  }

  const uint32_t width = g_panel.width;
  const uint32_t height = g_panel.height;
  const uint32_t row_stride = (width * 3 + 3) & ~uint32_t{3};
  const uint32_t pixel_data_size = row_stride * height;
  const uint32_t file_size = 54 + pixel_data_size;

  std::array<uint8_t, 54> header = {};
  header[0] = 'B';
  header[1] = 'M';
  put_le32(&header[2], file_size);
  put_le32(&header[10], 54);
  put_le32(&header[14], 40);
  put_le32(&header[18], width);
  put_le32(&header[22], height);
  put_le16(&header[26], 1);
  put_le16(&header[28], 24);
  put_le32(&header[34], pixel_data_size);

  if (!writer.begin_stream(200, "image/bmp", writer.context) ||
      !writer.send_chunk(header.data(), header.size(), writer.context)) {
    if (error != nullptr) {
      *error = "display_frame_send_failed";
    }
    return ESP_FAIL;
  }

  std::vector<uint8_t> row(row_stride, 0);
  for (uint32_t y = 0; y < height; ++y) {
    const uint32_t source_y = height - 1 - y;
    const uint16_t *source =
        g_framebuffer.data() + static_cast<size_t>(source_y) * width;
    std::fill(row.begin(), row.end(), 0);
    for (uint32_t x = 0; x < width; ++x) {
      const uint16_t pixel = source[x];
      row[x * 3] = scale_5_to_8(pixel & 0x001f);
      row[x * 3 + 1] = scale_6_to_8((pixel >> 5) & 0x003f);
      row[x * 3 + 2] = scale_5_to_8((pixel >> 11) & 0x001f);
    }
    if (!writer.send_chunk(row.data(), row.size(), writer.context)) {
      if (error != nullptr) {
        *error = "display_frame_send_failed";
      }
      return ESP_FAIL;
    }
  }

  writer.end_stream(writer.context);
  clear_error_out(error);
  return ESP_OK;
}

} // namespace lunaredu::device
