#pragma once

#include <lvgl.h>
#include "smart_speaker_pet_animation.h"

namespace smart_speaker {

// LVGL owns screen composition, so moving the sprite repaints the exposed
// wallpaper and never sends an opaque tile directly to the LCD.
class HomePetView {
 public:
  static constexpr int kBufferSide = 192;
  static constexpr PetSpriteRect kConversationRect{24, 0, 192, 192};
  static constexpr int kFallbackSide = 80;
  static constexpr size_t kBufferBytes = kBufferSide * kBufferSide * 3;
  void create(lv_obj_t *parent, uint8_t *buffer);
  void show(PetSpriteRect rect, const uint16_t *pixels, int frame_key,
            const uint8_t *fallback);
  void hide();
  void set_conversation(bool active, const char *caption);

 private:
  lv_obj_t *image_ = nullptr;
  lv_obj_t *conversation_ = nullptr;
  lv_obj_t *caption_ = nullptr;
  lv_image_dsc_t source_{};
  uint8_t *buffer_ = nullptr;
  PetSpriteRect previous_{};
  int frame_key_ = -2;
};

} // namespace smart_speaker
