#include "lunaros_app_session.h"
#include <cstring>
namespace lunaredu::lunaros::apps {
bool validate_aot_header(const uint8_t *p, size_t n) {
  if (!p || n < 64 || n > 192 * 1024 || memcmp(p, "\0aot\6\0\0\0", 8)) return false;
  auto u32 = [](const uint8_t *b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8
    | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; };
  return u32(p+8) == 0 && u32(p+12) == 48 && u32(p+16) == 0
    && u32(p+20) == (1U | 94U << 16) && u32(p+24) == 1
    && !memcmp(p+48, "xtensa\0", 7) && p[63] == 0;
}
}
