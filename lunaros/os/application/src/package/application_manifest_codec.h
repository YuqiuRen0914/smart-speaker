#pragma once
#include "app/package/application_package.h"
#include <cstddef>

namespace lunaredu::lunaros::apps {
// Package decoding port implemented by the persistence adapter. The caller
// owns envelope bounds, entry digest verification and executable admission.
bool decode_package_manifest(const uint8_t *, size_t, uint32_t package_version,
                             Application &, std::string &error, bool aot_available);
} // namespace lunaredu::lunaros::apps
