#pragma once

#include <string>

namespace lunaredu::lunaros::internal {

// The platform historically exposed a packed MMmmpp release code to some
// devices (for example 020105). Convert only that unambiguous 5/6-digit form;
// semantic versions and unknown formats pass through unchanged.
std::string normalize_ota_release_version(const std::string &version);

} // namespace lunaredu::lunaros::internal
