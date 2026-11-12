#include "smart_speaker_music_view.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace smart_speaker {
namespace {
constexpr int side = 126, radius = 62;
constexpr size_t plane = side * side;
constexpr int frame_side = 176;
constexpr size_t frame_plane = frame_side * frame_side;
constexpr float pi = 3.14159265358979323846f;
uint16_t rgb(int r, int g, int b) { return uint16_t((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }
void text(lv_obj_t *label, const std::string &value) {
  if (value != lv_label_get_text(label)) lv_label_set_text(label, value.c_str());
}
lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, int y) {
  auto *o = lv_label_create(parent);
  lv_obj_set_pos(o, 12, y); lv_obj_set_size(o, 216, 22);
  lv_obj_set_style_text_font(o, font, 0);
  lv_obj_set_style_text_color(o, lv_color_hex(0xE8E6DF), 0);
  lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
  return o;
}
}
void MusicView::create(lv_obj_t *parent, const lv_font_t *font, const lv_font_t *small, bool direct) {
  direct_ = direct;
  root_ = lv_obj_create(parent); lv_obj_remove_style_all(root_);
  lv_obj_set_size(root_, 240, 240);
  lv_obj_remove_flag(root_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_style_bg_color(root_, lv_color_hex(0), 0);
  lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
  // One opaque bitmap avoids allocating 64 line draw tasks for every partial
  // LVGL strip. Both the rotating record and spectrum live in PSRAM.
  image_ = lv_image_create(root_); lv_obj_set_pos(image_, 32, 32);
  for (int i = 0; i < 64; ++i) {
    const float a = i * 2 * pi / 64 - pi / 2;
    direction_x_[i] = std::lround(std::cos(a) * 16384);
    direction_y_[i] = std::lround(std::sin(a) * 16384);
  }
  lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
  title_ = label(root_, font, 8);
  lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT); // Song title stays still.
  footer_clip_ = lv_obj_create(root_); lv_obj_remove_style_all(footer_clip_);
  lv_obj_set_pos(footer_clip_, 12, 212); lv_obj_set_size(footer_clip_, 216, 28);
  lv_obj_remove_flag(footer_clip_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  footer_ = label(footer_clip_, font, (28 - lv_font_get_line_height(font)) / 2);
  lv_obj_set_size(footer_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  volume_label_ = label(root_, small, 10);
  lv_obj_set_pos(volume_label_, 189, 10); lv_obj_set_size(volume_label_, 39, 18);
  lv_obj_set_style_text_color(volume_label_, lv_color_hex(0xF4C95D), 0);
  lv_label_set_long_mode(volume_label_, LV_LABEL_LONG_CLIP);
  lv_obj_add_flag(volume_label_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}
void MusicView::update_footer(const std::string &value, uint32_t now) {
  if (value != footer_text_) {
    footer_text_ = value;
    auto line = value;
    // Translations or duplicate LRC timestamps may contain newlines. Keep one
    // fixed baseline; do not let LVGL infer a vertical marquee from text height.
    std::replace(line.begin(), line.end(), '\n', ' ');
    std::replace(line.begin(), line.end(), '\r', ' ');
    text(footer_, line);
    lv_obj_update_layout(footer_);
    footer_width_ = lv_obj_get_width(footer_);
    footer_started_ = now;
  }
  int x = (216 - footer_width_) / 2;
  if (footer_width_ > 216) {
    constexpr uint32_t hold = 1200, pixels_per_second = 24;
    const int distance = footer_width_ - 216;
    const uint32_t travel = (uint32_t(distance) * 1000 + pixels_per_second - 1) / pixels_per_second;
    const uint32_t phase = uint32_t(now - footer_started_) % (2 * (hold + travel));
    if (phase < hold) x = 0;
    else if (phase < hold + travel) x = -int((phase - hold) * pixels_per_second / 1000);
    else if (phase < 2 * hold + travel) x = -distance;
    else x = -distance + int((phase - 2 * hold - travel) * pixels_per_second / 1000);
  }
  // Integer pixel changes only. The fixed clipping region never moves in Y.
  if (lv_obj_get_x(footer_) != x) lv_obj_set_x(footer_, x);
}
void MusicView::prepare_record() {
  if (!pixels_) return;
  for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
    const float dx = x - 62.5f, dy = y - 62.5f;
    const float r = std::sqrt(dx * dx + dy * dy);
    uint16_t color = 0;
    if (r <= radius) {
      if (cover_ && cover_->pixels) {
        color = cover_->pixels.get()[(y * cover::kSide / side) * cover::kSide + x * cover::kSide / side];
      } else if (r < 4) color = 0;
      else if (r < 23) {
        color = rgb(205, 169, 98);
        // Two minimal grooves on the label make the slow rotation legible.
        if ((std::abs(dy + 11) < 1 || std::abs(dy - 12) < 1) && std::abs(dx) < 13)
          color = rgb(84, 67, 38);
      } else {
        const float azimuth = std::atan2(dy, dx);
        const int sheen = static_cast<int>(22 * std::pow(std::abs(std::cos(azimuth + .5f)), 12));
        const int base = (int(r * 2) % 4 == 0 ? 34 : 23) + sheen;
        color = rgb(base, base + 1, base + 2);
      }
    }
    pixels_[y * side + x] = color;
  }
}
void MusicView::rotate_record() {
  if (!pixels_) return;
  auto *out = pixels_ + plane;
  std::memset(out, 0, frame_plane * sizeof(uint16_t));
  const int c = std::lround(std::cos(motion_.angle()) * 16384);
  const int s = std::lround(std::sin(motion_.angle()) * 16384);
  for (int y = 0; y < side; ++y) {
    const int dy = 2 * y - 125;
    int sx = (-125 * c + dy * s + 125 * 16384) / 2 + 8192;
    int sy = (125 * s + dy * c + 125 * 16384) / 2 + 8192;
    for (int x = 0; x < side; ++x, sx += c, sy -= s) {
      const int dx = 2 * x - 125;
      const int ix = sx >> 14, iy = sy >> 14;
      if (dx * dx + dy * dy <= 4 * radius * radius &&
          ix >= 0 && ix < side && iy >= 0 && iy < side)
        out[(y + 25) * frame_side + x + 25] = pixels_[iy * side + ix];
    }
  }
  const uint16_t color = playing_ ? rgb(244, 201, 93) : rgb(104, 86, 49);
  for (int i = 0; i < 64; ++i) {
    const float position = (i < 32 ? i : 63 - i) * 15.f / 31;
    const int band = static_cast<int>(position);
    const float value = motion_.levels()[band] * (1 - (position - band)) +
        motion_.levels()[std::min(band + 1, 15)] * (position - band);
    const int length = 2 + std::lround(value * 10);
    // Half-pixel radial steps keep diagonal two-pixel strokes connected.
    for (int r = 146; r <= 2 * (73 + length); ++r) {
      const int x = 88 + (r * direction_x_[i] + 16384) / 32768;
      const int y = 88 + (r * direction_y_[i] + 16384) / 32768;
      for (int yy = y; yy <= y + 1; ++yy) for (int xx = x; xx <= x + 1; ++xx)
        if (xx >= 0 && xx < frame_side && yy >= 0 && yy < frame_side)
          out[yy * frame_side + xx] = color;
    }
  }
  frame_dirty_ = true;
  if (!direct_) lv_obj_invalidate(image_);
}
bool MusicView::show(const std::string &title, const std::string &footer,
    std::shared_ptr<const cover::Image> artwork, bool playing,
    const music::Bands &bands, uint32_t now, int volume) {
  if (!root_) return false;
  bool rendered = false;
  const bool opening = !visible_;
  bool changed = opening || artwork != cover_;
  if (opening) {
#ifdef ESP_PLATFORM
    pixels_ = static_cast<uint16_t *>(heap_caps_malloc((plane + frame_plane) * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    pixels_ = static_cast<uint16_t *>(std::malloc((plane + frame_plane) * sizeof(uint16_t)));
#endif
    motion_ = {};
    footer_text_.clear();
    last_frame_ = now - 40;
    volume_ = volume;
    volume_visible_ = false;
    lv_obj_add_flag(volume_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(title_, 216);
    if (pixels_) {
      descriptor_ = {};
      descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
      descriptor_.header.cf = LV_COLOR_FORMAT_RGB565;
      descriptor_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
      descriptor_.header.w = descriptor_.header.h = frame_side;
      descriptor_.header.stride = frame_side * 2;
      descriptor_.data_size = frame_plane * 2;
      descriptor_.data = reinterpret_cast<const uint8_t *>(pixels_ + plane);
      lv_image_set_src(image_, &descriptor_);
      if (!direct_) lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
    }
    visible_ = true;
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
  }
  if (changed) { cover_ = std::move(artwork); prepare_record(); }
  text(title_, title);
  update_footer(pixels_ ? footer : "显示内存不足", now);
  if (volume != volume_) { volume_ = volume; volume_changed_ = now; volume_visible_ = true; }
  if (volume_visible_) {
    if (uint32_t(now - volume_changed_) < 1400) {
      char value[16]; std::snprintf(value, sizeof(value), "%d%%", volume_);
      text(volume_label_, value);
      if (lv_obj_has_flag(volume_label_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(volume_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(title_, 172);
      }
    } else {
      volume_visible_ = false; lv_obj_add_flag(volume_label_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_width(title_, 216);
    }
  }
  if (uint32_t(now - last_frame_) >= 40 || changed || playing_ != playing) {
    last_frame_ = now;
    changed |= motion_.update(now, playing, bands);
    changed |= playing_ != playing;
    playing_ = playing;
    animating_ = playing || changed;
    if (changed) { rotate_record(); rendered = true; }
  }
  return rendered;
}
const uint16_t *MusicView::direct_frame() const {
  return direct_ && visible_ && pixels_ ? pixels_ + plane : nullptr;
}
void MusicView::hide() {
  if (!root_ || !visible_) return;
  visible_ = false;
  frame_dirty_ = animating_ = false;
  lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
  lv_image_set_src(image_, nullptr);
  lv_image_cache_drop(&descriptor_);
  std::free(pixels_); pixels_ = nullptr;
  cover_.reset();
}
} // namespace smart_speaker
