#include "smart_speaker_home_pet.h"

#include <cstring>
#include "src/misc/cache/instance/lv_image_cache.h"

namespace smart_speaker {

void HomePetView::create(lv_obj_t *parent, uint8_t *buffer) {
  buffer_ = buffer;
  source_.header.magic = LV_IMAGE_HEADER_MAGIC;
  source_.header.cf = LV_COLOR_FORMAT_RGB565A8;
  source_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
  source_.header.w = source_.header.h = kBufferSide;
  source_.header.stride = kBufferSide * 2;
  source_.data_size = kBufferBytes;
  source_.data = buffer_;
  conversation_ = lv_obj_create(parent);
  lv_obj_remove_style_all(conversation_);
  lv_obj_remove_flag(conversation_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_size(conversation_, 240, 240);
  lv_obj_set_style_bg_color(conversation_, lv_color_hex(0x10151E), 0);
  lv_obj_set_style_bg_opa(conversation_, LV_OPA_COVER, 0);
  caption_ = lv_label_create(conversation_);
  lv_obj_set_size(caption_, 208, 32);
  lv_obj_set_pos(caption_, 16, 196);
  lv_label_set_long_mode(caption_, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(caption_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(caption_, lv_color_hex(0xE8E6DF), 0);
  lv_obj_add_flag(conversation_, LV_OBJ_FLAG_HIDDEN);
  if (!buffer_) return;
  image_ = lv_image_create(parent);
  lv_image_set_src(image_, &source_);
  hide();
}

void HomePetView::show(PetSpriteRect rect, const uint16_t *pixels, int frame_key,
                       const uint8_t *fallback) {
  if (!image_ || rect.w <= 0 || rect.h <= 0 ||
      rect.w > kBufferSide || rect.h > kBufferSide) return;
  const bool resized = source_.header.w != rect.w || source_.header.h != rect.h;
  if (frame_key_ != frame_key || resized) {
    const size_t plane = rect.w * rect.h;
    auto *rgb = reinterpret_cast<uint16_t *>(buffer_);
    auto *alpha = buffer_ + 2 * plane;
    std::memset(alpha, 0, plane);
    const int side = pixels ? product::kPetSpriteSide : kFallbackSide;
    for (int y = 0; y < rect.h; ++y) {
      for (int x = 0; x < rect.w; ++x) {
        const int from = (y * side / rect.h) * side + x * side / rect.w;
        const int to = y * rect.w + x;
        if (pixels) {
          // v1/v2 SD packs have black mattes and no alpha plane. Keep them
          // usable without regenerating or reinstalling 114 clips.
          rgb[to] = pixels[from];
          alpha[to] = pixels[from] ? 255 : 0;
        } else {
          rgb[to] = uint16_t(fallback[from * 2]) | (uint16_t(fallback[from * 2 + 1]) << 8);
          alpha[to] = fallback[2 * kFallbackSide * kFallbackSide + from];
        }
      }
    }
    if (resized) {
      lv_image_cache_drop(&source_);
      source_.header.w = rect.w;
      source_.header.h = rect.h;
      source_.header.stride = rect.w * 2;
      source_.data_size = plane * 3;
      lv_image_set_src(image_, &source_);
    }
    lv_obj_invalidate(image_);
    frame_key_ = frame_key;
  }
  if (previous_.x != rect.x || previous_.y != rect.y ||
      previous_.w != rect.w || previous_.h != rect.h)
    lv_obj_set_pos(image_, rect.x, rect.y);
  previous_ = rect;
  if (lv_obj_has_flag(image_, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
}

void HomePetView::set_conversation(bool active, const char *caption) {
  if (!conversation_) return;
  if (!active) {
    if (!lv_obj_has_flag(conversation_, LV_OBJ_FLAG_HIDDEN))
      lv_obj_add_flag(conversation_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (std::strcmp(lv_label_get_text(caption_), caption) != 0)
    lv_label_set_text(caption_, caption);
  lv_obj_remove_flag(conversation_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(conversation_);
  if (image_) lv_obj_move_foreground(image_);
}

void HomePetView::hide() {
  if (image_ && !lv_obj_has_flag(image_, LV_OBJ_FLAG_HIDDEN))
    lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
  frame_key_ = -2;
}

} // namespace smart_speaker
