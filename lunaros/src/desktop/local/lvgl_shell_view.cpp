#include "desktop/local/local_shell_internal.h"
#include "lunaredu_lunaros/desktop/local/lvgl_shell_view.h"
#include "lunaredu_lunaros/desktop/local/lunaros_ui_bridge.h"

#include <atomic>

#include <esp_log.h>

#include "lunaredu_lunaros/desktop/core/page_state.h"
#include "lunaredu_lunaros/desktop/local/font_runtime.h"
#include "lunaredu_lunaros/desktop/local/boot_view.h"
#include "lunaredu_lunaros/desktop/local/local_shell.h"

namespace lunaredu::desktop {
namespace {

constexpr const char *kTag = "lunar_os_lvgl";

bool g_ready = false;
LocalDisplayConfig g_display;
std::atomic<uint8_t> g_boot_ready_mask{0};
std::atomic<uint8_t> g_boot_degraded_mask{0};
std::atomic<bool> g_boot_finish_requested{false};

constexpr uint8_t boot_stage_mask(uint8_t stage) {
  return static_cast<uint8_t>(1U << static_cast<uint8_t>(stage));
}

} // namespace

bool read_local_touch_raw(uint16_t *x, uint16_t *y) {
  return x && y && g_display.read_touch_raw && g_display.read_touch_raw(x, y);
}

bool setup_lvgl_shell_view(const LocalDisplayConfig &config) {
  if (g_ready) {
    return true;
  }

  const auto &runtime = config;
  if (runtime.width == 0 || runtime.height == 0) {
    ESP_LOGW(kTag, "LVGL runtime is unavailable");
    return false;
  }

  g_display = config;
  set_presentation_viewport(config.width, config.height);
  const bool font_ready = lunaros_font_runtime_setup();
  if (!font_ready) {
    ESP_LOGE(kTag,
             "shared font unavailable; continuing with the rescue font");
  }
  g_boot_ready_mask.store(0, std::memory_order_release);
  g_boot_degraded_mask.store(0, std::memory_order_release);
  g_boot_finish_requested.store(false, std::memory_order_release);
  if (!initialize_local_shell()) return false;
  if (!font_ready) {
    lv_obj_t *warning = lv_label_create(lv_layer_top());
    lv_label_set_text_fmt(
        warning, "FONT ERROR: %s",
        lunaros_font_runtime_failure_reason());
    lv_obj_set_style_text_font(warning, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(warning, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(warning, lv_color_hex(0xB91C1C), 0);
    lv_obj_set_style_bg_opa(warning, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(warning, 6, 0);
    lv_obj_set_style_radius(warning, 6, 0);
    lv_obj_align(warning, LV_ALIGN_TOP_MID, 0, 4);
  }
  g_ready = true;
  ESP_LOGI(kTag, "shared LunarOS UI ready size=%ux%u",
           static_cast<unsigned>(runtime.width),
           static_cast<unsigned>(runtime.height));
  return true;
}

bool lvgl_shell_view_ready() { return g_ready; }

void report_lvgl_shell_boot_stage(uint8_t stage, bool ready) {
  if (stage >= 8) return;
  const uint8_t mask = boot_stage_mask(stage);
  if (ready) {
    g_boot_ready_mask.fetch_or(mask, std::memory_order_release);
    g_boot_degraded_mask.fetch_and(static_cast<uint8_t>(~mask),
                                   std::memory_order_release);
  } else {
    g_boot_degraded_mask.fetch_or(mask, std::memory_order_release);
    g_boot_ready_mask.fetch_and(static_cast<uint8_t>(~mask),
                                std::memory_order_release);
  }
}

void request_lvgl_shell_boot_finish() {
  g_boot_finish_requested.store(true, std::memory_order_release);
}

void tick_lvgl_shell_view() {
  if (!g_ready) {
    return;
  }
  lunaredu::desktop::sync_lunaros_ui_page_request();
  lunaros_set_boot_status(
      g_boot_ready_mask.load(std::memory_order_acquire),
      g_boot_degraded_mask.load(std::memory_order_acquire),
      g_boot_finish_requested.load(std::memory_order_acquire) ? 1 : 0);
}

bool lvgl_shell_boot_animation_complete() {
  return !g_ready || lunaros_ui_boot_animation_complete() != 0;
}

} // namespace lunaredu::desktop
