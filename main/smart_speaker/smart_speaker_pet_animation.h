#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "product/pet_action_catalog.h"
#include "smart_speaker_character.h"
#include "smart_speaker_sensors.h"

namespace smart_speaker {

struct PetSpriteRect {
  int x = 0, y = 0, w = 0, h = 0;
};
struct PetAnimationStep {
  product::PetClip clip = product::PetClip::Count;
  unsigned index = 0;
  PetSpriteRect sprite{};
  const char *caption = nullptr;
};

// Display-owned playback and contact reactions. The clip clock starts once its
// first frame arrives; subsequent slow reads may skip frames, never stall input.
class PetAnimation {
 public:
  // Keep the animation's working frame off the stack during LVGL rendering.
  [[gnu::noinline]] PetAnimationStep update(uint32_t now, bool visible, bool interactive,
                          product::PetClip conversation, PetTouchFeedback touch,
                          product::PetClip loaded_clip, unsigned loaded_index,
                          unsigned loaded_count, const character::Pose &pose,
                          int view_height, int action = -1, SmartSpeakerMotionInput motion = {},
                          bool catalog_ready = false, bool unavailable = false,
                          bool wallpaper_home = false) {
    using product::PetClip;
    if (!visible) {
      const bool keep_sleeping = sleep_ == PetClip::SleepStart || sleep_ == PetClip::SleepLoop;
      *this = PetAnimation{};
      if (keep_sleeping) sleep_ = PetClip::SleepLoop;
      taps_ = touch.taps;
      releases_ = touch.releases;
      shakes_ = motion.shake_sequence;
      return {};
    }
    if (unavailable) {
      manual_ = sleep_ = ambient_ = PetClip::Count;
      idle_armed_ = false;
    }

    if (!interactive || !was_interactive_) {
      reacting_ = false;
      bouncing_ = false;
      if (touch.dragging) ignore_drag_ = true;
    } else {
      if (taps_ != touch.taps) {
        reacting_ = true;
        retrigger_ = true;
        squeeze_started_ = now;
      }
      if (releases_ != touch.releases && !ignore_drag_) {
        const float vx = touch.release_sling ? -touch.release_dx * 12.0f : touch.release_vx;
        const float vy = touch.release_sling ? -touch.release_dy * 12.0f : touch.release_vy;
        bounce(now, touch.release_dx, touch.release_dy, vx, vy, wallpaper_home ? 240.0f : 26.0f);
      } else if (shakes_ != motion.shake_sequence && sleep_ == PetClip::Count &&
                 !touch.dragging && action < 0) {
        bounce(now, 0, 0, motion.shake_x * 425.0f, motion.shake_y * 425.0f);
      }
    }
    shakes_ = motion.shake_sequence;
    if (interactive && action >= 0) {
      reacting_ = bouncing_ = false;
      ambient_ = PetClip::Count;
      if (action == product::kPetToggleSleep) {
        manual_ = PetClip::Count;
        toggle_sleep();
      } else if (action < product::kPetToggleSleep) {
        sleep_ = PetClip::Count;
        manual_ = static_cast<PetClip>(action);
        retrigger_ = true;
      }
    } else if (reacting_ && sleep_ != PetClip::Count) {
      if (sleep_ != PetClip::SleepEnd) toggle_sleep();
      reacting_ = false;
    }
    taps_ = touch.taps;
    releases_ = touch.releases;
    was_interactive_ = interactive;
    if (!touch.dragging) ignore_drag_ = false;
    const bool sleeping = sleep_ == PetClip::SleepStart || sleep_ == PetClip::SleepLoop;
    const bool dragging = interactive && !sleeping && touch.dragging && !ignore_drag_;
    if (dragging) {
      reacting_ = false;
      bouncing_ = false;
      manual_ = PetClip::Count;
    }

    if (reacting_) manual_ = PetClip::Count;
    if (conversation != PetClip::Count) manual_ = PetClip::Count;
    if (!catalog_ready || !interactive || sleep_ != PetClip::Count || dragging ||
        bouncing_ || reacting_ || manual_ != PetClip::Count) {
      ambient_ = PetClip::Count;
      idle_armed_ = false;
    } else if (!idle_armed_ && ambient_ == PetClip::Count) {
      random_ = random_ * 1664525U + 1013904223U + now;
      idle_started_ = now;
      idle_delay_ = 35000 + random_ % 25000;
      idle_armed_ = true;
    } else if (idle_armed_ && now - idle_started_ >= idle_delay_) {
      constexpr PetClip choices[] = {
          product::pet_action_named("东张西望"), product::pet_action_named("小幅度原地360度旋转展示"),
          product::pet_action_named("悠闲哼歌"), product::pet_action_named("超大伸懒腰"),
          product::pet_action_named("碎碎念-发呆碎碎念"),
      };
      constexpr unsigned choice_count = sizeof(choices) / sizeof(choices[0]);
      random_ = random_ * 1664525U + 1013904223U;
      const unsigned next = (ambient_choice_ + 1 + random_ % (choice_count - 1)) % choice_count;
      ambient_choice_ = next;
      ambient_ = choices[next];
      idle_armed_ = false;
    }
    const PetClip wanted = conversation != PetClip::Count ? conversation :
        sleep_ != PetClip::Count ? sleep_ : dragging ? PetClip::Drag :
        reacting_ ? PetClip::Tap : manual_ != PetClip::Count ? manual_ :
        ambient_ != PetClip::Count ? ambient_ : PetClip::Idle;
    const bool manual = conversation == PetClip::Count && wanted == manual_;
    if (wanted != clip_ || retrigger_) {
      clip_ = wanted;
      playing_ = false;
      retrigger_ = false;
    }
    if (!playing_ && loaded_clip == clip_ && loaded_index == 0 && loaded_count) {
      started_ = now;
      frame_count_ = loaded_count;
      playing_ = true;
    }

    unsigned index = 0;
    if (playing_) {
      const unsigned count = frame_count_;
      if (count != 0) {
        const uint32_t age = now - started_;
        const uint64_t next = uint64_t(age) * product::kPetSpriteFps / 1000U;
        const bool sleep_transition = conversation == PetClip::Count && wanted == sleep_ &&
            (sleep_ == PetClip::SleepStart || sleep_ == PetClip::SleepEnd);
        if ((clip_ == PetClip::Tap || manual || sleep_transition || clip_ == ambient_) && next >= count) {
          if (sleep_transition)
            sleep_ = clip_ == PetClip::SleepStart ? PetClip::SleepLoop : PetClip::Count;
          reacting_ = false;
          playing_ = false;
          if (manual) manual_ = PetClip::Count;
          if (clip_ == ambient_) ambient_ = PetClip::Count;
          clip_ = sleep_ == PetClip::Count ? PetClip::Idle : sleep_;
        } else {
          index = static_cast<unsigned>(next % count);
        }
      }
    }

    float dx = sleeping ? 0 : pose.x, dy = sleeping ? 0 : pose.y;
    float sx = 1.0f, sy = 1.0f;
    const int width = wallpaper_home ? 240 : character::kSize;
    const int side = wallpaper_home ? 80 : view_height < character::kSize ? 106 : product::kPetSpriteSide;
    const float anchor_x = wallpaper_home ? 200.0f : width * .5f;
    const float anchor_y = wallpaper_home ? 185.0f : view_height * .5f;
    if (dragging) {
      dx += touch.dx;
      dy += touch.dy;
    } else if (bouncing_) {
      const bool launched = release_vx_ || release_vy_;
      const float duration = launched ? 680.0f : 460.0f;
      const float t = float(now - bounce_started_) / duration;
      if (t >= 1.0f) bouncing_ = false;
      else {
        const float rest = (1.0f - t) * (1.0f - t);
        const float spring = rest * std::cos(t * 9.5f);
        const float travel = rest * std::sin(t * 4.4f) * duration / 4400.0f;
        dx += release_x_ * spring + release_vx_ * travel;
        dy += release_y_ * spring + release_vy_ * travel;
        const float impact = rest * std::max(0.0f, std::sin(t * 12.0f)) * .12f;
        sx += impact;
        sy -= impact;
        if (launched) {
          dx = reflect(dx + anchor_x - width * .5f, (width - side) * .5f, sx, sy)
              - anchor_x + width * .5f;
          dy = reflect(dy + anchor_y - view_height * .5f, (view_height - side) * .5f, sy, sx)
              - anchor_y + view_height * .5f;
        }
      }
    }
    if (reacting_ && now - squeeze_started_ < 240) {
      const float t = float(now - squeeze_started_) / 240.0f;
      const float pulse = (1.0f - t) * .15f;
      sx += pulse * .45f;
      sy -= pulse;
    }
    if (!sleeping && !dragging && !bouncing_ && pose.edge_impact > 0) {
      const float pulse = std::clamp(pose.edge_impact, 0.0f, 1.0f) * .08f;
      if (pose.edge_contact == character::EdgeContact::Left ||
          pose.edge_contact == character::EdgeContact::Right) sx -= pulse;
      else sy -= pulse;
    }
    const int w = static_cast<int>(std::lround(side * sx));
    const int h = static_cast<int>(std::lround(side * sy));
    const int x = std::clamp(static_cast<int>(std::lround(anchor_x - w * .5f + dx)),
                             0, width - w);
    const int y = std::clamp(static_cast<int>(std::lround(anchor_y - h * .5f + dy)),
                             0, view_height - h);
    const char *caption = nullptr;
    if (playing_ && clip_ == ambient_ && now - started_ < 3000 &&
        ambient_ == product::pet_action_named("碎碎念-发呆碎碎念")) caption = "让我发会儿呆…";
    return {clip_, index, {x, y, w, h}, caption};
  }

