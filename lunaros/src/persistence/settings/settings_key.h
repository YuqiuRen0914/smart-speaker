#pragma once

#include <string>

namespace lunaredu::lunaros::internal {

inline constexpr size_t kNvsKeyMaximumLength = 15;

// ESP-IDF NVS keys are limited to 15 bytes. Keep short keys readable and map
// longer product-owned setting names to a deterministic key so dynamic names
// (for example assistant_expression_<id>) remain persistable.
std::string normalize_nvs_key(const char *key);

} // namespace lunaredu::lunaros::internal
