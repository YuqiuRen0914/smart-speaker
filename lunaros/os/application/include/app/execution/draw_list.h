#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace lunaredu::lunaros::apps {
struct DrawCommand {
  uint16_t op = 0, size = 0, align = 0;
  int x = 0, y = 0, width = 0, height = 0;
  uint16_t color = 0;
  std::string text;
};
using DrawList = std::vector<DrawCommand>;
struct DrawLimits { size_t bytes, commands; int width, height; };
// Decode transactionally using the admission owner's explicit budget.
bool decode_draw_list(const uint8_t *, size_t, DrawList &, DrawLimits);
}
