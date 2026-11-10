#pragma once

#include <cstddef>
#include <string>

namespace lunaredu::lunaros::internal {

bool ensure_private_resource_capacity(size_t required_bytes,
                                      std::string &error);

} // namespace lunaredu::lunaros::internal
