#pragma once
#include <array>
#include <cstdint>
namespace lunaredu::desktop {
struct LocalPageRequest {
  std::array<char, 49> page_id{};
  uint32_t revision = 0;
};
// Only the LVGL owner consumes the latest request.
bool take_local_page_request(LocalPageRequest &request);
} // namespace lunaredu::desktop
