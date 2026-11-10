#pragma once

#include <cstdint>

namespace lunaredu::pet::conversation::internal {

struct Runtime;

enum class ProductSystemState : uint8_t {
  Booting = 0,
  Provisioning,
  Offline,
  ActivationPending,
  Connecting,
  Ready,
  Recovering,
};

const char *product_system_state_name(ProductSystemState state);
ProductSystemState derive_product_system_state(const Runtime &runtime);

} // namespace lunaredu::pet::conversation::internal

