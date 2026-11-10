#include "settings/settings_key.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace lunaredu::lunaros::internal {

std::string normalize_nvs_key(const char *key) {
  if (key == nullptr) return {};
  const size_t length = std::strlen(key);
  if (length <= kNvsKeyMaximumLength) return key;

  // FNV-1a is intentionally fixed here; std::hash is not stable across
  // toolchains. The leading 'h' plus 14 hexadecimal digits fits NVS exactly.
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t index = 0; index < length; ++index) {
    hash ^= static_cast<uint8_t>(key[index]);
    hash *= UINT64_C(1099511628211);
  }
  std::array<char, kNvsKeyMaximumLength + 1> normalized{};
  std::snprintf(normalized.data(), normalized.size(), "h%014llx",
                static_cast<unsigned long long>(
                    hash & UINT64_C(0x00ffffffffffffff)));
  return normalized.data();
}

} // namespace lunaredu::lunaros::internal
