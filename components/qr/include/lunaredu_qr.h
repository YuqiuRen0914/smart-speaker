#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lunaredu::lunaros {

// Display-independent, caller-owned QR symbol. Generate it only while a screen
// needs it, then clear it when the screen closes or its payload expires.
class QrCode {
 public:
  static constexpr int kMaxVersion = 12;
  static constexpr int kQuietZone = 4;
  static constexpr size_t kMaxTextBytes = 240;

  enum class Result : uint8_t {
    Ready,
    Empty,
    InvalidText,
    TooLong,
  };

  // A shared minimum version keeps related symbols at the same module density.
  Result generate(std::string_view text, int minimum_version = 1);
  void clear();

  bool ready() const { return size_ != 0; }
  int size() const { return size_; }
  int display_size() const { return ready() ? size_ + 2 * kQuietZone : 0; }

  using FillRect = void (*)(void *context, int x, int y, int width,
                            int height, bool dark);

  // Draws an opaque white square with black modules, centered within a
  // max_side square. Returns false without drawing if fewer than two pixels
  // per module are available or if the symbol is not ready.
  bool paint(int x, int y, int max_side, void *context, FillRect fill) const;

  // Coordinates include the required white quiet zone. Out-of-range modules
  // are white. Paint each module as a solid square at an integer pixel scale.
  bool dark_at(int x, int y) const;

 private:
  static constexpr int kMaxModules = kMaxVersion * 4 + 17;
  static constexpr size_t kBufferBytes =
      (kMaxModules * kMaxModules + 7) / 8 + 1;
  std::array<uint8_t, kBufferBytes> modules_{};
  int size_ = 0;
};

}  // namespace lunaredu::lunaros
