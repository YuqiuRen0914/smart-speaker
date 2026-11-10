#include "lunaredu_lunaros/desktop/local/lvgl_screen_runtime.h"
#include <cassert>
int main() {
  using namespace lunaredu::desktop;
  assert(!setup_lvgl_screen_runtime({}));
  assert(!lvgl_screen_runtime_ready());
  assert(lvgl_screen_runtime_status().last_error == "disabled");
  lvgl_screen_runtime_handler();
  lvgl_screen_runtime_handler();
  lvgl_screen_runtime_set_refresh_enabled(true);
  std::string error;
  assert(!lvgl_screen_runtime_test_rgb565(&error) && error == "disabled");
  lvgl_screen_runtime_clear_rgb565_test();
}
