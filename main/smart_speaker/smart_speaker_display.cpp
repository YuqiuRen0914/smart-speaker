#include "smart_speaker_display.h"
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
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

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
#include "smart_speaker_xiaozhi.h"
#include "lunaredu_lunaros/persistence/identity.h"
#include "lunaredu_device_sdcard_tfcard_store.h"
#include "lunaredu_lunaros/device_link/device_link.h"

namespace smart_speaker {
namespace {

constexpr const char *kTag = "speaker_display";
const lv_font_t *g_body_font = nullptr;
const lv_font_t *g_heading_font = nullptr;
constexpr uint32_t kDisplayFrameMs = 20;
lv_obj_t *g_clock = nullptr;
lv_obj_t *g_play_status = nullptr;
lv_obj_t *g_track_panel = nullptr;
TrackCaption g_track_caption;
lv_obj_t *g_lyrics_panel = nullptr;
lv_obj_t *g_lyric_label = nullptr;
bool g_lyrics_layout = false;
lv_obj_t *g_volume = nullptr;
float g_indicator_morph = 0, g_indicator_level = 0, g_indicator_opacity = 0;
PauseIndicator g_pause_indicator;
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
  const auto strokes = indicator_strokes(g_indicator_morph, g_indicator_level);
  for (int pass=0; pass<2; ++pass) {
    for (const auto &stroke : strokes) {
      if (pass == 1 && stroke.fill <= 0) continue;
      lv_draw_line_dsc_t line;
      lv_draw_line_dsc_init(&line);
      line.width = 4;
      line.round_start = line.round_end = 1;
      line.opa = static_cast<lv_opa_t>(g_indicator_opacity*255);
      const int cx = bounds.x1+58, cy = bounds.y1+12;
      line.p1 = {cx+static_cast<int32_t>(std::lround(stroke.x1)), cy+static_cast<int32_t>(std::lround(stroke.y1))};
      line.p2 = {cx+static_cast<int32_t>(std::lround(stroke.x2)), cy+static_cast<int32_t>(std::lround(stroke.y2))};
      line.color = lv_color_hex(0x242424);
      if (pass == 1) {
        line.p2 = {cx+static_cast<int32_t>(std::lround(stroke.x1+(stroke.x2-stroke.x1)*stroke.fill)),
                   cy+static_cast<int32_t>(std::lround(stroke.y1+(stroke.y2-stroke.y1)*stroke.fill))};
        line.color = lv_color_hex(0xF4C95D);
      }
      lv_draw_line(layer, &line);
    }
  }
}

void make_ui() {
  lv_obj_t *screen = lv_screen_active();
  style_plain_object(screen);
  lv_obj_set_style_text_font(screen, g_body_font, 0);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(0xE8E6DF), 0);

