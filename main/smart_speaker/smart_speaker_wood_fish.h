#pragma once
#include <cstdint>
#include <array>
#include <lvgl.h>

namespace smart_speaker {
// Display-owner-only view. ProductState owns the count; this owns only visual
// timing. No timers, files, audio handles or per-hit objects are allocated.
class WoodFishView {
 public:
  void create(lv_obj_t *parent, const lv_font_t *heading, const lv_font_t *small);
  void show(uint32_t count, uint32_t now_ms);
  void hide();
 private:
  static void draw(lv_event_t *event);
  struct FloatingGain {
    lv_obj_t *label = nullptr;
    uint32_t started = 0;
    bool active = false;
  };
  std::array<FloatingGain, 8> gains_{};
  size_t next_gain_ = 0;
  lv_obj_t *root_ = nullptr, *art_ = nullptr, *count_label_ = nullptr, *body_ = nullptr;
  uint32_t count_ = 0, started_ = 0, age_ = 700;
  bool visible_ = false;
};
}
