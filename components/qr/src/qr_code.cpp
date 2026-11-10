#include "lunaredu_qr.h"

#include <array>
#include <algorithm>

#include <qrcodegen.h>

namespace lunaredu::lunaros {

QrCode::Result QrCode::generate(std::string_view text, int minimum_version) {
  clear();
  if (minimum_version < 1 || minimum_version > kMaxVersion) return Result::InvalidText;
  if (text.empty()) return Result::Empty;
  if (text.size() > kMaxTextBytes) return Result::TooLong;
  if (text.find('\0') != std::string_view::npos) return Result::InvalidText;

  std::array<char, kMaxTextBytes + 1> terminated{};
  std::copy(text.begin(), text.end(), terminated.begin());
  std::array<uint8_t, kBufferBytes> scratch{};
  if (!qrcodegen_encodeText(terminated.data(), scratch.data(), modules_.data(),
                            qrcodegen_Ecc_MEDIUM, minimum_version,
                            kMaxVersion, qrcodegen_Mask_AUTO, false)) {
    clear();
    return Result::TooLong;
  }
  size_ = qrcodegen_getSize(modules_.data());
  return Result::Ready;
}

void QrCode::clear() {
  modules_.fill(0);
  size_ = 0;
}

bool QrCode::dark_at(int x, int y) const {
  x -= kQuietZone;
  y -= kQuietZone;
  if (!ready() || x < 0 || y < 0 || x >= size_ || y >= size_) return false;
  return qrcodegen_getModule(modules_.data(), x, y);
}

bool QrCode::paint(int x, int y, int max_side, void *context,
                   FillRect fill) const {
  if (!ready() || fill == nullptr || max_side < display_size() * 2) return false;
  const int scale = max_side / display_size();
  const int painted_side = display_size() * scale;
  const int offset = (max_side - painted_side) / 2;
  x += offset;
  y += offset;
  fill(context, x, y, painted_side, painted_side, false);
  for (int row = 0; row < size_; ++row) {
    int column = 0;
    while (column < size_) {
      if (!dark_at(column + kQuietZone, row + kQuietZone)) {
        ++column;
        continue;
      }
      const int start = column;
      do {
        ++column;
      } while (column < size_ &&
               dark_at(column + kQuietZone, row + kQuietZone));
      fill(context, x + (start + kQuietZone) * scale,
           y + (row + kQuietZone) * scale,
           (column - start) * scale, scale, true);
    }
  }
  return true;
}

}  // namespace lunaredu::lunaros