  g_clock = lv_label_create(screen);
  lv_label_set_text(g_clock, "--:--");
  lv_obj_set_style_text_font(g_clock, g_heading_font, 0);
  lv_obj_set_style_text_color(g_clock, lv_color_hex(0xE8E6DF), 0);
  lv_obj_set_pos(g_clock, 12, 16);

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
  g_lyrics_panel = lv_obj_create(screen);
  style_plain_object(g_lyrics_panel);
  lv_obj_set_style_radius(g_lyrics_panel, 0, 0);
  lv_obj_set_style_bg_color(g_lyrics_panel, lv_color_hex(0x000000), 0);
  lv_obj_set_pos(g_lyrics_panel, 12, 184);
  lv_obj_set_size(g_lyrics_panel, 216, 42);
  lv_obj_add_flag(g_lyrics_panel, LV_OBJ_FLAG_HIDDEN);
  g_lyric_label = lv_label_create(g_lyrics_panel);
  lv_obj_set_size(g_lyric_label, 216, 42);
  lv_label_set_long_mode(g_lyric_label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(g_lyric_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(g_lyric_label, 2, 0);
  lv_obj_set_style_text_color(g_lyric_label, lv_color_hex(0xF1EFE7), 0);
  lv_label_set_text(g_lyric_label, "");
  g_volume = lv_obj_create(screen);
  lv_obj_remove_style_all(g_volume);
  style_plain_object(g_volume);
  lv_obj_remove_flag(g_volume, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(g_volume, 62, 212);
  lv_obj_set_size(g_volume, 116, 24);
  lv_obj_add_event_cb(g_volume, draw_playback_indicator, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_flag(g_volume, LV_OBJ_FLAG_HIDDEN);

  g_character_hint = lv_label_create(screen);
  lv_obj_set_width(g_character_hint, 216);
  lv_obj_set_pos(g_character_hint, 12, 214);
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

void update_clock(char (&previous)[6]) {
  char text[6] = "--:--";
  if (lunaredu::lunaros::network_snapshot().clock == lunaredu::lunaros::NetworkClockState::Valid) {
    const std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_r(&now, &local);
    std::strftime(text, sizeof(text), "%H:%M", &local);
  }
  if (std::strncmp(previous, text, sizeof(previous)) != 0) {
    lv_label_set_text(g_clock, text);
    std::memcpy(previous, text, sizeof(previous));
  }
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
lv_obj_t *g_hold_hint = nullptr;
bool g_local_overlay = false;
lv_obj_t *g_menu_hint = nullptr;
lv_obj_t *g_binding_code = nullptr;
lv_obj_t *g_binding_info = nullptr;
lv_obj_t *g_avatar_frame = nullptr;
lv_obj_t *g_avatar_view = nullptr;
lv_obj_t *g_avatar_placeholder = nullptr;
lv_obj_t *g_info_labels[3] = {};
lv_obj_t *g_info_values[3] = {};
void set_label_text(lv_obj_t *label, const std::string &text) {
  if (text != lv_label_get_text(label)) lv_label_set_text(label, text.c_str());
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
    auto hint = lv_label_create(g_menu_panel);
    g_menu_hint = hint;
    lv_label_set_text(hint, "");
    lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(hint, 16, 212);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8A8984), 0);
    lv_obj_set_style_text_color(g_menu_text, lv_color_hex(0xE8E6DF), 0);
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
    g_hold_hint = lv_label_create(lv_screen_active());
    lv_obj_set_style_bg_color(g_hold_hint, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_hold_hint, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(g_hold_hint, lv_color_hex(0xF4C95D), 0);
    lv_obj_align(g_hold_hint, LV_ALIGN_BOTTOM_MID, 0, -4);
  }
  const auto menu = local_menu_snapshot();
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
  if (mode == previous_mode && menu.selected == previous_selection &&
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

void update_playback_indicator(bool visible, float morph, float opacity, float level) {
  const bool changed = g_indicator_morph != morph || g_indicator_opacity != opacity ||
      g_indicator_level != level;
  g_indicator_morph = morph;
  g_indicator_opacity = opacity;
  g_indicator_level = level;
  set_visible(g_volume, visible);
  if (visible && changed) {
    // One small fixed damage rectangle, without per-stroke movement or
    // opacity layers. Stable endpoints do not invalidate the footer.
    lv_obj_invalidate(g_volume);
  }
}

bool render_character_direct(const character::Pose &pose, int screen_top, bool compact = false) {
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
  const int bottom = compact ? 136 : character::kSize - inset;
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
  make_ui();
  update_setup_screen();
  uint32_t last_status_ms = 0, last_clock_ms = 0, last_status_activity = 0;
  char previous_clock[6] = {};
  RuntimeStatus current_status = status_snapshot();
  uint32_t last_character_ms = 0;
  std::string previous_error;
  uint32_t error_seen_ms = 0;
  uint32_t error_generation = 0;
  bool previous_transfer_page = false;
  for (;;) {
    const uint32_t poll_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    const uint32_t poll_activity = visual_activity_snapshot();
    if (poll_ms - last_status_ms >= 80 || poll_activity != last_status_activity) {
      last_status_ms = poll_ms;
      last_status_activity = poll_activity;
      current_status = status_snapshot();
      update_setup_screen();
      update_status(current_status);
    }
    if (poll_ms - last_clock_ms >= 1000) {
      last_clock_ms = poll_ms;
      update_clock(previous_clock);
    }
    update_local_menu();
    const auto local_menu = local_menu_snapshot();
    const bool menu_page = local_menu.open || local_menu.binding || local_menu.information || local_menu.network || local_menu.library;
    const bool holding = !menu_page && !g_setup_visible && local_menu.hold_percent > 0;
    const bool character_allowed = !menu_page && (!g_setup_visible || g_setup_character);
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
    static bool previous_radar = false;
    const bool radar = pose.mood == character::Mood::Radar;
    if (radar != previous_radar) {
      // Erase the full previous figure before switching to/from the crop.
      lv_obj_invalidate(lv_screen_active());
      g_force_character_redraw = true;
      previous_radar = radar;
    }
    if (!g_setup_visible && !menu_page)
      update_character_layout(pose.visible);
    const bool stopped = show_pause_for_volume(current_status.volume, current_status.playing,
        current_status.paused, current_status.current_index >= 0);
    const float pause_morph = g_pause_indicator.update(now, stopped);
    const float volume_level = g_pause_indicator.volume_level(current_status.volume,
        CONFIG_LUNAREDU_SMART_SPEAKER_MAX_VOLUME);
    const float indicator_opacity = std::max({pose.volume_opacity,
        stopped ? 1.0f : pause_morph, g_pause_indicator.resume_opacity(now)});
    const bool indicator_visible = character_allowed && pose.visible &&
        !g_setup_visible && !holding && !transfer_page &&
        feedback == character::Feedback::None && indicator_opacity > 0;
    std::string hint;
    if (pose.visible && !holding && !g_setup_visible) {
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
        const std::string xiaozhi = xiaozhi_status_label();
        if (xiaozhi != "小智待命" && xiaozhi != "小智准备中") hint = xiaozhi;
      }
    }
    // Volume changes get priority over transient status text so the animated
    // level bar remains visible while the user turns the encoder.
    if (pose.volume_visible && feedback == character::Feedback::None &&
        !transfer_page) hint.clear();
    // Keep the compact layout for the entire song (including instrumental
    // gaps). Only the lyric band changes at sentence boundaries.
    const bool compact = character_allowed && pose.visible && !g_setup_visible &&
        !holding && !transfer_page && feedback == character::Feedback::None &&
        current_status.has_lyrics && (current_status.playing || current_status.paused);
    if (compact != g_lyrics_layout) {
      g_lyrics_layout = compact;
      lv_obj_invalidate(lv_screen_active());
      lv_timer_ready(lv_display_get_refr_timer(lv_display_get_default()));
      g_force_character_redraw = true;
    }
    // Hints and lyrics share the footer with the playback indicator.
    const bool show_indicator = indicator_visible && hint.empty();
    update_playback_indicator(show_indicator, pause_morph, indicator_opacity, volume_level);
    const bool show_lyric = compact && hint.empty() && !show_indicator && !current_status.lyric_line.empty();
    const bool lyric_was_visible = !lv_obj_has_flag(g_lyrics_panel, LV_OBJ_FLAG_HIDDEN);
    set_visible(g_lyrics_panel, show_lyric);
    if (show_lyric && (!lyric_was_visible || current_status.lyric_line != lv_label_get_text(g_lyric_label))) {
      lv_anim_delete(g_lyric_label, nullptr);
      lv_label_set_text(g_lyric_label, current_status.lyric_line.c_str());
      lv_anim_t slide;
      lv_anim_init(&slide);
      lv_anim_set_var(&slide, g_lyric_label);
      lv_anim_set_values(&slide, 0, 255);
      lv_anim_set_duration(&slide, 180);
      lv_anim_set_exec_cb(&slide, [](void *object, int32_t value) {
        auto *label = static_cast<lv_obj_t *>(object);
        lv_obj_set_y(label, (255-value)*4/255);
        lv_obj_set_style_opa(label, static_cast<lv_opa_t>(value), 0);
      });
      lv_anim_start(&slide);
    }
    if (compact) character::fit_lyrics_layout(pose);
    set_visible(g_character_hint, !hint.empty());
    set_label_text(g_character_hint, hint);
    set_visible(g_track_panel, pose.visible && !holding && !g_setup_visible);
    portENTER_CRITICAL(&g_display_stats_lock);
    g_display_stats.character_visible = pose.visible;
    g_display_stats.lyrics_visible = show_lyric;
    g_display_stats.lyrics_layout = compact;
    g_display_stats.transfer_page = transfer_page;
    g_display_stats.transfer_percent = transfer_page ? percent : 0;
    g_display_stats.volume_bar_visible = show_indicator && pause_morph < 1;
    g_display_stats.pause_indicator_visible = show_indicator && pause_morph > 0;
    g_display_stats.pause_indicator_percent = static_cast<uint8_t>(std::lround(pause_morph * 100));
    g_display_stats.character_edge_contact = static_cast<uint8_t>(pose.edge_contact);
    g_display_stats.character_edge_impact = static_cast<uint8_t>(
        std::lround(std::clamp(pose.edge_impact, 0.0f, 1.0f) * 100.0f));
    g_display_stats.account_avatar_ready = account_avatar_image() != nullptr;
    g_display_stats.account_avatar_generation = account_avatar_generation();
    g_display_stats.character_mood = static_cast<uint8_t>(pose.mood);
    const uint32_t character_frame_ms = g_display_stats.character_frame_ms;
    portEXIT_CRITICAL(&g_display_stats_lock);
    const int character_top = g_setup_character ? (g_setup_cached_credentials ? 72 : 60) : compact ? 44 : kCharacterTop;
    g_character_damage_height.store(compact ? 136 : character::kSize, std::memory_order_relaxed);
    g_character_damage_top.store(character_top, std::memory_order_relaxed);
    lunaredu::desktop::lvgl_screen_runtime_handler();
    const auto lvgl_status = lunaredu::desktop::lvgl_screen_runtime_status();
    // Account for every flushed rectangle, not just the final tile. A scrolling
    // caption above the character no longer forces a full character upload.
    if (g_character_damaged.exchange(false, std::memory_order_acq_rel)) g_force_character_redraw = true;
    // Radar uses a 96x96 viewport below credentials; failures use full Pebble.
    if (character_allowed && !lvgl_status.flush_pending) {
      if (pose.visible) {
        if (g_force_character_redraw || now - last_character_ms >= character_frame_ms) {
          g_force_character_redraw = !render_character_direct(pose, character_top, compact);
          last_character_ms = now;
        }
      }
    }
    // Drawing consumes part of the frame budget. Sleep only until the next
    // input poll or frame, rather than adding 20 ms after every slow draw.
    uint32_t wait_ms = kDisplayFrameMs;
    if (character_allowed && pose.visible) {
      const uint32_t since_frame = static_cast<uint32_t>(esp_timer_get_time() / 1000) - last_character_ms;
      wait_ms = std::min(wait_ms, since_frame >= character::kFrameMs ? 1U : character::kFrameMs - since_frame);
    }
    vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(wait_ms)));
  }
}

} // namespace

bool setup_smart_speaker_display() {
  std::string task_error;
  const bool created = lunaredu::lunaros::create_managed_task(
      {.id = "speaker_display", .owner_service_id = "device.hardware", .role = "visualization",
       .stack_bytes = 8192, .priority = 2,
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

} // namespace smart_speaker
