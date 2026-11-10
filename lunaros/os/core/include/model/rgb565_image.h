#pragma once
#include <cstdint>
#include <vector>
namespace lunaredu::lunaros {
struct Rgb565Image {
  uint16_t width=0,height=0;
  std::vector<uint16_t> pixels;
};
}
