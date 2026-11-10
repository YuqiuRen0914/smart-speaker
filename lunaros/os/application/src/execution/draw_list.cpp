#include "app/execution/draw_list.h"
#include <algorithm>
#include <cstring>
namespace lunaredu::lunaros::apps {
namespace {
uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
int32_t i32(const uint8_t *p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++)
    v |= uint32_t(p[i]) << (8 * i);
  return static_cast<int32_t>(v);
}
}
bool decode_draw_list(const uint8_t *bytes, size_t size, DrawList &out, DrawLimits limits) {
  if (limits.width <= 0 || limits.height <= 0 || limits.width > INT32_MAX / 2 || limits.height > INT32_MAX / 2) return false;
  if (!bytes || size < 24 || size > limits.bytes)
    return false;
  DrawList next;
  next.reserve(std::min<size_t>(limits.commands,size/24));
  size_t at = 0;
  while (at < size) {
    if (size - at < 24 || next.size() >= limits.commands)
      return false;
    const auto *p = bytes + at;
    uint16_t op = u16(p), n = u16(p + 2);
    if (n < 24 || n > size - at || op < 1 || op > 3 || (op != 3 && n != 24))
      return false;
    DrawCommand c;
    c.op = op;
    c.x = i32(p + 4);
    c.y = i32(p + 8);
    c.width = i32(p + 12);
    c.height = i32(p + 16);
    int color = i32(p + 20);
    if (c.x < -limits.width || c.x > 2 * limits.width || c.y < -limits.height || c.y > 2 * limits.height || c.width < 0 ||
        c.width > 2 * limits.width || c.height < 0 || c.height > 2 * limits.height || color < 0 ||
        color > 65535)
      return false;
    c.color = uint16_t(color);
    if (op == 3) {
      if (n < 29 || n > 284 || p[n - 1] != 0)
        return false;
      c.size = u16(p + 24);
      c.align = u16(p + 26);
      if (c.size < 8 || c.size > 40 || c.align > 1 || memchr(p + 28, 0, n - 29))
        return false;
      c.text.assign(reinterpret_cast<const char *>(p + 28), n - 29);
    }
    if (op == 1 &&
        (!next.empty() || c.x || c.y || c.width != limits.width || c.height != limits.height))
      return false;
    next.push_back(std::move(c));
    at += n;
  }
  if (next.empty() || next.front().op != 1)
    return false;
  out = std::move(next);
  return true;
}
}
