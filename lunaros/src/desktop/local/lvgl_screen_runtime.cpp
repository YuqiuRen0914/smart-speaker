#include "lunaredu_lunaros/desktop/local/lvgl_screen_runtime.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <lvgl.h>

#include "lunaredu_lunaros/kernel/managed_task.h"

namespace lunaredu::desktop {
namespace {

constexpr const char *kTag = "lvgl_screen_runtime";
// The product executor can service LVGL every 10 ms.  LVGL otherwise creates
// both its display refresh and pointer-read timers from LV_DEF_REFR_PERIOD,
// which is 20 ms in the checked-in product config and adds a full scheduling
// window before a touch can become a LVGL event.
constexpr uint32_t kUiTimerPeriodMs = 10;
constexpr uint32_t kTouchReadPeriodMs = 5;
constexpr uint32_t kFlushWaitTimeoutMs = 3000;
constexpr uint32_t kFlushWaitPollMs = 250;
constexpr uint32_t kFlushTaskStackBytes = 4096;
constexpr UBaseType_t kFlushTaskPriority = 4;

struct LvglFlushRequest {
  lv_display_t *display = nullptr;
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  const uint8_t *pixels = nullptr;
  uint64_t started_us = 0;
  bool last_in_refresh = false;
};

std::recursive_mutex g_mutex;
std::recursive_mutex g_metrics_mutex;
LvglScreenRuntimeConfig g_config;
bool g_initialized = false;
bool g_refresh_enabled = true;
lv_display_t *g_display = nullptr;
lv_indev_t *g_touch_indev = nullptr;
uint8_t *g_draw_buffer = nullptr;
size_t g_draw_buffer_pixels = 0;
size_t g_draw_buffer_bytes = 0;
LvglScreenPixelFormat g_source_pixel_format = LvglScreenPixelFormat::Rgb565;
uint16_t g_width = 0;
uint16_t g_height = 0;
QueueHandle_t g_flush_queue = nullptr;
SemaphoreHandle_t g_flush_complete = nullptr;
TaskHandle_t g_flush_task = nullptr;
std::atomic<bool> g_flush_pending{false};
// Protected by the metrics mutex, never by the LVGL owner mutex. Diagnostics
// must remain readable while the owner waits for a transfer to finish.
struct {
  bool initialized = false;
  uint16_t width = 0;
  uint16_t height = 0;
  bool touch_input = false;
} g_setup_status;
uint64_t g_pending_started_us = 0;
struct RefreshAccumulator {
  bool active = false;
  bool failed = false;
  uint64_t started_us = 0;
  uint32_t pixels = 0;
  uint16_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
} g_refresh;
uint32_t g_flush_count = 0;
uint32_t g_refresh_count = 0;
uint32_t g_flush_fail_count = 0;
uint32_t g_last_flush_us = 0;
uint32_t g_max_flush_us = 0;
uint64_t g_total_flush_us = 0;
uint64_t g_flushed_pixels = 0;
uint32_t g_last_refresh_pixels = 0;
uint32_t g_last_refresh_us = 0;
uint32_t g_max_refresh_us = 0;
uint64_t g_total_refresh_us = 0;
uint32_t g_full_refresh_count = 0;
uint16_t g_last_refresh_x = 0;
uint16_t g_last_refresh_y = 0;
uint16_t g_last_refresh_width = 0;
uint16_t g_last_refresh_height = 0;
std::string g_last_error;
lv_obj_t *g_rgb565_test_overlay = nullptr;

uint32_t now_ms() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void set_last_error(const std::string &error) {
  std::lock_guard<std::recursive_mutex> lock(g_metrics_mutex);
  g_last_error = error;
  if (!error.empty()) {
    ESP_LOGW(kTag, "%s", error.c_str());
  }
}

void complete_flush_locked(const LvglFlushRequest &request, bool ok,
                           const std::string &error, uint32_t elapsed_us) {
  std::lock_guard<std::recursive_mutex> lock(g_metrics_mutex);
  g_last_flush_us = elapsed_us;
  g_max_flush_us = std::max(g_max_flush_us, elapsed_us);
  g_total_flush_us += elapsed_us;
  if (!g_refresh.active) {
    g_refresh = {true, false, request.started_us, 0, request.x, request.y,
                 request.x, request.y};
  }
  g_refresh.x1 = std::min(g_refresh.x1, request.x);
  g_refresh.y1 = std::min(g_refresh.y1, request.y);
  g_refresh.x2 = std::max<uint16_t>(g_refresh.x2, request.x + request.width);
  g_refresh.y2 = std::max<uint16_t>(g_refresh.y2, request.y + request.height);
  if (!ok) {
    ++g_flush_fail_count;
    g_refresh.failed = true;
    set_last_error(error.empty() ? "lvgl_flush_failed" : error);
  } else {
    ++g_flush_count;
    const uint32_t pixels = static_cast<uint32_t>(request.width) * request.height;
    g_flushed_pixels += pixels;
    g_refresh.pixels += pixels;
  }
  if (!request.last_in_refresh) return;
  if (!g_refresh.failed) {
    ++g_refresh_count;
    g_last_refresh_pixels = g_refresh.pixels;
    g_last_refresh_us = static_cast<uint32_t>(
        request.started_us + elapsed_us - g_refresh.started_us);
    g_max_refresh_us = std::max(g_max_refresh_us, g_last_refresh_us);
    g_total_refresh_us += g_last_refresh_us;
    if (g_refresh.pixels >= static_cast<uint32_t>(g_width) * g_height &&
        g_refresh.x1 == 0 && g_refresh.y1 == 0 &&
        g_refresh.x2 == g_width && g_refresh.y2 == g_height) {
      ++g_full_refresh_count;
    }
    g_last_refresh_x = g_refresh.x1;
    g_last_refresh_y = g_refresh.y1;
    g_last_refresh_width = g_refresh.x2 - g_refresh.x1;
    g_last_refresh_height = g_refresh.y2 - g_refresh.y1;
    set_last_error("");
  }
  g_refresh = {};
}

void flush_worker_task(void *) {
  LvglFlushRequest request;
  for (;;) {
    if (xQueueReceive(g_flush_queue, &request, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    std::string error;
    const bool ok = g_config.surface.draw_bitmap(
        g_config.surface.context, request.x, request.y, request.width,
        request.height, request.pixels, g_source_pixel_format, error);
    const uint32_t elapsed_us = static_cast<uint32_t>(
        esp_timer_get_time() - request.started_us);
    complete_flush_locked(request, ok, error, elapsed_us);
    if (g_config.notify_flush) {
      g_config.notify_flush(request.x, request.y, request.width, request.height);
    }
    if (request.display != nullptr) {
      // LVGL defines flush_ready as the asynchronous driver's completion
      // signal (it only clears the display's flushing flag). It must not wait
      // for g_mutex: a single partial render can need several sequential
      // 16-row flushes while lv_timer_handler() still owns that mutex.
      lv_display_flush_ready(request.display);
    }
    // Publish completion only after LVGL has cleared its own flushing flag.
    // The opposite order allowed the product task to enter lv_timer_handler()
    // in the tiny window between these operations and wait on stale state.
    g_flush_pending.store(false, std::memory_order_release);
    if (g_flush_complete != nullptr) {
      xSemaphoreGive(g_flush_complete);
    }
  }
}

bool start_flush_worker() {
  g_flush_queue = xQueueCreate(1, sizeof(LvglFlushRequest));
  g_flush_complete = xSemaphoreCreateBinary();
  if (g_flush_queue == nullptr || g_flush_complete == nullptr) {
    if (g_flush_queue != nullptr) {
      vQueueDelete(g_flush_queue);
      g_flush_queue = nullptr;
    }
    if (g_flush_complete != nullptr) {
      vSemaphoreDelete(g_flush_complete);
      g_flush_complete = nullptr;
    }
    set_last_error("lvgl_flush_sync_alloc_failed");
    return false;
  }

  std::string error;
  const lunaredu::lunaros::ManagedTaskDefinition definition{
      .id = "lvgl_flush",
      .owner_service_id = "desktop.display",
      .role = "lcd_transfer",
      .stack_bytes = kFlushTaskStackBytes,
      .priority = kFlushTaskPriority,
      .core_affinity = -1,
      .lifetime = lunaredu::lunaros::ManagedTaskLifetime::Resident,
      .memory = lunaredu::lunaros::ManagedTaskMemory::Internal,
  };
  if (!lunaredu::lunaros::create_managed_task(
          definition, flush_worker_task, nullptr, &g_flush_task, error)) {
    vQueueDelete(g_flush_queue);
    vSemaphoreDelete(g_flush_complete);
    g_flush_queue = nullptr;
    g_flush_complete = nullptr;
    set_last_error(error.empty() ? "lvgl_flush_task_create_failed"
                                 : "lvgl_flush_task_create_failed:" + error);
    return false;
  }
  return true;
}

void restart_if_flush_stalled() {
  {
    std::lock_guard<std::recursive_mutex> lock(g_metrics_mutex);
    if (!g_flush_pending.load(std::memory_order_acquire) ||
        esp_timer_get_time() - g_pending_started_us <
            static_cast<uint64_t>(kFlushWaitTimeoutMs) * 1000ULL) return;
    g_last_error = "lvgl_flush_wait_timeout";
  }
  // A stuck callback can still own the draw buffer/DMA. Do not return a fake
  // completion, delete the worker or free its memory. Reset the system so the
  // peripheral and its owner are stopped together. This also covers the last
  // tile, for which LVGL may not call flush_wait until a later refresh.
  ESP_LOGE(kTag, "LCD transfer stalled; restarting to recover display ownership");
  esp_restart();
}

bool wait_for_runtime_flush() {
  if (!g_flush_pending.load(std::memory_order_acquire)) return true;
  if (g_flush_complete != nullptr) {
    (void)xSemaphoreTake(g_flush_complete, pdMS_TO_TICKS(kFlushWaitTimeoutMs));
  }
  restart_if_flush_stalled();
  return !g_flush_pending.load(std::memory_order_acquire);
}

void wait_for_lvgl_display_flush(lv_display_t *display) {
  (void)display;
  while (g_flush_pending.load(std::memory_order_acquire)) {
    if (g_flush_complete != nullptr) {
      (void)xSemaphoreTake(g_flush_complete, pdMS_TO_TICKS(kFlushWaitPollMs));
    } else {
      vTaskDelay(pdMS_TO_TICKS(kFlushWaitPollMs));
    }
    restart_if_flush_stalled();
  }
}

void flush_lvgl_display(lv_display_t *display, const lv_area_t *area,
                        uint8_t *pixel_map) {
  if (display == nullptr || area == nullptr || pixel_map == nullptr) {
    if (display != nullptr) {
      lv_display_flush_ready(display);
    }
    return;
  }

  const int32_t x1 = std::max<int32_t>(0, area->x1);
  const int32_t y1 = std::max<int32_t>(0, area->y1);
  const int32_t x2 =
      std::min<int32_t>(static_cast<int32_t>(g_width) - 1, area->x2);
  const int32_t y2 =
      std::min<int32_t>(static_cast<int32_t>(g_height) - 1, area->y2);
  if (x2 < x1 || y2 < y1) {
    lv_display_flush_ready(display);
    return;
  }

  const LvglFlushRequest request{
      .display = display,
      .x = static_cast<uint16_t>(x1),
      .y = static_cast<uint16_t>(y1),
      .width = static_cast<uint16_t>(x2 - x1 + 1),
      .height = static_cast<uint16_t>(y2 - y1 + 1),
      .pixels = pixel_map,
      .started_us = static_cast<uint64_t>(esp_timer_get_time()),
      .last_in_refresh = lv_display_flush_is_last(display),
  };
  if (g_flush_complete != nullptr) {
    (void)xSemaphoreTake(g_flush_complete, 0);
  }
  {
    std::lock_guard<std::recursive_mutex> lock(g_metrics_mutex);
    g_pending_started_us = request.started_us;
    g_flush_pending.store(true, std::memory_order_release);
  }
  if (g_flush_queue == nullptr ||
      xQueueSend(g_flush_queue, &request, 0) != pdTRUE) {
    g_flush_pending.store(false, std::memory_order_release);
    complete_flush_locked(request, false, "lvgl_flush_queue_full", 0);
    lv_display_flush_ready(display);
  }
}

void read_lvgl_touch(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  if (data == nullptr) {
    return;
  }

  uint16_t x = 0;
  uint16_t y = 0;
  bool pressed = false;
  if (g_config.read_touch_event != nullptr) {
    LvglScreenTouchEvent event;
    bool has_more = false;
    if (g_config.read_touch_event(&event, &has_more,
                                  g_config.touch_context)) {
      data->state = event.pressed ? LV_INDEV_STATE_PRESSED
                                  : LV_INDEV_STATE_RELEASED;
      data->point.x = static_cast<int32_t>(event.x);
      data->point.y = static_cast<int32_t>(event.y);
      data->timestamp = event.timestamp_ms;
      data->continue_reading = has_more;
      return;
    }
  }
  const bool available =
      g_config.read_touch != nullptr &&
      g_config.read_touch(&x, &y, &pressed, g_config.touch_context);
  data->state = (available && pressed) ? LV_INDEV_STATE_PRESSED
                                       : LV_INDEV_STATE_RELEASED;
  if (available && pressed) {
    data->point.x = static_cast<int32_t>(x);
    data->point.y = static_cast<int32_t>(y);
  }
}

void cleanup_unstarted_runtime() {
  if (g_touch_indev != nullptr) lv_indev_delete(g_touch_indev);
  g_touch_indev = nullptr;
  if (g_display != nullptr) lv_display_delete(g_display);
  g_display = nullptr;
  heap_caps_free(g_draw_buffer);
  g_draw_buffer = nullptr;
  g_draw_buffer_pixels = 0;
  g_draw_buffer_bytes = 0;
}

} // namespace

bool setup_lvgl_screen_runtime(const LvglScreenRuntimeConfig &config) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (g_initialized) {
    return true;
  }

  if (config.surface.draw_bitmap == nullptr || config.surface.width == 0 ||
      config.surface.height == 0 ||
      (config.surface.pixel_format != LvglScreenPixelFormat::Rgb565 &&
       config.surface.pixel_format != LvglScreenPixelFormat::Bgr666U8x3)) {
    set_last_error("display_panel_unavailable");
    return false;
  }

  g_config = config;
  g_width = config.surface.width;
  g_height = config.surface.height;
  const uint16_t rows = g_config.draw_buffer_rows == 0
                            ? static_cast<uint16_t>(16)
                            : g_config.draw_buffer_rows;
  g_draw_buffer_pixels = static_cast<size_t>(g_width) * rows;
  g_source_pixel_format = config.surface.pixel_format;
  const size_t bytes_per_pixel =
      g_source_pixel_format == LvglScreenPixelFormat::Bgr666U8x3 ? 3U
                                                                      : 2U;
  g_draw_buffer_bytes = g_draw_buffer_pixels * bytes_per_pixel;
  g_draw_buffer = static_cast<uint8_t *>(heap_caps_malloc(
      g_draw_buffer_bytes,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  const char *buffer_memory = "spiram";
  if (g_draw_buffer == nullptr) {
    g_draw_buffer = static_cast<uint8_t *>(heap_caps_malloc(
        g_draw_buffer_bytes,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    buffer_memory = "internal";
  }
  if (g_draw_buffer == nullptr) {
    g_draw_buffer_pixels = 0;
    g_draw_buffer_bytes = 0;
    set_last_error("lvgl_draw_buffer_alloc_failed");
    return false;
  }

  lv_init();
  // Use the monotonic ESP-IDF clock directly. Slow LCD transfers must not
  // stretch LVGL long-press, gesture and animation time.
  lv_tick_set_cb(now_ms);
  g_display = lv_display_create(g_width, g_height);
  if (g_display == nullptr) {
    cleanup_unstarted_runtime();
    set_last_error("lvgl_display_create_failed");
    return false;
  }

  lv_display_set_color_format(
      g_display,
      g_source_pixel_format == LvglScreenPixelFormat::Bgr666U8x3
          ? LV_COLOR_FORMAT_RGB888
          : LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(g_display, flush_lvgl_display);
  // Partial rendering with a single asynchronous draw buffer can require
  // several sequential flushes in one LVGL refresh. Without a wait callback
  // LVGL uses a tight `while(disp->flushing)` spin, starving CPU0 and the
  // XiaoZhi/network tasks until the task watchdog fires. Block on the worker
  // semaphore instead so the scheduler can run audio and DMA completion.
  lv_display_set_flush_wait_cb(g_display, wait_for_lvgl_display_flush);
  lv_display_set_buffers(g_display, g_draw_buffer, nullptr,
                         g_draw_buffer_bytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  if (lv_timer_t *refresh_timer = lv_display_get_refr_timer(g_display);
      refresh_timer != nullptr) {
    lv_timer_set_period(refresh_timer, kUiTimerPeriodMs);
  }

  if (g_config.read_touch != nullptr ||
      g_config.read_touch_event != nullptr) {
    g_touch_indev = lv_indev_create();
    if (g_touch_indev == nullptr) {
      set_last_error("lvgl_touch_create_failed");
      cleanup_unstarted_runtime();
      return false;
    }
    lv_indev_set_type(g_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_touch_indev, read_lvgl_touch);
    lv_indev_set_display(g_touch_indev, g_display);
    if (lv_timer_t *read_timer = lv_indev_get_read_timer(g_touch_indev);
        read_timer != nullptr) {
      lv_timer_set_period(read_timer, kTouchReadPeriodMs);
    }
  }

  if (!start_flush_worker()) {
    cleanup_unstarted_runtime();
    return false;
  }
  g_initialized = true;
  {
    std::lock_guard<std::recursive_mutex> lock(g_metrics_mutex);
    g_setup_status.initialized = true;
    g_setup_status.width = g_width;
    g_setup_status.height = g_height;
    g_setup_status.touch_input = g_touch_indev != nullptr;
    g_last_error.clear();
  }
  ESP_LOGI(kTag,
           "LVGL runtime ready size=%ux%u buffer_rows=%u buffer_memory=%s "
           "pixel_format=%s touch=%s",
           static_cast<unsigned>(g_width), static_cast<unsigned>(g_height),
           static_cast<unsigned>(rows),
           buffer_memory,
           g_source_pixel_format == LvglScreenPixelFormat::Bgr666U8x3
               ? "rgb666-u8x3"
               : "rgb565",
           g_touch_indev == nullptr ? "disabled" : "enabled");
  return true;
}

bool lvgl_screen_runtime_ready() {
  std::lock_guard<std::recursive_mutex> lock(g_metrics_mutex);
  return g_setup_status.initialized;
}

LvglScreenRuntimeStatus lvgl_screen_runtime_status() {
  std::lock_guard<std::recursive_mutex> metrics_lock(g_metrics_mutex);
  LvglScreenRuntimeStatus status;
  status.initialized = g_setup_status.initialized;
  status.width = g_setup_status.width;
  status.height = g_setup_status.height;
  status.touch_input = g_setup_status.touch_input;
  status.refresh_count = g_refresh_count;
  status.flush_count = g_flush_count;
  status.flush_fail_count = g_flush_fail_count;
  status.last_flush_us = g_last_flush_us;
  status.max_flush_us = g_max_flush_us;
  status.total_flush_us = g_total_flush_us;
  status.flushed_pixels = g_flushed_pixels;
  status.last_refresh_pixels = g_last_refresh_pixels;
  status.last_refresh_us = g_last_refresh_us;
  status.max_refresh_us = g_max_refresh_us;
  status.total_refresh_us = g_total_refresh_us;
  status.full_refresh_count = g_full_refresh_count;
  status.last_refresh_x = g_last_refresh_x;
  status.last_refresh_y = g_last_refresh_y;
  status.last_refresh_width = g_last_refresh_width;
  status.last_refresh_height = g_last_refresh_height;
  status.last_error = g_last_error;
  status.flush_pending = g_flush_pending.load(std::memory_order_acquire);
  return status;
}

void lvgl_screen_runtime_handler() {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (g_initialized) {
    lv_timer_t *refresh_timer =
        g_display == nullptr ? nullptr : lv_display_get_refr_timer(g_display);
    const bool transfer_in_progress =
        g_flush_pending.load(std::memory_order_acquire);
    if (transfer_in_progress) {
      restart_if_flush_stalled();
      // The display uses one LVGL draw buffer. Running input timers while an
      // asynchronous flush owns that buffer can invalidate a widget and
      // resume the refresh timer inside lv_timer_handler(). LVGL then waits
      // indefinitely for the outstanding flush while the product task still
      // owns its hardware mutex, freezing touch and Local Device API calls.
      // A normal SPI flush completes well inside one product tick, so defer
      // all LVGL timers until the worker has called flush_ready(). The touch
      // driver continues sampling independently and retains queued events.
      if (refresh_timer != nullptr) {
        lv_timer_pause(refresh_timer);
      }
      return;
    }
    if (refresh_timer != nullptr) {
      if (g_refresh_enabled) {
        // The worker deliberately does not mutate LVGL timers. Resume here on
        // the LVGL owner task after the asynchronous transfer has completed.
        lv_timer_resume(refresh_timer);
      } else {
        // A single LVGL draw buffer cannot be rendered again until
        // flush_ready. Keep only the display timer paused; input timers can
        // continue and invalidations remain queued.
        lv_timer_pause(refresh_timer);
      }
    }
    lv_timer_handler();
    if ((!g_refresh_enabled || g_flush_pending.load(std::memory_order_acquire)) &&
        refresh_timer != nullptr) {
      // Input callbacks may invalidate widgets and resume the refresh timer.
      // Do not let that make the next handler spin in wait_for_flushing().
      lv_timer_pause(refresh_timer);
    }
  }
}

void lvgl_screen_runtime_set_refresh_enabled(bool enabled) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  if (!g_initialized || g_display == nullptr || enabled == g_refresh_enabled) {
    return;
  }
  lv_timer_t *refresh_timer = lv_display_get_refr_timer(g_display);
  if (refresh_timer == nullptr) {
    return;
  }
  if (enabled) {
    lv_timer_resume(refresh_timer);
    lv_timer_ready(refresh_timer);
  } else {
    lv_timer_pause(refresh_timer);
  }
  g_refresh_enabled = enabled;
}

bool lvgl_screen_runtime_test_rgb565(std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  if (!wait_for_runtime_flush()) {
    if (error != nullptr) {
      *error = "rgb565_test_previous_flush_timeout";
    }
    return false;
  }
  std::unique_lock<std::recursive_mutex> lock(g_mutex);
  if (!g_initialized || g_display == nullptr) {
    if (error != nullptr) {
      *error = "lvgl_runtime_unavailable";
    }
    return false;
  }

  if (g_rgb565_test_overlay != nullptr) {
    lv_obj_delete(g_rgb565_test_overlay);
    g_rgb565_test_overlay = nullptr;
  }

  lv_obj_t *overlay = lv_obj_create(lv_screen_active());
  if (overlay == nullptr) {
    if (error != nullptr) {
      *error = "rgb565_test_overlay_alloc_failed";
    }
    return false;
  }
  lv_obj_remove_style_all(overlay);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_size(overlay, g_width, g_height);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

  constexpr uint32_t kBarColors[] = {
      0xF80000, 0xFD8400, 0xFFFF00, 0x00FF00,
      0x00FFFF, 0x0000FF, 0xFF00FF, 0xFFFFFF,
  };
  constexpr size_t kBarCount = sizeof(kBarColors) / sizeof(kBarColors[0]);
  const int bar_height = std::max<int>(1, g_height - 24);
  const int bar_width = std::max<int>(1, g_width / kBarCount);
  for (size_t index = 0; index < kBarCount; ++index) {
    lv_obj_t *bar = lv_obj_create(overlay);
    if (bar == nullptr) {
      lv_obj_delete(overlay);
      if (error != nullptr) {
        *error = "rgb565_test_bar_alloc_failed";
      }
      return false;
    }
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, static_cast<int32_t>(index * bar_width), 0);
    lv_obj_set_size(bar,
                    static_cast<int32_t>(index + 1 == kBarCount
                                             ? g_width - index * bar_width
                                             : bar_width),
                    bar_height);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kBarColors[index]), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  }

  constexpr int kCheckerCell = 8;
  const int checker_y = bar_height;
  for (int y = checker_y; y < g_height; y += kCheckerCell) {
    for (int x = 0; x < g_width; x += kCheckerCell) {
      lv_obj_t *cell = lv_obj_create(overlay);
      if (cell == nullptr) {
        lv_obj_delete(overlay);
        if (error != nullptr) {
          *error = "rgb565_test_checker_alloc_failed";
        }
        return false;
      }
      lv_obj_remove_style_all(cell);
      lv_obj_set_pos(cell, x, y);
      lv_obj_set_size(cell, std::min(kCheckerCell, g_width - x),
                      std::min(kCheckerCell, g_height - y));
      lv_obj_set_style_bg_color(cell,
                                lv_color_hex(((x / kCheckerCell +
                                               y / kCheckerCell) & 1) == 0
                                                 ? 0x000000
                                                 : 0xFFFFFF),
                                0);
      lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    }
  }

  lv_obj_move_foreground(overlay);
  g_rgb565_test_overlay = overlay;
  lv_refr_now(g_display);
  lock.unlock();
  if (!wait_for_runtime_flush()) {
    if (error != nullptr) {
      *error = "rgb565_test_flush_timeout";
    }
    return false;
  }
  return true;
}

void lvgl_screen_runtime_clear_rgb565_test() {
  (void)wait_for_runtime_flush();
  std::unique_lock<std::recursive_mutex> lock(g_mutex);
  if (g_rgb565_test_overlay == nullptr) {
    return;
  }
  lv_obj_delete(g_rgb565_test_overlay);
  g_rgb565_test_overlay = nullptr;
  if (g_initialized && g_display != nullptr) {
    lv_refr_now(g_display);
    lock.unlock();
    (void)wait_for_runtime_flush();
  }
}

} // namespace lunaredu::desktop
