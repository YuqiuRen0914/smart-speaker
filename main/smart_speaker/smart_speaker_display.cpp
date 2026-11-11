#include "smart_speaker_display.h"
#include "lunaredu_pet_conversation.h"
#include "lunaredu_account_resources.h"
#include "lunaredu_lunaros/kernel/managed_task.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include <esp_heap_caps.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include "misc/cache/instance/lv_image_cache.h"

#include "lunaredu_lunaros/desktop/local/lvgl_screen_runtime.h"
#include "lunaredu_lunaros/desktop/local/font_runtime.h"
#include "lunaredu_device_spi_lcd_desktop_surface.h"
#include "lunaredu_device_spi_lcd_surface.h"
#include "lunaredu_lunaros/network/network.h"

#include "smart_speaker_audio.h"
#include "smart_speaker_character.h"
#include "smart_speaker_frame_delta.h"
#include "smart_speaker_pause_indicator.h"
#include "smart_speaker_runtime.h"
#include "smart_speaker_track_caption.h"
#include "smart_speaker_avatar.h"
#include "smart_speaker_ui_status.h"
#include "smart_speaker_sensors.h"
#include "smart_speaker_touch_input.h"
#include "smart_speaker_pet_animation.h"
#include "smart_speaker_xiaozhi.h"
#include "smart_speaker_amoled_list.h"
#include "smart_speaker_home.h"
#include "smart_speaker_home_pet.h"
#include "smart_speaker_launcher.h"
#include "assets/home_clock_font.h"
#include "smart_speaker_boot.h"
#include "product/product_state_runtime.h"
#include "product/desktop_resources.h"
#include "product/weather.h"
#include "lunaredu_lunaros/persistence/identity.h"
#include "lunaredu_device_sdcard_tfcard_store.h"
#include "lunaredu_lunaros/device_link/device_link.h"

