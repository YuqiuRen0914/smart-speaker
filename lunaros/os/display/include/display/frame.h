#pragma once
#include <cstdint>
#include <string_view>
namespace lunaredu::display {
struct FramePresentationKey {
  uint64_t generation = 0;
  std::string_view application; // Borrowed for this submission only.
};
struct FrameCanvas {
  uint16_t *pixels = nullptr;
  uint16_t width = 0, height = 0, stride = 0;
  // Issued by the screen owner. Successful submission consumes this lease.
  const void *issuer = nullptr;
  uint64_t generation = 0;
  unsigned slot = 0;
};
enum class FrameSubmission { Failed, Unchanged, Submitted };

}
