#pragma once
#include <lvgl.h>
#include "smart_speaker_cover.h"
#include "smart_speaker_music_spectrum.h"

namespace smart_speaker {
class MusicView {
 public:
  void create(lv_obj_t *parent, const lv_font_t *font, const lv_font_t *small, bool direct = false);
  bool show(const std::string &title, const std::string &footer,
      std::shared_ptr<const cover::Image> cover, bool playing,
      const music::Bands &bands, uint32_t now, int volume);
  void hide();
  const uint16_t *direct_frame() const;
  bool needs_flush() const { return frame_dirty_; }
  void flushed() { frame_dirty_ = false; }
  uint32_t frame_wait_ms(uint32_t now) const {
    if (!animating_) return 20;
    const auto age = uint32_t(now - last_frame_);
    return age < 40 ? 40 - age : 0;
  }
 private:
  void prepare_record();
  void rotate_record();
  void update_footer(const std::string &value, uint32_t now);
  lv_obj_t *root_ = nullptr, *image_ = nullptr;
  lv_obj_t *title_ = nullptr, *footer_ = nullptr, *footer_clip_ = nullptr, *volume_label_ = nullptr;
  lv_image_dsc_t descriptor_{};
  std::shared_ptr<const cover::Image> cover_;
  uint16_t *pixels_ = nullptr;
  music::Motion motion_;
  int16_t direction_x_[64]{}, direction_y_[64]{};
  bool direct_ = false, frame_dirty_ = false, animating_ = false;
  bool visible_ = false, playing_ = false, volume_visible_ = false;
  uint32_t last_frame_ = 0, volume_changed_ = 0;
  int volume_ = 0;
  std::string footer_text_;
  uint32_t footer_started_ = 0;
  int footer_width_ = 0;
};
} // namespace smart_speaker
