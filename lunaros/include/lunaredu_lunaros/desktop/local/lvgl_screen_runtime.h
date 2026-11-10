#pragma once

#include <stdint.h>
#include <string>

namespace lunaredu::desktop {

enum class LvglScreenPixelFormat { Rgb565, Bgr666U8x3 };

// Product-injected physical surface. The callback may pipeline a private
// copy, but must stop reading pixels before returning, including on failure.
// Context and the selected format remain stable for the runtime's lifetime.
struct LvglScreenSurface {
  uint16_t width = 0;
  uint16_t height = 0;
  LvglScreenPixelFormat pixel_format = LvglScreenPixelFormat::Rgb565;
  void *context = nullptr;
  bool (*draw_bitmap)(void *context, uint16_t x, uint16_t y,
                      uint16_t width, uint16_t height, const uint8_t *pixels,
                      LvglScreenPixelFormat format, std::string &error) = nullptr;
};

using LvglScreenTouchReadFn = bool (*)(uint16_t *x, uint16_t *y,
                                       bool *pressed, void *context);

struct LvglScreenTouchEvent {
  bool pressed = false;
  uint16_t x = 0;
  uint16_t y = 0;
  uint32_t timestamp_ms = 0;
};

using LvglScreenTouchEventReadFn = bool (*)(LvglScreenTouchEvent *event,
                                            bool *has_more, void *context);

struct LvglScreenRuntimeConfig {
  uint16_t draw_buffer_rows = 16;
  LvglScreenTouchReadFn read_touch = nullptr;
  LvglScreenTouchEventReadFn read_touch_event = nullptr;
  void *touch_context = nullptr;
  LvglScreenSurface surface;
  // Runs on the flush worker. Must be nonblocking and must not call LVGL.
  void (*notify_flush)(uint16_t x, uint16_t y, uint16_t width, uint16_t height) = nullptr;
};

struct LvglScreenRuntimeStatus {
  bool initialized = false;
  uint16_t width = 0;
  uint16_t height = 0;
  bool touch_input = false;
  // Successful complete LVGL refreshes, independent of partial-buffer tiles.
  uint32_t refresh_count = 0;
  uint32_t flush_count = 0;
  uint32_t flush_fail_count = 0;
  uint32_t last_flush_us = 0;
  uint32_t max_flush_us = 0;
  uint64_t total_flush_us = 0;
  uint64_t flushed_pixels = 0;
  uint32_t last_refresh_pixels = 0;
  // Wall time from the first submitted tile to completion of the last tile.
  uint32_t last_refresh_us = 0;
  uint32_t max_refresh_us = 0;
  uint64_t total_refresh_us = 0;
  uint32_t full_refresh_count = 0;
  bool flush_pending = false;
  uint16_t last_refresh_x = 0;
  uint16_t last_refresh_y = 0;
  uint16_t last_refresh_width = 0;
  uint16_t last_refresh_height = 0;
  std::string last_error;
};

// A transfer that retains buffer ownership for 3 seconds triggers a system
// restart. Status reads remain independent of the blocked LVGL owner.
// Setup fails if configured touch input cannot be created; retry is safe.
bool setup_lvgl_screen_runtime(const LvglScreenRuntimeConfig &config);
bool lvgl_screen_runtime_ready();
LvglScreenRuntimeStatus lvgl_screen_runtime_status();
void lvgl_screen_runtime_handler();
// Pause only the display refresh timer while continuing to process input and
// other LVGL timers. Used while a direct framebuffer diagnostic owns the LCD.
void lvgl_screen_runtime_set_refresh_enabled(bool enabled);

// Render a deterministic RGB565 pattern through the normal LVGL render and
// flush path. This is a diagnostic overlay, not a production UI surface.
bool lvgl_screen_runtime_test_rgb565(std::string *error = nullptr);
void lvgl_screen_runtime_clear_rgb565_test();

} // namespace lunaredu::desktop
