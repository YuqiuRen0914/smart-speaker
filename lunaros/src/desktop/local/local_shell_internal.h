#pragma once
#include <cstdint>
namespace lunaredu::desktop {
// Called only by the Desktop runtime on the LVGL owner.
bool read_local_touch_raw(uint16_t *x, uint16_t *y);
bool initialize_local_shell();
void request_local_shell_page(const char *route, uint32_t revision);
} // namespace lunaredu::desktop