 private:
  void bounce(uint32_t now, float dx, float dy, float vx, float vy, float offset_limit = 26.0f) {
    bouncing_ = true;
    bounce_started_ = now;
    release_x_ = std::clamp(dx, -offset_limit, offset_limit);
    release_y_ = std::clamp(dy, -offset_limit, offset_limit);
    release_vx_ = std::clamp(vx, -550.0f, 550.0f);
    release_vy_ = std::clamp(vy, -550.0f, 550.0f);
  }
  static float reflect(float position, float limit, float &axis, float &cross) {
    const float excess = std::abs(position) - limit;
    if (excess <= 0) return position;
    const float squash = std::min(.15f, excess * .012f);
    axis -= squash;
    cross += squash * .35f;
    return std::copysign(1.0f, position) * std::clamp(limit - excess * .5f, -limit, limit);
  }
  void toggle_sleep() {
    if (sleep_ == PetClip::SleepStart) sleep_ = PetClip::Count;
    else if (sleep_ == PetClip::SleepLoop) sleep_ = PetClip::SleepEnd;
    else sleep_ = PetClip::SleepStart;
  }
  using PetClip = product::PetClip;
  product::PetClip clip_ = product::PetClip::Count;
  PetClip sleep_ = PetClip::Count, manual_ = PetClip::Count;
  PetClip ambient_ = PetClip::Count;
  uint32_t taps_ = 0, releases_ = 0, started_ = 0, bounce_started_ = 0, squeeze_started_ = 0;
  float release_x_ = 0, release_y_ = 0;
  float release_vx_ = 0, release_vy_ = 0;
  uint32_t shakes_ = 0, idle_started_ = 0, idle_delay_ = 0, random_ = 0;
  unsigned ambient_choice_ = 0;
  bool idle_armed_ = false;
  unsigned frame_count_ = 0;
  bool was_interactive_ = false, ignore_drag_ = false, playing_ = false;
  bool reacting_ = false, retrigger_ = false, bouncing_ = false;
};

// Clear every tile, including the previous sprite's footprint. The existing
// frame differ then transfers only changed pixels; scale and position can
// change at the display cadence even if the decoded frame is unchanged.
inline void render_pet_sprite_tile(const uint16_t *pixels, PetSpriteRect r,
                                   int top, int rows, uint16_t *target) {
  constexpr int width = character::kSize;
  constexpr int side = product::kPetSpriteSide;
  std::fill_n(target, rows * width, uint16_t{0});
  if (r.w <= 0 || r.h <= 0) return;
  const int x0 = std::max(0, r.x), x1 = std::min(width, r.x + r.w);
  const int y0 = std::max(top, r.y), y1 = std::min(top + rows, r.y + r.h);
  if (x0 >= x1 || y0 >= y1) return;
  uint8_t source_x[width];
  for (int x = x0; x < x1; ++x) source_x[x] = (x - r.x) * side / r.w;
  for (int y = y0; y < y1; ++y) {
    auto *row = target + (y - top) * width;
    const auto *source = pixels + (y - r.y) * side / r.h * side;
    for (int x = x0; x < x1; ++x) row[x] = source[source_x[x]];
  }
}

} // namespace smart_speaker