namespace smart_speaker {
namespace {

constexpr const char *kTag = "speaker_display";
const lv_font_t *g_body_font = nullptr;
const lv_font_t *g_heading_font = nullptr;
constexpr uint32_t kDisplayFrameMs = 20;
HomePetView g_home_pet;
AppLauncher g_launcher;
extern const uint8_t kHomePetIdle[] asm("_binary_home_pet_idle_bin_start");
SpeakerHome g_home;
SpeakerBootScreen g_boot;
lv_obj_t *g_wallpaper_view = nullptr;
lv_image_dsc_t g_wallpaper_image{};
std::shared_ptr<const product::WallpaperPixels> g_wallpaper_pixels;
lv_obj_t *g_play_status = nullptr;
lv_obj_t *g_track_panel = nullptr;
TrackCaption g_track_caption;
lv_obj_t *g_volume = nullptr;
float g_indicator_level = 0, g_indicator_opacity = 0;
VolumeIndicator g_volume_indicator;
lv_obj_t *g_setup_panel = nullptr;
lv_obj_t *g_setup_title = nullptr;
lv_obj_t *g_setup_ssid = nullptr;
lv_obj_t *g_setup_password = nullptr;
lv_obj_t *g_setup_step_two = nullptr;
bool g_setup_visible = false;
bool g_setup_character = false;
bool g_setup_cached_credentials = false;
bool g_setup_cached_failure = false;
bool g_network_was_connecting = false;
bool g_network_success_seen = false;
uint32_t g_network_success_ms = 0;
character::Feedback g_network_feedback = character::Feedback::None;
lv_obj_t *g_character_hint = nullptr;
bool g_force_character_redraw = false;
std::atomic<bool> g_character_damaged{false};
std::atomic<uint8_t> g_assistant_emotion{
    static_cast<uint8_t>(AssistantVisualState::Inactive)};
std::atomic<uint32_t> g_assistant_emotion_until_ms{0};
std::atomic<int> g_character_damage_top{50};
std::atomic<int> g_character_damage_height{character::kSize};
void notify_character_flush(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
  const int top = g_character_damage_top.load(std::memory_order_relaxed);
  if (x < 200 && x + width > 40 && y < top + g_character_damage_height.load(std::memory_order_relaxed) && y + height > top)
    g_character_damaged.store(true, std::memory_order_release);
}
std::string g_setup_cached_ssid;
std::string g_setup_cached_password;
uint16_t *g_character_tile = nullptr;
uint16_t *g_character_previous = nullptr;
uint16_t *g_character_transfer = nullptr;
portMUX_TYPE g_display_stats_lock = portMUX_INITIALIZER_UNLOCKED;
SmartSpeakerDisplayStats g_display_stats{};
character::Controller g_character;
character::Renderer g_character_renderer;
constexpr int kCharacterTop = 50;
// Opening the application menu can nest LVGL layout and font drawing on
// top of the display loop. Keep that working stack in internal RAM.
constexpr size_t kDisplayStackBytes = 12 * 1024;
std::atomic<int> g_pending_pet_action{-1};
bool g_character_layout = false;

void style_plain_object(lv_obj_t *object) {
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_pad_all(object, 0, 0);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void draw_playback_indicator(lv_event_t *event) {
  auto *object = lv_event_get_target_obj(event);
  auto *layer = lv_event_get_layer(event);
  lv_area_t bounds;
  lv_obj_get_coords(object, &bounds);
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.width = 4;
  line.round_start = line.round_end = 1;
  line.opa = static_cast<lv_opa_t>(g_indicator_opacity * 255);
  line.p1 = {bounds.x1 + 4, bounds.y1 + 4};
  line.p2 = {bounds.x1 + 112, bounds.y1 + 4};
  line.color = lv_color_hex(0x242424);
  lv_draw_line(layer, &line);
  if (g_indicator_level > 0) {
    line.p2.x = bounds.x1 + 4 + static_cast<int32_t>(g_indicator_level * 108);
    line.color = lv_color_hex(0xF4C95D);
    lv_draw_line(layer, &line);
  }
}

void make_ui(uint8_t *pet_buffer) {
  lv_obj_t *screen = lv_screen_active();
  style_plain_object(screen);
  lv_obj_set_style_text_font(screen, g_body_font, 0);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(0xE8E6DF), 0);

  g_wallpaper_view = lv_image_create(screen);
  lv_obj_set_pos(g_wallpaper_view, 0, 0);
  lv_obj_add_flag(g_wallpaper_view, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(g_wallpaper_view);
  g_home.create(screen, &speaker_home_digits, &speaker_home_seconds, g_body_font,
      lunaros_font_runtime_12(), g_heading_font);

  g_home_pet.create(screen, pet_buffer);

  g_track_panel = lv_obj_create(screen);
  style_plain_object(g_track_panel);
  lv_obj_set_style_radius(g_track_panel, 0, 0);
  lv_obj_set_style_bg_color(g_track_panel, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_track_panel, LV_OPA_COVER, 0);
  lv_obj_set_pos(g_track_panel, 86, 18);
  lv_obj_set_size(g_track_panel, 142, 24);
  lv_obj_add_flag(g_track_panel, LV_OBJ_FLAG_HIDDEN);
  g_play_status = lv_label_create(g_track_panel);
  lv_label_set_text(g_play_status, "待播放");
  lv_obj_set_pos(g_play_status, 0, 0);
  lv_obj_set_width(g_play_status, 142);
  lv_label_set_long_mode(g_play_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_anim_duration(g_play_status, lv_anim_speed_clamped(24, 1000, 18000), 0);
  lv_obj_set_style_text_align(g_play_status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(g_play_status, lv_color_hex(0x8A8984), 0);
  g_volume = lv_obj_create(screen);
  lv_obj_remove_style_all(g_volume);
  style_plain_object(g_volume);
  lv_obj_remove_flag(g_volume, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(g_volume, 62, 220);
  lv_obj_set_size(g_volume, 116, 8);
  lv_obj_add_event_cb(g_volume, draw_playback_indicator, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_flag(g_volume, LV_OBJ_FLAG_HIDDEN);

  g_character_hint = lv_label_create(screen);
  lv_obj_set_size(g_character_hint, 216, 24);
  lv_label_set_long_mode(g_character_hint, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(g_character_hint, 12, 16);
  lv_obj_set_style_text_align(g_character_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(g_character_hint, lv_color_hex(0x8A8984), 0);
  lv_label_set_text(g_character_hint, "正在连接…");
  lv_obj_add_flag(g_character_hint, LV_OBJ_FLAG_HIDDEN);

  // Last child covers the player while the local setup credentials are visible.
  g_setup_panel = lv_obj_create(screen);
  style_plain_object(g_setup_panel);
  lv_obj_set_size(g_setup_panel, 240, 240);
  lv_obj_set_pos(g_setup_panel, 0, 0);
  lv_obj_set_style_radius(g_setup_panel, 0, 0);
  lv_obj_set_style_bg_color(g_setup_panel, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_setup_panel, LV_OPA_COVER, 0);
  auto label = [](int y, const char *text, uint32_t color) {
    lv_obj_t *object = lv_label_create(g_setup_panel);
    lv_obj_set_width(object, 216);
    lv_obj_set_pos(object, 12, y);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_label_set_text(object, text);
    return object;
  };
  g_setup_title = label(10, "无线网络", 0xF4C95D);
  lv_obj_set_style_text_font(g_setup_title, g_heading_font, 0);
  g_setup_ssid = label(60, "", 0xE8E6DF);
  lv_label_set_long_mode(g_setup_ssid, LV_LABEL_LONG_SCROLL_CIRCULAR);
  g_setup_password = label(111, "", 0xF4C95D);
#if LV_FONT_MONTSERRAT_36
  lv_obj_set_style_text_font(g_setup_password, &lv_font_montserrat_36, 0);
#endif
  g_setup_step_two = label(165, "手机连接后配网", 0x8A8984);
  lv_obj_add_flag(g_setup_panel, LV_OBJ_FLAG_HIDDEN);
}

void update_setup_screen() {
  using namespace lunaredu::lunaros;
  const auto network = network_snapshot();
  const bool applying = network.provisioning == NetworkProvisioningState::Applying;
  const bool active = network.provisioning != NetworkProvisioningState::Idle;
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  const bool connecting = network.link == NetworkLinkState::Connecting || applying;
  if (g_network_was_connecting && network.link == NetworkLinkState::Up && !applying) {
    g_network_success_seen = true;
    g_network_success_ms = now;
  }
  g_network_was_connecting = connecting;
  g_network_feedback = connecting ? character::Feedback::Radar :
      g_network_success_seen && now - g_network_success_ms < 1800 ?
          character::Feedback::Success : character::Feedback::None;
  const auto menu = local_menu_snapshot();
  // A dismissed setup page must not latch itself open while Wi-Fi reconnects.
  const bool visible = !menu.provisioning_dismissed && (active || menu.provisioning);
  if (!visible) {
    if (g_setup_visible) {
      lv_label_set_text(g_setup_password, "");
      g_setup_cached_password.clear();
      g_setup_cached_ssid.clear();
      lv_obj_add_flag(g_setup_panel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_invalidate(lv_screen_active());
      g_force_character_redraw = true;
    }
    g_setup_visible = false;
    g_setup_character = false;
    return;
  }
  const bool show_credentials = active && !applying &&
      !network.provisioning_ssid.empty();
  const bool failed = !show_credentials && !connecting && !network.last_error.empty();
  g_setup_character = true;
  if (g_setup_character)
    g_network_feedback = failed ? character::Feedback::Error : character::Feedback::Radar;
  const std::string ssid = show_credentials ? network.provisioning_ssid
                                            : network.target_ssid;
  // The secret is read only by the on-device screen; never add it to status JSON.
  const std::string password = !show_credentials ? "" :
      (g_setup_visible && ssid == g_setup_cached_ssid &&
       !g_setup_cached_password.empty()) ? g_setup_cached_password :
                                         network_local_provisioning_password();
  if (!g_setup_visible || ssid != g_setup_cached_ssid ||
      password != g_setup_cached_password || show_credentials != g_setup_cached_credentials ||
      failed != g_setup_cached_failure) {
    lv_label_set_text(g_setup_title, show_credentials ? "等待配网" : failed ? "连接失败" : "正在连接…");
    lv_label_set_text(g_setup_ssid, ssid.c_str());
    lv_obj_set_y(g_setup_ssid, show_credentials ? 40 : 35);
    lv_label_set_text(g_setup_password, show_credentials ? ("密码 " + password).c_str() : "");
    lv_obj_set_y(g_setup_password, 67);
    lv_obj_set_style_text_font(g_setup_password, g_body_font, 0);
    lv_obj_set_y(g_setup_step_two, 202);
    for (lv_obj_t *step : {g_setup_step_two}) {
      if (show_credentials) lv_obj_remove_flag(step, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(step, LV_OBJ_FLAG_HIDDEN);
    }
    for (lv_obj_t *object : {g_setup_password}) {
      if (show_credentials) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
    g_setup_cached_ssid = ssid;
    g_setup_cached_password = password;
    g_setup_cached_credentials = show_credentials;
    g_setup_cached_failure = failed;
    lv_obj_invalidate(g_setup_panel);
    g_force_character_redraw = true;
  }
  lv_obj_remove_flag(g_setup_panel, LV_OBJ_FLAG_HIDDEN);
  g_setup_visible = true;
}

void slide_track_caption(void *object, int32_t x) {
  lv_obj_set_x(static_cast<lv_obj_t *>(object), x);
}

void update_status(const RuntimeStatus &status) {

  const auto caption = g_track_caption.update(
      static_cast<uint32_t>(esp_timer_get_time() / 1000), status.current_name,
      status.current_index, status.playing, status.paused);
  const auto transfer = transfer_snapshot();
  const bool transfer_title = transfer.generation && !transfer_dismissed(transfer.generation) &&
      (transfer.receiving || transfer.batch_open || static_cast<uint32_t>(esp_timer_get_time()/1000) - transfer.updated_ms < 2500);
  if (!transfer_title && caption.text != lv_label_get_text(g_play_status)) {
    // Reset a previous song's scroll offset so each new title starts legibly.
    lv_label_set_long_mode(g_play_status, LV_LABEL_LONG_CLIP);
    lv_label_set_text(g_play_status, caption.text.c_str());
    lv_label_set_long_mode(g_play_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
  }
  if (caption.entered && !transfer_title) {
    lv_anim_delete(g_play_status, slide_track_caption);
    lv_anim_t slide;
    lv_anim_init(&slide);
    lv_anim_set_var(&slide, g_play_status);
    lv_anim_set_values(&slide, 142, 0);
    lv_anim_set_duration(&slide, 450);
    lv_anim_set_exec_cb(&slide, slide_track_caption);
    lv_anim_start(&slide);
  }


}

lv_obj_t *g_menu_panel = nullptr;
lv_obj_t *g_menu_text = nullptr;
lv_obj_t *g_menu_rows[4] = {};
AmoledList g_product_list;
lv_obj_t *g_hold_hint = nullptr;
bool g_local_overlay = false;
lv_obj_t *g_menu_hint = nullptr;
lv_obj_t *g_game2048_panel = nullptr;
lv_obj_t *g_game2048_score = nullptr;
lv_obj_t *g_game2048_tiles[16] = {};
lv_obj_t *g_game2048_values[16] = {};
lv_obj_t *g_binding_code = nullptr;
lv_obj_t *g_binding_info = nullptr;
lv_obj_t *g_avatar_frame = nullptr;
lv_obj_t *g_avatar_view = nullptr;
lv_obj_t *g_avatar_placeholder = nullptr;
lv_obj_t *g_info_labels[3] = {};
lv_obj_t *g_info_values[3] = {};
lv_obj_t *g_calendar_panel = nullptr;
lv_obj_t *g_calendar_days[42] = {};
lv_obj_t *g_cover_view = nullptr;
lv_image_dsc_t g_cover_descriptor{};
std::shared_ptr<const cover::Image> g_display_cover;
void update_cover_view(bool visible) {
  const auto current = visible ? current_cover_snapshot().image : nullptr;
  if (current != g_display_cover) {
    lv_image_set_src(g_cover_view, nullptr);
    lv_image_cache_drop(&g_cover_descriptor);
    g_display_cover = current;
    if (current) {
      g_cover_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
      g_cover_descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
      g_cover_descriptor.header.w = g_cover_descriptor.header.h = cover::kSide;
      g_cover_descriptor.header.stride = cover::kSide * 2;
      g_cover_descriptor.data_size = cover::kSide * cover::kSide * 2;
      g_cover_descriptor.data = reinterpret_cast<const uint8_t *>(current->pixels.get());
      lv_image_set_src(g_cover_view, &g_cover_descriptor);
    }
  }
  if (current) lv_obj_remove_flag(g_cover_view, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(g_cover_view, LV_OBJ_FLAG_HIDDEN);
}
void set_label_text(lv_obj_t *label, const std::string &text) {
  if (text != lv_label_get_text(label)) lv_label_set_text(label, text.c_str());
}
uint32_t game2048_tile_color(uint16_t value) {
  switch (value) {
    case 0: return 0x262626;
    case 2: return 0xEEE4DA;
    case 4: return 0xEDE0C8;
    case 8: return 0xF2B179;
    case 16: return 0xF59563;
    case 32: return 0xF67C5F;
    case 64: return 0xF65E3B;
    case 128: return 0xEDCF72;
    case 256: return 0xEDCC61;
    case 512: return 0xEDC850;
    default: return value >= 1024 ? 0xEDC22E : 0xEDC53F;
  }
}
void style_menu_selection(lv_obj_t *row, bool selected) {
      lv_obj_set_style_bg_color(row, lv_color_hex(selected ? 0x292313 : 0x0D0D0D), 0);
      lv_obj_set_style_text_color(row, lv_color_hex(selected ? 0xF4C95D : 0xE8E6DF), 0);
      lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
      lv_obj_set_style_border_color(row, lv_color_hex(selected ? 0xF4C95D : 0x242424), 0);
}
void update_local_menu() {
  if (!g_menu_panel) {
    g_menu_panel = lv_obj_create(lv_screen_active());
    style_plain_object(g_menu_panel);
    lv_obj_set_style_radius(g_menu_panel, 0, 0);
    lv_obj_set_size(g_menu_panel, 240, 240);
    lv_obj_set_pos(g_menu_panel, 0, 0);
    lv_obj_set_style_bg_color(g_menu_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_menu_panel, LV_OPA_COVER, 0);
    g_menu_text = lv_label_create(g_menu_panel);
    lv_obj_set_pos(g_menu_text, 16, 14);
    lv_label_set_text(g_menu_text, "设置");
    lv_obj_set_style_text_font(g_menu_text, g_heading_font, 0);
    for (int i=0; i<4; ++i) {
      g_menu_rows[i] = lv_obj_create(g_menu_panel);
      style_plain_object(g_menu_rows[i]);
      lv_obj_set_size(g_menu_rows[i], 208, 38);
      lv_obj_set_pos(g_menu_rows[i], 16, 46+i*41);
      lv_obj_set_style_radius(g_menu_rows[i], 6, 0);
      lv_obj_set_style_bg_opa(g_menu_rows[i], LV_OPA_COVER, 0);
      auto label = lv_label_create(g_menu_rows[i]);
      lv_label_set_text(label, i == 0 ? "无线网络" : i == 1 ? "绑定官网" : i == 2 ? "设备信息" : "歌曲库");
      lv_obj_set_width(label, 184);
      lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
      lv_obj_align(label, LV_ALIGN_LEFT_MID, 12, 0);
    }
    g_product_list.create(g_menu_panel, g_body_font);
    g_launcher.create(g_menu_panel, g_heading_font, lunaros_font_runtime_12());
    g_cover_view = lv_image_create(g_menu_panel);
    lv_obj_set_pos(g_cover_view, 152, 60);
    lv_image_set_pivot(g_cover_view, 0, 0);
    lv_image_set_scale(g_cover_view, 77); // 240px source -> 72px preview
    lv_obj_add_flag(g_cover_view, LV_OBJ_FLAG_HIDDEN);
    auto hint = lv_label_create(g_menu_panel);
    g_menu_hint = hint;
    lv_label_set_text(hint, "");
    lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(hint, 16, 212);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8A8984), 0);
    lv_obj_set_style_text_color(g_menu_text, lv_color_hex(0xE8E6DF), 0);
    g_game2048_score = lv_label_create(g_menu_panel);
    lv_obj_set_pos(g_game2048_score, 16, 42);
    lv_obj_set_width(g_game2048_score, 208);
    lv_obj_set_style_text_color(g_game2048_score, lv_color_hex(0xF4C95D), 0);
    lv_obj_add_flag(g_game2048_score, LV_OBJ_FLAG_HIDDEN);
    g_game2048_panel = lv_obj_create(g_menu_panel);
    style_plain_object(g_game2048_panel);
    lv_obj_set_pos(g_game2048_panel, 16, 64);
    lv_obj_set_size(g_game2048_panel, 208, 140);
    lv_obj_set_style_bg_opa(g_game2048_panel, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(g_game2048_panel, LV_OBJ_FLAG_HIDDEN);
    for (int cell = 0; cell < 16; ++cell) {
      const int column = cell % 4;
      const int row = cell / 4;
      auto *tile = lv_obj_create(g_game2048_panel);
      style_plain_object(tile);
      lv_obj_set_pos(tile, 2 + column * 50, 2 + row * 34);
      lv_obj_set_size(tile, 48, 32);
      lv_obj_set_style_radius(tile, 4, 0);
      lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
      g_game2048_tiles[cell] = tile;
      auto *value = lv_label_create(tile);
      lv_obj_center(value);
      lv_obj_set_style_text_font(value, &lv_font_montserrat_16, 0);
      lv_obj_set_style_text_color(value, lv_color_hex(0x262626), 0);
      g_game2048_values[cell] = value;
    }
    g_binding_code = lv_label_create(g_menu_panel);
    lv_obj_set_pos(g_binding_code, 16, 52);
    lv_obj_set_style_text_font(g_binding_code, &lv_font_montserrat_36, 0);
    g_binding_info = lv_label_create(g_menu_panel);
    lv_obj_set_pos(g_binding_info, 16, 102);
    lv_obj_set_width(g_binding_info, 208);
    g_avatar_frame = lv_obj_create(g_menu_panel);
    style_plain_object(g_avatar_frame);
    lv_obj_set_size(g_avatar_frame, 96, 96);
    lv_obj_set_pos(g_avatar_frame, 72, 52);
    lv_obj_set_style_radius(g_avatar_frame, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(g_avatar_frame, true, 0);
    lv_obj_set_style_bg_color(g_avatar_frame, lv_color_hex(0x242424), 0);
    g_avatar_view = lv_image_create(g_avatar_frame);
    g_avatar_placeholder = lv_label_create(g_avatar_frame);
    lv_label_set_text(g_avatar_placeholder, "账号");
    lv_obj_center(g_avatar_placeholder);
    for (int i = 0; i < 3; ++i) {
      g_info_labels[i] = lv_label_create(g_menu_panel);
      lv_obj_set_pos(g_info_labels[i], 16, 48 + i * 52);
      lv_obj_set_style_text_color(g_info_labels[i], lv_color_hex(0x8A8984), 0);
      g_info_values[i] = lv_label_create(g_menu_panel);
      lv_obj_set_pos(g_info_values[i], 16, 69 + i * 52);
      lv_obj_set_width(g_info_values[i], 208);
      lv_label_set_long_mode(g_info_values[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    g_calendar_panel = lv_obj_create(g_menu_panel);
    style_plain_object(g_calendar_panel);
    lv_obj_set_pos(g_calendar_panel, 16, 64);
    lv_obj_set_size(g_calendar_panel, 208, 148);
    lv_obj_set_style_bg_opa(g_calendar_panel, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(g_calendar_panel, LV_OBJ_FLAG_HIDDEN);
    for (int column = 0; column < 7; ++column) {
      auto *label = lv_label_create(g_calendar_panel);
      lv_obj_set_pos(label, column * 30, 0);
      lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
      constexpr const char *names[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
      lv_label_set_text(label, names[column]);
    }
    for (int cell = 0; cell < 42; ++cell) {
      g_calendar_days[cell] = lv_label_create(g_calendar_panel);
      lv_obj_set_pos(g_calendar_days[cell], (cell % 7) * 30, 20 + (cell / 7) * 21);
      lv_obj_set_size(g_calendar_days[cell], 28, 20);
      lv_obj_set_style_text_font(g_calendar_days[cell], &lv_font_montserrat_16, 0);
      lv_obj_set_style_text_align(g_calendar_days[cell], LV_TEXT_ALIGN_CENTER, 0);
    }
    g_hold_hint = lv_label_create(lv_screen_active());
    lv_obj_set_style_bg_color(g_hold_hint, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_hold_hint, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(g_hold_hint, lv_color_hex(0xF4C95D), 0);
    lv_obj_align(g_hold_hint, LV_ALIGN_BOTTOM_MID, 0, -4);
  }
  const auto menu = local_menu_snapshot();
  const auto current_navigation = product::product_navigation_snapshot();
  update_cover_view(menu.product_shell && current_navigation.page == product::Page::Feature &&
      current_navigation.module == product::Module::Music);
  if (!menu.product_shell || current_navigation.page != product::Page::Feature ||
      current_navigation.module != product::Module::Information)
    lv_obj_add_flag(g_calendar_panel, LV_OBJ_FLAG_HIDDEN);
  if (!menu.product_shell || current_navigation.page != product::Page::Feature ||
      current_navigation.module != product::Module::Games || current_navigation.item != 1) {
    lv_obj_add_flag(g_game2048_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_game2048_score, LV_OBJ_FLAG_HIDDEN);
  }
  if (menu.product_shell) {
    const auto product_state = product::product_state_snapshot(
        static_cast<uint32_t>(esp_timer_get_time() / 1000000),
        static_cast<uint32_t>(esp_timer_get_time() / 1000));
    const auto nav = product_state.navigation;
    if (nav.page == product::Page::ModuleList) {
      g_product_list.hide();
      g_launcher.show(nav.item);
      lv_obj_remove_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(g_menu_panel);
      g_local_overlay = true;
      return;
    }
    g_launcher.hide();
    const char *module_titles[] = {"音乐", "助手", "时间管理", "信息看板", "游戏娱乐", "电脑协同", "设置"};
    const char *title = nav.page == product::Page::ModuleMenu ? module_titles[static_cast<int>(nav.module)] :
        nav.page == product::Page::PetActions ? "宠物动作" :
        nav.page == product::Page::Settings ? "设置" : "功能";
    set_label_text(g_menu_text, title);
    lv_obj_add_flag(g_binding_code, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_binding_info, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_avatar_frame, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 3; ++i) {
      lv_obj_add_flag(g_info_labels[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g_info_values[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (auto row : g_menu_rows) lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    std::array<std::string, AmoledList::kMaxItems> labels{};
    std::array<const char *, AmoledList::kMaxItems> icons{};
    int count = 0;
    int selected = nav.item;
    const bool catalog_ready = nav.page == product::Page::PetActions &&
        product::pet_animation_catalog_ready();
    if (nav.page == product::Page::ModuleMenu) {
      if (nav.module == product::Module::Music) labels = {"本地音乐", "白噪声", "", ""};
      else if (nav.module == product::Module::Assistant) labels = {"语音对话", "宠物动作", "", ""};
      else if (nav.module == product::Module::Games) labels = {"电子木鱼", "2048", "", ""};
      else if (nav.module == product::Module::Time) labels = {"时钟", "番茄钟与提醒", "", ""};
      else if (nav.module == product::Module::Information) labels = {"日历", "天气", "", ""};
      else labels = {"打开", "", "", ""};
      count = labels[1].empty() ? 1 : 2;
    } else if (nav.page == product::Page::Settings) {
      labels = {"无线网络", "设备信息", "绑定账号", ""};
      icons = {LV_SYMBOL_WIFI, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS};
      count = 3;
    } else if (nav.page == product::Page::PetActions && catalog_ready) {
      const int first = std::clamp(int(nav.item) - 3, 0,
          int(product::kPetActionRows - AmoledList::kMaxItems));
      count = AmoledList::kMaxItems;
      selected = nav.item - first;
      for (int row = 0; row < count; ++row) {
        const int item = first + row;
        labels[row] = item == 0 ? "睡觉 / 叫醒" : product::kPetActionNames[item - 1];
        icons[row] = item == 0 ? LV_SYMBOL_SETTINGS : LV_SYMBOL_PLAY;
      }
    }
    if (nav.page == product::Page::Feature) {
      g_product_list.hide();
      std::string detail;
      if (nav.module == product::Module::Music) {
        const auto speaker = status_snapshot();
        set_label_text(g_menu_text, nav.item == 1 ? "白噪声" : "音乐");
        detail = !speaker.audio_ready ? "音频未就绪" : speaker.white_noise ? "白噪声 · 循环" : !speaker.sd_ready ? "存储卡未就绪" :
            speaker.current_name.empty() ? "还没有歌曲" : speaker.current_name;
        detail += speaker.playing ? "\n正在播放" : speaker.paused ? "\n已暂停" : "\n已停止";
        detail += " · 音量 " + std::to_string(speaker.volume);
        if (!speaker.lyric_line.empty()) detail += "\n" + speaker.lyric_line;
        if (!speaker.last_error.empty()) detail += "\n" + std::string(speaker_error_hint(speaker.last_error));
        const auto artwork = current_cover_snapshot();
        if (!speaker.white_noise) {
          if (!artwork.error.empty()) detail += "\n封面读取失败";
          else if (!artwork.image) detail += "\n暂无封面";
        }
        detail += "\n确认播放/暂停";
      } else if (nav.module == product::Module::Assistant) {
        set_label_text(g_menu_text, "助手");
        detail = xiaozhi_status_label() + "\n确认开始对话";
      } else if (nav.module == product::Module::Games && nav.item == 0) {
        detail = "电子木鱼\n功德 " + std::to_string(product_state.wood_fish.count);
      } else if (nav.module == product::Module::Games) {
        set_label_text(g_menu_text, "2048");
        set_label_text(g_game2048_score, "分数 " + std::to_string(product_state.game_score) +
            "    四向滑动移动");
        lv_obj_remove_flag(g_game2048_score, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_game2048_panel, LV_OBJ_FLAG_HIDDEN);
        for (size_t i = 0; i < product_state.game_board.size(); ++i) {
          const auto value = product_state.game_board[i];
          set_label_text(g_game2048_values[i], value == 0 ? "" : std::to_string(value));
          const auto background = lv_color_hex(game2048_tile_color(value));
          if (!lv_color_eq(lv_obj_get_style_bg_color(g_game2048_tiles[i], LV_PART_MAIN), background))
            lv_obj_set_style_bg_color(g_game2048_tiles[i], background, 0);
          const auto foreground = lv_color_hex(value > 4 ? 0xFFFFFF : 0x4A4037);
          if (!lv_color_eq(lv_obj_get_style_text_color(g_game2048_values[i], LV_PART_MAIN), foreground))
            lv_obj_set_style_text_color(g_game2048_values[i], foreground, 0);
        }
        detail.clear();
      } else if (nav.module == product::Module::Time) {
        if (nav.item == 0) {
          const auto clock = lunaredu::lunaros::network_snapshot().clock;
          const std::time_t epoch = std::time(nullptr);
          std::tm local = {};
          if (clock == lunaredu::lunaros::NetworkClockState::Valid && localtime_r(&epoch, &local)) {
            char value[40] = {};
            std::strftime(value, sizeof(value), "%Y-%m-%d\n%H:%M:%S", &local);
            detail = value;
          } else detail = "系统时间未校准";
        } else {
          detail = product_state.pomodoro.active ?
              (product_state.pomodoro.paused ? "番茄钟已暂停\n" : "番茄钟运行中\n") +
                  std::to_string(product_state.pomodoro.remaining_seconds) + " 秒" : "番茄钟未开始";
          if (product_state.time_operation_pending) detail += "\n正在保存…";
          if (!product_state.time_storage_ready) detail += "\n时间存储不可用";
          for (const auto pending : product_state.pending_reminders) {
            const auto reminder = std::find_if(product_state.reminders.begin(), product_state.reminders.end(),
                [pending](const product::Reminder &item) { return item.id == pending.id; });
            if (reminder != product_state.reminders.end()) detail += "\n待查看：" + reminder->title;
            if (pending.missed) detail += "（已错过）";
          }
        }
      } else if (nav.module == product::Module::Computer) {
        set_label_text(g_menu_text, "电脑协同");
        if (product_state.paired_computer.empty()) detail = "未配对电脑\n请通过网页配对采集端";
        else {
          auto metric = [](product::TelemetryStatus status, float value, const char *unit) {
            if (status != product::TelemetryStatus::Fresh) return std::string(product::telemetry_status_name(status));
            char number[32] = {};
            std::snprintf(number, sizeof(number), "%.1f%s", static_cast<double>(value), unit);
            return std::string(number);
          };
          const auto &sample = product_state.computer;
          detail = "CPU " + metric(sample.cpu, sample.cpu_percent, "%") +
              "\nGPU " + metric(sample.gpu, sample.gpu_percent, "%") +
              "\n温度 " + metric(sample.temperature, sample.temperature_c, "°C") +
              "\n风扇 " + metric(sample.fan, sample.fan_rpm, " rpm") +
              "\n" + std::to_string(sample.sample_age_ms / 1000) + " 秒前";
        }
      } else if (nav.module == product::Module::Information) {
        const std::time_t epoch = std::time(nullptr);
        const auto network = lunaredu::lunaros::network_snapshot();
        const auto weather = product::weather_snapshot(static_cast<uint32_t>(esp_timer_get_time() / 1000),
            epoch, network.clock == lunaredu::lunaros::NetworkClockState::Valid,
            network.link == lunaredu::lunaros::NetworkLinkState::Up);
        product::MonthlyCalendar calendar;
        if (nav.item == 1) {
          lv_obj_add_flag(g_calendar_panel, LV_OBJ_FLAG_HIDDEN);
          detail = weather.has_reading ?
              weather.config.place + "\n" + product::weather_code_label(weather.reading.code) + " " +
                  std::to_string(static_cast<int>(std::lround(weather.reading.temperature_c))) + "°C\n湿度 " +
                  std::to_string(weather.reading.humidity_percent) + "% · " +
                  std::to_string(weather.age_seconds / 60) + " 分钟前" :
              weather.status == "not_configured" ? "请在网页设置天气地点" :
                  "天气不可用\n" + weather.status;
          if (weather.status != "fresh" && weather.has_reading) detail += "\n" + weather.status;
          if (weather.refreshing) detail += "\n正在更新…";
          if (weather.retry_required) detail += "\n连续失败，请从网页重试";
          detail += "\nOpen-Meteo · CC BY 4.0";
        } else if (!product::make_local_calendar(epoch,
                lunaredu::lunaros::network_snapshot().clock == lunaredu::lunaros::NetworkClockState::Valid,
                product_state.reminders, product_state.pending_reminders, calendar)) {
          lv_obj_add_flag(g_calendar_panel, LV_OBJ_FLAG_HIDDEN);
          detail = "系统时间未校准\n月历不可用";
        } else {
          char date[24] = {};
          std::snprintf(date, sizeof(date), "%04d-%02u-%02u", calendar.year, calendar.month, calendar.today);
          set_label_text(g_menu_text, date);
          for (size_t i = 0; i < calendar.days.size(); ++i) {
            const auto &day = calendar.days[i];
            const auto label = g_calendar_days[i];
            set_label_text(label, std::to_string(day.day) + (day.has_reminder ? "*" : ""));
            const auto colour = lv_color_hex(day.today ? 0xF4C95D : day.in_month ? 0xE8E6DF : 0x555555);
            if (!lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), colour))
              lv_obj_set_style_text_color(label, colour, 0);
          }
          lv_obj_remove_flag(g_calendar_panel, LV_OBJ_FLAG_HIDDEN);
          detail = "* 提醒";
        }
      } else {
        detail = std::string(module_titles[static_cast<int>(nav.module)]) + "\n功能准备中";
      }
      set_label_text(g_menu_hint, detail);
      lv_obj_set_pos(g_menu_hint, 16, 60);
      lv_obj_set_width(g_menu_hint, 208);
      if (nav.module == product::Module::Music) lv_obj_set_width(g_menu_hint, 128);
      lv_label_set_long_mode(g_menu_hint, LV_LABEL_LONG_WRAP);
      if (nav.module == product::Module::Information && !lv_obj_has_flag(g_calendar_panel, LV_OBJ_FLAG_HIDDEN))
        lv_obj_set_y(g_menu_hint, 216);
      if (nav.module == product::Module::Games && nav.item == 1)
        lv_obj_add_flag(g_menu_hint, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_remove_flag(g_menu_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
      g_product_list.show(labels, icons, count, selected,
          static_cast<int>(nav.page) * 8 + static_cast<int>(nav.module));
      const std::string footer = nav.page == product::Page::PetActions ?
          (!catalog_ready ? "请在 SD 卡安装完整动画包" :
           std::to_string(nav.item + 1) + "/" + std::to_string(product::kPetActionRows)) :
          std::to_string(nav.item + 1) + "/" + std::to_string(count);
      set_label_text(g_menu_hint, footer);
      lv_obj_set_pos(g_menu_hint, 16, 212);
      lv_obj_set_width(g_menu_hint, 208);
      lv_label_set_long_mode(g_menu_hint, LV_LABEL_LONG_CLIP);
      lv_obj_remove_flag(g_menu_hint, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_menu_panel);
    g_local_overlay = true;
    return;
  }
  g_product_list.hide();
  g_launcher.hide();
  const bool overlay = menu.open || menu.binding || menu.information || menu.network || menu.library || menu.hold_percent > 0;
  static int previous_mode = -1;
  static int previous_selection = -1;
  static int previous_info_page = -1;
  static int previous_library_index = -1, previous_network_selected = -1;
  static int previous_account_selected = -1;
  static bool previous_unbind_confirm = false, previous_unbind_pending = false;
  static uint32_t binding_update_ms = 0;
  const int mode = menu.library ? 6 : menu.network ? 5 : menu.information ? 4 : menu.binding ? 3 : menu.open ? 2 : menu.hold_percent > 0 ? 1 : 0;
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  static lunaredu::account_resources::AccountResourcesSnapshot p;
  static uint32_t account_poll_ms = 0;
  if (account_poll_ms == 0 || now - account_poll_ms >= 500) {
    account_poll_ms = now;
    p = lunaredu::account_resources::snapshot();
    update_account_avatar(p);
    lv_mem_monitor_t memory{};
    lv_mem_monitor(&memory);
    portENTER_CRITICAL(&g_display_stats_lock);
    g_display_stats.account_avatar_available = p.account_avatar_resource_available;
    g_display_stats.lvgl_pool_free_bytes = memory.free_size;
    g_display_stats.lvgl_pool_largest_block = memory.free_biggest_size;
    portEXIT_CRITICAL(&g_display_stats_lock);
  }
  // Product pages also own this panel. Their exit must run the visibility
  // update even when the legacy cache still describes the same home state.
  if (g_local_overlay == overlay && mode == previous_mode && menu.selected == previous_selection &&
      menu.information_page == previous_info_page &&
      menu.library_index == previous_library_index && menu.network_selected == previous_network_selected &&
      menu.account_selected == previous_account_selected &&
      menu.unbind_confirm == previous_unbind_confirm && menu.unbind_pending == previous_unbind_pending &&
      (!(menu.binding || menu.open || menu.information || menu.network || menu.library) || now - binding_update_ms < 500)) return;
  previous_info_page = menu.information_page;
  previous_library_index = menu.library_index;
  previous_network_selected = menu.network_selected;
  previous_account_selected = menu.account_selected;
  previous_unbind_confirm = menu.unbind_confirm;
  previous_unbind_pending = menu.unbind_pending;
  previous_mode = mode;
  previous_selection = menu.selected;
  binding_update_ms = now;
  lv_obj_add_flag(g_binding_code, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_binding_info, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_avatar_frame, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < 3; ++i) {
    lv_obj_add_flag(g_info_labels[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_info_values[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(g_info_labels[i], 16, 48 + i * 52);
    lv_obj_set_pos(g_info_values[i], 16, 69 + i * 52);
    lv_obj_set_style_text_color(g_info_values[i], lv_color_hex(0xE8E6DF), 0);
  }
  const bool bound = p.state == lunaredu::lunaros::DeviceLinkBindingState::Bound;
  const char *account_title = bound ? "我的账号" : "绑定官网";
  set_label_text(g_menu_text, menu.library ? "歌曲库" : menu.network ? "无线网络" : menu.information ? "设备信息" : menu.binding ? account_title : "设置");
  lv_obj_add_flag(g_menu_hint, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < 4; ++i) {
    lv_obj_remove_flag(g_menu_rows[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(g_menu_rows[i], 16, 46 + i * 41);
    auto label = lv_obj_get_child(g_menu_rows[i], 0);
    const char *name = i == 0 ? "无线网络" : i == 1 ? account_title : i == 2 ? "设备信息" : "歌曲库";
    if (menu.open) {
      lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
      set_label_text(label, name);
    }
  }
  if (menu.open) {
    for (int i=0; i<4; ++i) {
      const bool selected = menu.selected == i;
      style_menu_selection(g_menu_rows[i], selected);
    }
    lv_obj_remove_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_menu_panel);
  } else lv_obj_add_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
  if (menu.binding) {
    using namespace lunaredu::lunaros;
    for (auto row : g_menu_rows) lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_binding_code, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_binding_info, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_binding_code, "");
    lv_obj_set_y(g_binding_info, bound ? 162 : 102);
    lv_obj_set_style_text_align(g_binding_info, bound ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(g_binding_info, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(g_binding_info, lv_color_hex(0xE8E6DF), 0);
    std::string info;
    if (menu.unbind_confirm) {
      set_label_text(g_menu_text, "退出账号");
      lv_obj_set_y(g_binding_info, 72);
      info = "退出后解除设备绑定";
      for (int i = 0; i < 2; ++i) {
        auto row = g_menu_rows[i];
        lv_obj_set_y(row, 132 + i * 45);
        auto label = lv_obj_get_child(row, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        set_label_text(label, i == 0 ? "取消" : "确认退出");
        style_menu_selection(row, menu.account_selected == i);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
      }
    } else if (bound) {
      info = p.account_display_name.empty() ? "明月账号" : p.account_display_name;
      lv_obj_add_flag(g_binding_code, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(g_avatar_frame, LV_OBJ_FLAG_HIDDEN);
      const auto *avatar = account_avatar_image();
      static uint32_t avatar_generation = UINT32_MAX;
      if (avatar_generation != account_avatar_generation()) {
        avatar_generation = account_avatar_generation();
        if (avatar) lv_image_set_src(g_avatar_view, avatar);
        lv_obj_invalidate(g_avatar_view);
      }
      if (avatar) {
        lv_obj_remove_flag(g_avatar_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_avatar_placeholder, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(g_avatar_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_avatar_placeholder, LV_OBJ_FLAG_HIDDEN);
      }
      lv_obj_set_style_text_color(g_binding_info,
          lv_color_hex(menu.account_selected == 0 ? 0xF4C95D : 0xE8E6DF), 0);
      auto action = g_menu_rows[0];
      lv_obj_set_y(action, 190);
      auto label = lv_obj_get_child(action, 0);
      lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
      set_label_text(label, menu.unbind_pending ?
          (network_snapshot().link == NetworkLinkState::Up ? "正在退出…" : "等待联网…") :
          !menu.account_notice.empty() ? menu.account_notice : "退出账号");
      style_menu_selection(action, menu.account_selected == 1 && !menu.unbind_pending);
      lv_obj_remove_flag(action, LV_OBJ_FLAG_HIDDEN);
    }
    else if (menu.unbind_pending) info = "正在退出…";
    else if (!menu.account_notice.empty()) info = menu.account_notice;
    else if (network_snapshot().link != NetworkLinkState::Up) info = "请先联网";
    else if (p.state == DeviceLinkBindingState::Pairing) {
      const int64_t seconds = (p.expires_at_ms - static_cast<int64_t>(std::time(nullptr))*1000)/1000;
      if (seconds > 0) {
        lv_label_set_text(g_binding_code, p.code.c_str());
        info = "有效期 " + std::to_string(seconds/60) + "分" + std::to_string(seconds%60) + "秒";
      } else info = "已过期";
    } else if (p.state == DeviceLinkBindingState::Connecting) info = "正在获取…";
    else if (p.state == DeviceLinkBindingState::Error) {
      info = p.error == "pairing_rate_limited" ? "请稍后重试" :
        p.error == "device_already_bound" ? "设备已绑定账号" : "连接失败";
    } else info = "获取绑定码";
    set_label_text(g_binding_info, info);
    lv_obj_add_flag(g_menu_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_menu_panel);
  }
  if (menu.network) {
    for (auto row : g_menu_rows) lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    using namespace lunaredu::lunaros;
    const auto network = network_snapshot();
    const bool connected = network.link == NetworkLinkState::Up;
    set_label_text(g_info_labels[0], connected ? "已连接" : network.link == NetworkLinkState::Connecting ? "正在连接" : "未连接");
    set_label_text(g_info_values[0], network.target_ssid.empty() ? "尚未配置网络" : network.target_ssid);
    lv_obj_set_style_text_color(g_info_values[0], lv_color_hex(menu.network_selected == 0 ? 0xF4C95D : 0xE8E6DF), 0);
    set_label_text(g_info_labels[1], "IP 地址");
    set_label_text(g_info_values[1], connected ? network.local_ip : "—");
    for (int i = 0; i < 2; ++i) {
      lv_obj_remove_flag(g_info_labels[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(g_info_values[i], LV_OBJ_FLAG_HIDDEN);
    }
    auto action = g_menu_rows[0];
    lv_obj_set_y(action, 169);
    auto label = lv_obj_get_child(action, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    set_label_text(label, "更换网络");
    style_menu_selection(action, menu.network_selected == 1);
    lv_obj_remove_flag(action, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_menu_panel);
  }
  if (menu.library) {
    const auto library = local_library_snapshot();
    const auto speaker = status_snapshot();
    for (auto row : g_menu_rows) lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    const int count = static_cast<int>(library.tracks.size());
    const int first = (library.selected / 3) * 3;
    for (int row = 0; row < 3 && first + row < count; ++row) {
      const int index = first + row;
      const auto &track = library.tracks[index];
      const bool selected = index == library.selected;
      auto object = g_menu_rows[row];
      lv_obj_set_y(object, 50 + row * 49);
      style_menu_selection(object, selected);
      auto label = lv_obj_get_child(object, 0);
      std::string name = track.name;
      if (name.size() > 4) name.resize(name.size() - 4);
      if (speaker.playing && speaker.current_path == track.path) name = "播放 · " + name;
      if (name != lv_label_get_text(label)) {
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_label_set_text(label, name.c_str());
      }
      const auto long_mode = selected ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT;
      if (lv_label_get_long_mode(label) != long_mode) lv_label_set_long_mode(label, long_mode);
      lv_obj_set_style_anim_duration(label, lv_anim_speed_clamped(24, 1000, 18000), 0);
      lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
    std::string footer = count ? std::to_string(library.selected + 1) + "/" + std::to_string(count) :
        library.sd_ready ? "还没有歌曲" : "存储卡未就绪";
    if (library.uploading) footer = "正在接收歌曲";
    else if (!menu.library_notice.empty()) footer = menu.library_notice;
    set_label_text(g_menu_hint, footer);
    lv_obj_remove_flag(g_menu_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_menu_panel);
  }
  if (menu.information) {
    for (auto row : g_menu_rows) lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    using namespace lunaredu::lunaros;
    std::array<std::string, 3> names, values;
    if (menu.information_page == 0) {
      names = {"设备", "固件版本", "设备编号"};
      values = {"明月智能音箱", firmware_version(), hardware_serial_number()};
    } else if (menu.information_page == 1) {
      const auto network = network_snapshot();
      names = {"无线网络", "连接状态", "IP 地址"};
      values = {network.target_ssid.empty() ? "未配置" : network.target_ssid,
          network.link == NetworkLinkState::Up ? "已连接" :
          network.link == NetworkLinkState::Connecting ? "正在连接" : "未连接",
          network.local_ip.empty() ? "—" : network.local_ip};
    } else {
      const auto card = lunaredu::device::tfcard_store_status();
      const auto speaker = status_snapshot();
      char capacity[32];
      std::snprintf(capacity, sizeof(capacity), "%.1f GB", card.capacity_bytes / 1e9);
      names = {"存储卡", "总容量", "歌曲"};
      values = {card.mounted ? "已就绪" : "未就绪", card.mounted ? capacity : "—",
                std::to_string(speaker.playlist_count) + " 首"};
    }
    for (int i = 0; i < 3; ++i) {
      set_label_text(g_info_labels[i], names[i]);
      set_label_text(g_info_values[i], values[i]);
      lv_obj_remove_flag(g_info_labels[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(g_info_values[i], LV_OBJ_FLAG_HIDDEN);
    }
    set_label_text(g_menu_hint, std::to_string(menu.information_page + 1) + "/3");
    lv_obj_remove_flag(g_menu_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_menu_panel);
  }
  if (menu.hold_percent > 0) {
    lv_label_set_text(g_hold_hint, "设置");
    lv_obj_remove_flag(g_hold_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_hold_hint);
  } else lv_obj_add_flag(g_hold_hint, LV_OBJ_FLAG_HIDDEN);
  if (g_local_overlay && !overlay) {
    lv_obj_invalidate(lv_screen_active());
    g_force_character_redraw = true;
  }
  g_local_overlay = overlay;
}

void set_visible(lv_obj_t *object, bool visible) {
  if (visible == !lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return;
  if (visible) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

void update_character_layout(bool visible) {
  if (visible != g_character_layout) {
    g_character_layout = visible;
    lv_obj_invalidate(lv_screen_active());
    g_force_character_redraw = true;
    // Complete the old layout erase before painting outside LVGL's scene graph.
    lv_timer_ready(lv_display_get_refr_timer(lv_display_get_default()));
  }
}

void update_playback_indicator(bool visible, float opacity, float level) {
  level = std::round(level * 108) / 108;
  opacity = std::round(opacity * 255) / 255;
  const bool changed = g_indicator_opacity != opacity ||
      g_indicator_level != level;
  g_indicator_opacity = opacity;
  g_indicator_level = level;
  set_visible(g_volume, visible);
  if (visible && changed) {
    // One small fixed damage rectangle, without per-stroke movement or
    // opacity layers. Stable endpoints do not invalidate the footer.
    lv_obj_invalidate(g_volume);
  }
}

bool render_character_direct(const character::Pose &pose, int screen_top) {
  const uint64_t started = esp_timer_get_time();
  g_character_renderer.prepare(pose);
  uint32_t raster_us = static_cast<uint32_t>(esp_timer_get_time() - started);
  uint32_t transfer_us = 0;
  uint32_t transferred_pixels = 0, transferred_regions = 0;
  std::string error;
  bool ok = true;
  constexpr size_t capacity = character::kSize * character::kTileRows;
  const int inset = pose.mood == character::Mood::Radar ? 32 : 0;
  const int width = character::kSize - inset * 2;
  const int bottom = character::kSize - inset;
  for (int top = inset; top < bottom; top += character::kTileRows) {
    const int rows = std::min(character::kTileRows, bottom - top);
    uint64_t stage_started = esp_timer_get_time();
    const bool rendered = g_character_renderer.render_tile(top, rows, g_character_tile, capacity);
    if (rendered && inset) {
      for (int row = 0; row < rows; ++row)
        std::memmove(g_character_tile + row * width,
                     g_character_tile + row * character::kSize + inset, width * sizeof(uint16_t));
    }
    raster_us += static_cast<uint32_t>(esp_timer_get_time() - stage_started);
    stage_started = esp_timer_get_time();
    bool transferred = rendered;
    auto *previous = g_character_previous + top * character::kSize;
    if (rendered) {
      const auto regions = frame_delta(previous, g_character_tile, width, rows, g_force_character_redraw);
      for (unsigned i = 0; i < regions.count; ++i) {
        const auto &r = regions.items[i];
        for (int row = 0; row < r.height; ++row)
          std::memcpy(g_character_transfer + row * r.width,
              g_character_tile + (row + r.y) * width + r.x, r.width * sizeof(uint16_t));
        transferred = lunaredu::device::device_spi_lcd_surface_draw_rgb565_bitmap(
            40 + inset + r.x, screen_top + top + r.y, r.width, r.height,
            g_character_transfer, false, &error,
            lunaredu::device::DeviceSpiLcdTransferMode::kWaitForCompletion);
        if (!transferred) break;
        for (int row = 0; row < r.height; ++row)
          std::memcpy(previous + (r.y + row) * width + r.x,
              g_character_tile + (r.y + row) * width + r.x, r.width * sizeof(uint16_t));
        transferred_pixels += r.width * r.height;
        ++transferred_regions;
      }
    }
    transfer_us += static_cast<uint32_t>(esp_timer_get_time() - stage_started);
    if (!transferred) {
      ok = false;
      break;
    }
  }
  const uint32_t elapsed = static_cast<uint32_t>(esp_timer_get_time() - started);
  portENTER_CRITICAL(&g_display_stats_lock);
  if (ok) {
    ++g_display_stats.character_render_count;
    g_display_stats.last_character_render_us = elapsed;
    g_display_stats.last_character_raster_us = raster_us;
    g_display_stats.last_character_transfer_us = transfer_us;
    g_display_stats.character_transferred_pixels += transferred_pixels;
    g_display_stats.last_character_pixels = transferred_pixels;
    g_display_stats.last_character_regions = transferred_regions;
    if (pose.mood == character::Mood::Progress || pose.mood == character::Mood::Radar) {
      ++g_display_stats.progress_render_count;
      g_display_stats.last_progress_render_us = elapsed;
    }
    g_display_stats.max_character_render_us = std::max(g_display_stats.max_character_render_us, elapsed);
    if (elapsed > character::kFrameMs * 1000) ++g_display_stats.character_over_budget_count;
    // Leave headroom after a slow transfer; never raise this task's priority.
    g_display_stats.character_frame_ms = character::kFrameMs;
  } else {
    ++g_display_stats.character_render_fail_count;
  }
  const uint32_t failures = g_display_stats.character_render_fail_count;
  portEXIT_CRITICAL(&g_display_stats_lock);
  if (!ok && failures % 25 == 1)
    ESP_LOGW(kTag, "character draw failed: %s", error.c_str());
  return ok;
}

void display_task(void *) {
  const bool surface_ready = lunaredu::device::device_spi_lcd_surface_setup();
  lunaredu::desktop::LvglScreenRuntimeConfig config;
  config.draw_buffer_rows = 32;
  config.notify_flush = notify_character_flush;
  if (surface_ready) config.surface = lunaredu::device::device_spi_lcd_desktop_surface();
  if (!surface_ready || !lunaredu::desktop::setup_lvgl_screen_runtime(config)) {
    ESP_LOGE(kTag, "ST7789/LVGL setup failed");
    return;
  }
  constexpr size_t tile_bytes = character::kSize * character::kTileRows * sizeof(uint16_t);
  // Hot raster/packing tiles benefit from internal SRAM. The large shadow
  // remains in PSRAM; fall back gracefully when contiguous SRAM is scarce.
  auto allocate_tile = []() {
    void *tile = heap_caps_malloc(tile_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!tile) tile = heap_caps_malloc(tile_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return static_cast<uint16_t *>(tile);
  };
  g_character_tile = allocate_tile();
  g_character_previous = static_cast<uint16_t *>(heap_caps_calloc(
      character::kSize * character::kSize, sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  g_character_transfer = allocate_tile();
  if (!g_character_tile || !g_character_previous || !g_character_transfer) {
    heap_caps_free(g_character_tile);
    heap_caps_free(g_character_previous);
    heap_caps_free(g_character_transfer);
    ESP_LOGE(kTag, "character tile allocation failed");
    return;
  }
  g_force_character_redraw = true;
  if (!lunaros_font_runtime_setup()) {
    ESP_LOGE(kTag, "Chinese font initialization failed: %s",
             lunaros_font_runtime_failure_reason());
    return;
  }
  g_body_font = lunaros_font_runtime_16();
  g_heading_font = lunaros_font_runtime_20();
  ESP_LOGI(kTag, "Shared Chinese fonts ready");
  auto *pet_buffer = static_cast<uint8_t *>(heap_caps_calloc(
      1, HomePetView::kBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pet_buffer) ESP_LOGW(kTag, "home pet buffer allocation failed");
  make_ui(pet_buffer);
  // First visible UI is a boot overlay on the same LVGL display. No extra
  // task/timer or LCD writer; all objects are deleted once it yields the UI.
  g_boot.create(lv_layer_top(), g_home, esp_app_get_description()->version,
                static_cast<uint32_t>(esp_timer_get_time() / 1000));
  portENTER_CRITICAL(&g_display_stats_lock);
  g_display_stats.boot_visible = true;
  portEXIT_CRITICAL(&g_display_stats_lock);
  ESP_LOGI(kTag, "boot screen shown");
  update_setup_screen();
  uint32_t last_status_ms = 0, last_status_activity = 0;
  uint32_t last_home_ms = 0;
  uint32_t last_stack_sample_ms = 0;
  bool previous_home_visible = false;
  // Only this resident task accesses these snapshots. Keep their retained
  // storage separate from LVGL's nested layout and draw calls.
  static RuntimeStatus current_status;
  current_status = status_snapshot();
  uint32_t last_character_ms = 0;
  static PetAnimation pet_animation;
  static std::string reminder_hint, last_character_hint;
  uint32_t last_reminder_poll = 0;
  int last_drawn_pet_frame = -1;
  std::string previous_error;
  uint32_t error_seen_ms = 0;
  uint32_t error_generation = 0;
  bool previous_transfer_page = false;
  // The resident task already has a deep LVGL drawing call chain. Keep the
  // bounded boot histogram and baseline outside the task stack.
  static const auto boot_start_display = lunaredu::desktop::lvgl_screen_runtime_status();
  static std::array<uint16_t, 40> boot_frame_histogram{};
  static uint64_t boot_previous_poll_us = 0, boot_total_frame_us = 0;
  static uint32_t boot_frames = 0, boot_max_frame_us = 0, boot_slow_frames = 0;
  static uint32_t boot_max_refresh_us = 0;
  for (;;) {
    const uint64_t poll_us = esp_timer_get_time();
    const uint32_t poll_ms = static_cast<uint32_t>(poll_us / 1000);
    if (poll_ms - last_stack_sample_ms >= 1000) {
      const auto minimum = uxTaskGetStackHighWaterMark(nullptr);
      portENTER_CRITICAL(&g_display_stats_lock);
      g_display_stats.display_stack_min_free_bytes = minimum;
      portEXIT_CRITICAL(&g_display_stats_lock);
      last_stack_sample_ms = poll_ms;
    }
    const bool measuring_boot = g_boot.active();
    if (measuring_boot) {
      if (boot_previous_poll_us) {
        const auto interval = static_cast<uint32_t>(poll_us - boot_previous_poll_us);
        ++boot_frames;
        boot_total_frame_us += interval;
        boot_max_frame_us = std::max(boot_max_frame_us, interval);
        if (interval > 50000) ++boot_slow_frames;
        ++boot_frame_histogram[std::min<size_t>(interval / 10000, boot_frame_histogram.size() - 1)];
      }
      boot_previous_poll_us = poll_us;
    }
    const uint32_t poll_activity = visual_activity_snapshot();
    if (g_boot.active()) {
      const auto menu = local_menu_snapshot();
      // Explicit navigation wins immediately; never cover a requested menu.
      if (menu.open || menu.product_shell || menu.binding || menu.information ||
          menu.network || menu.library || menu.provisioning || menu.hold_percent > 0) {
        g_boot.dismiss();
      } else {
        const auto network = lunaredu::lunaros::network_snapshot();
        const BootNetworkStatus boot_network{
            network.link == lunaredu::lunaros::NetworkLinkState::Up,
            network.clock == lunaredu::lunaros::NetworkClockState::Valid,
            network.provisioning == lunaredu::lunaros::NetworkProvisioningState::Active ||
                !network.credentials_available};
        const bool was_revealing = g_boot.revealing();
        const bool home_target = !g_setup_visible && !boot_network.needs_setup;
        g_boot.update(poll_ms, boot_network, home_target);
        if (!was_revealing && g_boot.revealing())
          ESP_LOGI(kTag, "boot home reveal started");
      }
      if (!g_boot.active()) {
        portENTER_CRITICAL(&g_display_stats_lock);
        g_display_stats.boot_visible = false;
        portEXIT_CRITICAL(&g_display_stats_lock);
        g_force_character_redraw = true;
        ESP_LOGI(kTag, "boot screen dismissed");
      }
    }
    if (poll_ms - last_status_ms >= 80 || poll_activity != last_status_activity) {
      last_status_ms = poll_ms;
      last_status_activity = poll_activity;
      current_status = status_snapshot();
      update_setup_screen();
      update_status(current_status);
    }
    if (!g_boot.active()) update_local_menu();
    const auto assistant = lunaredu::pet::conversation::runtime_snapshot();
    const bool voice_busy = assistant.interaction_active;
    const auto local_menu = local_menu_snapshot();
    const bool menu_page = local_menu.open || local_menu.product_shell || local_menu.binding || local_menu.information || local_menu.network || local_menu.library;
    const bool home_visible = !menu_page && !g_setup_visible;
    const bool home_face_visible = home_visible;
    const auto resources = product::desktop_resources_snapshot();
    const bool background_changed = resources.pixels != g_wallpaper_pixels;
    if (background_changed) {
      // Drop LVGL's cached decoder before releasing the old immutable buffer.
      lv_obj_add_flag(g_wallpaper_view, LV_OBJ_FLAG_HIDDEN);
      lv_image_set_src(g_wallpaper_view, nullptr);
      lv_image_cache_drop(&g_wallpaper_image);
      g_wallpaper_pixels = resources.pixels;
      if (g_wallpaper_pixels) {
        g_wallpaper_image.header.magic = LV_IMAGE_HEADER_MAGIC;
        g_wallpaper_image.header.cf = LV_COLOR_FORMAT_RGB565;
        g_wallpaper_image.header.w = g_wallpaper_image.header.h = product::kWallpaperSide;
        g_wallpaper_image.header.stride = product::kWallpaperSide * 2;
        g_wallpaper_image.data_size = g_wallpaper_pixels->size() * 2;
        g_wallpaper_image.data = reinterpret_cast<const uint8_t *>(g_wallpaper_pixels->data());
        lv_image_set_src(g_wallpaper_view, &g_wallpaper_image);
      }
      lv_obj_invalidate(lv_screen_active());
    }
    set_visible(g_wallpaper_view, home_visible && g_wallpaper_pixels);
    if (home_face_visible && (!previous_home_visible || background_changed || poll_ms - last_home_ms >= 1000)) {
      const auto network = lunaredu::lunaros::network_snapshot();
      const bool trusted = network.clock == lunaredu::lunaros::NetworkClockState::Valid;
      const std::time_t epoch = std::time(nullptr);
      std::tm local{};
      const bool local_valid = trusted && localtime_r(&epoch, &local) != nullptr;
      const auto weather = product::weather_snapshot(poll_ms, epoch, trusted,
          network.link == lunaredu::lunaros::NetworkLinkState::Up);
      const bool missing = !g_wallpaper_pixels && !resources.selected.empty();
      g_home.show(home_content(local_valid ? &local : nullptr, weather, missing),
                  g_wallpaper_pixels != nullptr);
      last_home_ms = poll_ms;
    }
    if (!home_face_visible) g_home.hide();
    previous_home_visible = home_face_visible;
    if (g_boot.active()) {
      smart_speaker_set_pet_touch_target(0);
      // Resume character/lyrics work when the opening yields the LCD.
      lunaredu::desktop::lvgl_screen_runtime_handler();
      boot_max_refresh_us = std::max(boot_max_refresh_us,
          lunaredu::desktop::lvgl_screen_runtime_status().last_refresh_us);
      const auto work_us = static_cast<uint32_t>(esp_timer_get_time() - poll_us);
      const uint32_t period_us = kDisplayFrameMs * 1000;
      const uint32_t sleep_ms = work_us < period_us ? (period_us - work_us) / 1000 : 0;
      vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(sleep_ms)));
      continue;
    }
    const bool holding = !menu_page && !g_setup_visible && local_menu.hold_percent > 0;
    const bool setup_character = !menu_page && g_setup_visible && g_setup_character;
    const bool character_allowed = home_visible || setup_character || voice_busy;
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    const uint32_t activity = visual_activity_snapshot();
    const auto motion = smart_speaker_motion_input();
    const auto transfer = transfer_snapshot();
    const bool transfer_active = transfer.receiving || transfer.batch_open;
    const bool transfer_page = !menu_page && !g_setup_visible && !holding && transfer.generation &&
        !transfer_dismissed(transfer.generation) &&
        (transfer_active || now - transfer.updated_ms < 2500);
    if (transfer_page != previous_transfer_page) {
      lv_obj_invalidate(lv_screen_active());
      g_force_character_redraw = true;
      previous_transfer_page = transfer_page;
    }
    if (current_status.last_error != previous_error || transfer.generation != error_generation) {
      previous_error = current_status.last_error;
      error_generation = transfer.generation;
      error_seen_ms = now;
    }
    const bool error_hint = !previous_error.empty() && now - error_seen_ms < 3500;
    auto feedback = holding ? character::Feedback::Hold : g_network_feedback;
    uint8_t percent = static_cast<uint8_t>(std::clamp(local_menu.hold_percent, 0, 100));
    if (!holding && !g_setup_visible && g_network_feedback == character::Feedback::None) {
      if (transfer_page) {
        feedback = transfer_active ? character::Feedback::Uploading :
            transfer.failed ? character::Feedback::Error : character::Feedback::Success;
        percent = transfer.total ? static_cast<uint8_t>(std::min<uint64_t>(99, uint64_t(transfer.received) * 100 / transfer.total)) : 0;
        if (!transfer.receiving) percent = 100;
      } else if (current_status.protection_active || error_hint) feedback = character::Feedback::Error;
    }
    character::Pose pose = g_character.update(now, {
        current_status.playing,
        !character_allowed,
        activity >> 3, static_cast<character::Action>(activity & 7U),
        feedback, percent,
        current_status.volume, CONFIG_LUNAREDU_SMART_SPEAKER_MAX_VOLUME,
        dual_i2s_expression_level(),
        motion.valid, motion.tilt_x, motion.tilt_y, motion.twist_z,
        motion.angular_speed_dps});
    AssistantVisualState assistant_visual = AssistantVisualState::Inactive;
    if (assistant.speaking) assistant_visual = AssistantVisualState::Speaking;
    else if (assistant.thinking) assistant_visual = AssistantVisualState::Thinking;
    else if (assistant.listening || (assistant.opening && voice_busy)) assistant_visual = AssistantVisualState::Listening;
    else if (assistant.failed) assistant_visual = AssistantVisualState::Failed;
    else {
      const auto until = g_assistant_emotion_until_ms.load(std::memory_order_acquire);
      if (until != 0 && static_cast<int32_t>(until - now) > 0)
        assistant_visual = static_cast<AssistantVisualState>(
            g_assistant_emotion.load(std::memory_order_acquire));
    }
    const bool animation_visible = (home_visible || voice_busy) && pet_buffer && pose.visible;
    const auto animated_frame = product::pet_animation_snapshot();
    const bool animation_ready = animation_visible && animated_frame.pixels;
    static bool previous_radar = false;
    const bool radar = pose.mood == character::Mood::Radar;
    if (radar != previous_radar) {
      // Erase the full previous figure before switching to/from the crop.
      lv_obj_invalidate(lv_screen_active());
      g_force_character_redraw = true;
      previous_radar = radar;
    }
    if (!menu_page) update_character_layout(setup_character && pose.visible);
    const float volume_level = g_volume_indicator.update(now, current_status.volume,
        CONFIG_LUNAREDU_SMART_SPEAKER_MAX_VOLUME);
    const float indicator_opacity = pose.volume_opacity;
    const bool indicator_visible = character_allowed && pose.visible &&
        !g_setup_visible && !holding && !transfer_page &&
        feedback == character::Feedback::None && indicator_opacity > 0;
    std::string hint;
    if (character_allowed && pose.visible && !holding && !g_setup_visible) {
      if (transfer_page) {
        if (transfer.receiving) hint = transfer.received >= transfer.total ? "正在保存…" :
            "接收歌曲 " + std::to_string(percent) + "%";
        else if (transfer.batch_open) hint = "已添加 " + std::to_string(transfer.completed) + " 首 · 接收中";
        else hint = transfer.failed ? "部分歌曲未能保存" : "已添加 " + std::to_string(transfer.completed) + " 首";
        if (transfer.failed && transfer.completed == 0 && !transfer_active) hint = "歌曲未能保存";
        const auto name = transfer.name;
        if (name != lv_label_get_text(g_play_status)) lv_label_set_text(g_play_status, name.c_str());
      } else if (transfer_active) hint = "正在接收歌曲";
      else if (g_network_feedback == character::Feedback::Success) hint = "已联网";
      else if (g_network_feedback == character::Feedback::Radar) hint = "正在连接…";
      else if (current_status.protection_active) hint = "正在保护扬声器";
      else if (error_hint) hint = speaker_error_hint(previous_error);
      else if (local_menu.mode_hold_percent > 0) hint = "切换播放模式…";
      else if (current_status.mode_sequence && now - current_status.mode_updated_ms < 2200)
        hint = playback_mode_label(current_status.playback_mode);
      else {
        auto xiaozhi = xiaozhi_status_label(true);
        if (xiaozhi != "小智待命" && xiaozhi != "小智准备中") {
          if (voice_busy || assistant.activation_pending || assistant.failed) hint = std::move(xiaozhi);
        }
      }
    }
    if (hint.empty() && home_visible && !resources.selected.empty() && !resources.pixels)
      hint = "壁纸读取失败 · 可在网页重选";
    // Volume changes get priority over transient status text so the animated
    // level bar remains visible while the user turns the encoder.
    if (pose.volume_visible && feedback == character::Feedback::None &&
        !transfer_page) hint.clear();
    const auto touch = smart_speaker_pet_touch_feedback();
    auto conversation_clip = product::PetClip::Count;
    using product::PetClip;
    switch (assistant_visual) {
      case AssistantVisualState::Speaking: conversation_clip = PetClip::Talk; break;
      case AssistantVisualState::Thinking:
      case AssistantVisualState::Listening: conversation_clip = PetClip::Think; break;
      case AssistantVisualState::Happy: conversation_clip = PetClip::Tap; break;
      case AssistantVisualState::Curious: conversation_clip = product::pet_action_named("东张西望"); break;
      case AssistantVisualState::Sleepy: conversation_clip = product::pet_action_named("哈欠连天"); break;
      case AssistantVisualState::Error:
      case AssistantVisualState::Failed: conversation_clip = product::pet_action_named("被吓一跳"); break;
      default: break;
    }
    const bool pet_interactive = animation_visible && !voice_busy && !holding;
    if (animation_visible && animated_frame.unavailable) g_pending_pet_action.store(-1);
    const auto animation = pet_animation.update(now, animation_visible, pet_interactive,
        conversation_clip, touch, animated_frame.clip, animated_frame.index,
        animated_frame.frame_count, pose, 240,
        pet_interactive && animation_ready ? g_pending_pet_action.exchange(-1) : -1, motion,
        product::pet_animation_catalog_ready(), animated_frame.unavailable, true);
    if (animation_visible && !voice_busy && now - last_reminder_poll >= 1000) {
      reminder_hint = product::product_pending_reminder_hint();
      last_reminder_poll = now;
    }
    if (hint.empty() && animation_visible && !voice_busy &&
        !pose.volume_visible && !holding) {
      if (!reminder_hint.empty()) hint = reminder_hint;
      else if (animation.caption && animation.clip == animated_frame.clip) hint = animation.caption;
    }
    if (hint != last_character_hint) {
      last_character_hint = hint;
      lv_label_set_text(g_character_hint, last_character_hint.c_str());
    }
    const bool show_indicator = indicator_visible && hint.empty();
    update_playback_indicator(show_indicator, indicator_opacity, volume_level);
    set_visible(g_character_hint, !voice_busy && !hint.empty());
    set_visible(g_track_panel, home_visible && transfer_page);
    portENTER_CRITICAL(&g_display_stats_lock);
    g_display_stats.character_visible = pose.visible;

    g_display_stats.transfer_page = transfer_page;
    g_display_stats.transfer_percent = transfer_page ? percent : 0;
    g_display_stats.volume_bar_visible = show_indicator;
    g_display_stats.pause_indicator_visible = false;
    g_display_stats.pause_indicator_percent = 0;
    g_display_stats.character_edge_contact = static_cast<uint8_t>(pose.edge_contact);
    g_display_stats.character_edge_impact = static_cast<uint8_t>(
        std::lround(std::clamp(pose.edge_impact, 0.0f, 1.0f) * 100.0f));
    g_display_stats.account_avatar_ready = account_avatar_image() != nullptr;
    g_display_stats.account_avatar_generation = account_avatar_generation();
    g_display_stats.character_mood = static_cast<uint8_t>(pose.mood);
    const uint32_t character_frame_ms = g_display_stats.character_frame_ms;
    portEXIT_CRITICAL(&g_display_stats_lock);
    const int character_top = g_setup_character ? (g_setup_cached_credentials ? 72 : 60) : kCharacterTop;
    product::pet_animation_request(animation.clip, animation.index);
    portENTER_CRITICAL(&g_display_stats_lock);
    g_display_stats.pet_animation_unavailable = animation_visible && animated_frame.unavailable;
    g_display_stats.pet_dragging = pet_interactive && touch.dragging;
    if (!animation_ready) {
      g_display_stats.pet_animation_visible = false;
      last_drawn_pet_frame = -1;
    }
    portEXIT_CRITICAL(&g_display_stats_lock);
    if (animation_visible) {
      const int frame_key = animation_ready ?
          (static_cast<int>(animated_frame.clip) << 8) | animated_frame.index : -1;
      g_home_pet.show(voice_busy ? PetSpriteRect{80, 48, 80, 80} : animation.sprite, animation_ready ? animated_frame.pixels->data() : nullptr,
                      frame_key, kHomePetIdle);
    } else {
      g_home_pet.hide();
    }
    g_home_pet.set_conversation(voice_busy, voice_busy ? xiaozhi_status_label(true).c_str() : "");
    if (pet_interactive) {
      const auto r = animation.sprite;
      // Exclude most of the transparent margin in the 80px default sprite.
      smart_speaker_set_pet_touch_target(PetTouchTarget{
          static_cast<unsigned>(r.x + r.w / 12), static_cast<unsigned>(r.y + r.h / 12),
          static_cast<unsigned>(r.w * 5 / 6), static_cast<unsigned>(r.h * 5 / 6)}.packed());
    } else {
      smart_speaker_set_pet_touch_target(0);
    }
    g_character_damage_height.store(character::kSize, std::memory_order_relaxed);
    g_character_damage_top.store(character_top, std::memory_order_relaxed);
    lunaredu::desktop::lvgl_screen_runtime_handler();
    const auto lvgl_status = lunaredu::desktop::lvgl_screen_runtime_status();
    if (measuring_boot) {
      boot_max_refresh_us = std::max(boot_max_refresh_us, lvgl_status.last_refresh_us);
      if (!g_boot.active()) {
        uint32_t cumulative = 0, p95_us = 0;
        for (size_t i = 0; i < boot_frame_histogram.size(); ++i) {
          cumulative += boot_frame_histogram[i];
          if (cumulative * 100 >= boot_frames * 95) {
            p95_us = i + 1 == boot_frame_histogram.size() ? boot_max_frame_us :
                static_cast<uint32_t>((i + 1) * 10000);
            break;
          }
        }
        portENTER_CRITICAL(&g_display_stats_lock);
        g_display_stats.boot_frame_count = boot_frames;
        g_display_stats.boot_frame_mean_us = boot_frames ? boot_total_frame_us / boot_frames : 0;
        g_display_stats.boot_frame_p95_us = p95_us;
        g_display_stats.boot_frame_max_us = boot_max_frame_us;
        g_display_stats.boot_frames_over_50ms = boot_slow_frames;
        g_display_stats.boot_max_refresh_us = boot_max_refresh_us;
        g_display_stats.boot_refresh_count = lvgl_status.refresh_count - boot_start_display.refresh_count;
        g_display_stats.boot_full_refresh_count = lvgl_status.full_refresh_count - boot_start_display.full_refresh_count;
        g_display_stats.boot_flushed_pixels = lvgl_status.flushed_pixels - boot_start_display.flushed_pixels;
        portEXIT_CRITICAL(&g_display_stats_lock);
      }
    }
    // Account for every flushed rectangle, not just the final tile. A scrolling
    // caption above the character no longer forces a full character upload.
    if (g_character_damaged.exchange(false, std::memory_order_acq_rel)) g_force_character_redraw = true;
    if (animation_ready) {
      const int frame_key = (static_cast<int>(animated_frame.clip) << 8) | animated_frame.index;
      portENTER_CRITICAL(&g_display_stats_lock);
      if (frame_key != last_drawn_pet_frame) ++g_display_stats.pet_animation_frames_drawn;
      g_display_stats.pet_animation_visible = true;
      g_display_stats.pet_animation_clip = static_cast<uint8_t>(animated_frame.clip);
      g_display_stats.pet_animation_frame = animated_frame.index;
      portEXIT_CRITICAL(&g_display_stats_lock);
      last_drawn_pet_frame = frame_key;
    }
    if (setup_character && pose.visible && !lvgl_status.flush_pending &&
        (g_force_character_redraw || now - last_character_ms >= character_frame_ms)) {
      g_force_character_redraw = !render_character_direct(pose, character_top);
      last_character_ms = now;
    }
    // Drawing consumes part of the frame budget. Sleep only until the next
    // input poll or frame, rather than adding 20 ms after every slow draw.
    uint32_t wait_ms = display_frame_wait_ms(
        static_cast<uint32_t>(esp_timer_get_time() - poll_us), kDisplayFrameMs);
    if (setup_character && pose.visible) {
      const uint32_t since_frame = static_cast<uint32_t>(esp_timer_get_time() / 1000) - last_character_ms;
      wait_ms = std::min(wait_ms, since_frame >= character::kFrameMs ? 1U : character::kFrameMs - since_frame);
    }
    vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(wait_ms)));
  }
}

} // namespace

void smart_speaker_display_set_assistant_emotion(AssistantVisualState state) {
  switch (state) {
    case AssistantVisualState::Happy:
    case AssistantVisualState::Curious:
    case AssistantVisualState::Sleepy:
    case AssistantVisualState::Error:
      g_assistant_emotion.store(static_cast<uint8_t>(state), std::memory_order_release);
      g_assistant_emotion_until_ms.store(
          static_cast<uint32_t>(esp_timer_get_time() / 1000) + 2200,
          std::memory_order_release);
      break;
    default:
      break;
  }
}

bool setup_smart_speaker_display() {
  std::string task_error;
  const bool created = lunaredu::lunaros::create_managed_task(
      {.id = "speaker_display", .owner_service_id = "device.hardware", .role = "visualization",
       .stack_bytes = kDisplayStackBytes, .priority = 2,
#if CONFIG_FREERTOS_UNICORE
       .core_affinity = 0,
#else
       .core_affinity = 1,
#endif
       .lifetime = lunaredu::lunaros::ManagedTaskLifetime::Resident,
       .memory = lunaredu::lunaros::ManagedTaskMemory::Internal},
      display_task, nullptr, nullptr, task_error);
  if (!created) ESP_LOGE(kTag, "display task create failed: %s", task_error.c_str());
  return created;
}

SmartSpeakerDisplayStats smart_speaker_display_stats() {
  portENTER_CRITICAL(&g_display_stats_lock);
  const SmartSpeakerDisplayStats stats = g_display_stats;
  portEXIT_CRITICAL(&g_display_stats_lock);
  return stats;
}

void smart_speaker_display_request_pet_action(int action) {
  if (action >= 0 && action <= product::kPetToggleSleep) g_pending_pet_action.store(action);
}

} // namespace smart_speaker
