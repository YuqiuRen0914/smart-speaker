#pragma once
#include <cstdint>
namespace lunaredu::desktop {
// Product binds hardware before UI startup. Callbacks run on the LVGL owner.
struct LocalDisplayConfig {
  uint16_t width = 0;
  uint16_t height = 0;
  bool (*read_touch_raw)(uint16_t *, uint16_t *) = nullptr;
};
}
