#include "sdkconfig.h"
#include <string>
#include <utility>
#include "lunaredu_lunaros/desktop/local/lvgl_screen_runtime.h"

namespace {
[[maybe_unused]] constexpr const char *kDisabledError = "disabled";
[[maybe_unused]] void set_error(std::string &error, const char *message = kDisabledError) {
  error = message == nullptr ? kDisabledError : message;
}
}

#if !CONFIG_LUNAREDU_DESKTOP_LVGL_RUNTIME

namespace lunaredu::desktop {

bool setup_lvgl_screen_runtime(const LvglScreenRuntimeConfig &) {
  return false;
}

bool lvgl_screen_runtime_ready() { return false; }

LvglScreenRuntimeStatus lvgl_screen_runtime_status() {
  LvglScreenRuntimeStatus status;
  status.last_error = kDisabledError;
  return status;
}

void lvgl_screen_runtime_handler() {}
void lvgl_screen_runtime_set_refresh_enabled(bool) {}
bool lvgl_screen_runtime_test_rgb565(std::string *error) {
  if (error != nullptr) set_error(*error);
  return false;
}
void lvgl_screen_runtime_clear_rgb565_test() {}

} // namespace lunaredu::desktop

#endif
