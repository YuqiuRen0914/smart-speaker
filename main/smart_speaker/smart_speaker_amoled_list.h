#pragma once

#include <array>
#include <string>
#include <lvgl.h>

namespace smart_speaker {

// View only. ProductState owns selection and navigation; the existing display
// task creates, updates and destroys these objects with their parent.
class AmoledList {
 public:
  static constexpr size_t kMaxItems = 7;
  void create(lv_obj_t *parent, const lv_font_t *font);
  void show(const std::array<std::string, kMaxItems> &labels,
            const std::array<const char *, kMaxItems> &icons,
            size_t count, size_t selected, int page_key);
  void hide();

 private:
  lv_obj_t *container_ = nullptr;
  std::array<lv_obj_t *, kMaxItems> rows_{};
  std::array<std::string, kMaxItems> row_labels_{};
  int page_key_ = -1;
  size_t selected_ = kMaxItems;
};

} // namespace smart_speaker
