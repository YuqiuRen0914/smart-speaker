#include "delivery/update/ota_version.h"

#include <cctype>

namespace lunaredu::lunaros::internal {

std::string normalize_ota_release_version(const std::string &version) {
  const size_t first = version.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const size_t last = version.find_last_not_of(" \t\r\n");
  const std::string trimmed = version.substr(first, last - first + 1);
  if (trimmed.size() != 5 && trimmed.size() != 6) {
    return trimmed;
  }
  unsigned packed = 0;
  for (const unsigned char character : trimmed) {
    if (!std::isdigit(character)) {
      return trimmed;
    }
    packed = packed * 10U + static_cast<unsigned>(character - '0');
  }
  const unsigned major = packed / 10000U;
  const unsigned minor = (packed / 100U) % 100U;
  const unsigned patch = packed % 100U;
  if (major == 0U) {
    return trimmed;
  }
  return std::to_string(major) + "." + std::to_string(minor) + "." +
         std::to_string(patch);
}

} // namespace lunaredu::lunaros::internal
