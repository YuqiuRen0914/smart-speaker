#pragma once
#include <lvgl/lvgl.h>
#include <array>
namespace lunaredu::lunaros::graphics {
// One Scene/render owner, shared with its synchronous CanvasPainter only;
// never shared with the separate direct canvas/Pet render threads.
class SceneFonts {
  struct Entry {unsigned size=0;lv_font_t *font=nullptr;};
  std::array<Entry,32> entries_{};
  unsigned cached_fonts_=0;
public:
  const lv_font_t *get(unsigned size);
  uint32_t generation() const;
  void clear(); // Only after every widget using these fonts has retired.
  ~SceneFonts(){clear();}
};
}
