#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lunaredu::lunaros {

// WPA2-Personal requires a passphrase of at least eight characters. Keep the
// local setup password at that minimum while making it easy to enter by hand.
constexpr size_t kProvisioningSoftApPasswordDigits = 8;

bool is_provisioning_softap_password(std::string_view password);
std::string make_provisioning_softap_password(uint32_t random_value);

} // namespace lunaredu::lunaros
