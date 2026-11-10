#include "network/provisioning/provisioning_softap_password.h"

#include <array>
#include <cstdio>

namespace lunaredu::lunaros {

bool is_provisioning_softap_password(std::string_view password) {
  if (password.size() != kProvisioningSoftApPasswordDigits) {
    return false;
  }
  for (const char character : password) {
    if (character < '0' || character > '9') {
      return false;
    }
  }
  return true;
}

std::string make_provisioning_softap_password(uint32_t random_value) {
  constexpr uint32_t kPasswordRange = 100000000;
  std::array<char, kProvisioningSoftApPasswordDigits + 1> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%08lu",
                static_cast<unsigned long>(random_value % kPasswordRange));
  return std::string(buffer.data());
}

} // namespace lunaredu::lunaros
