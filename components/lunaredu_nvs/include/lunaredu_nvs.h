#pragma once
#include <string>

namespace lunaredu::nvs {
// Shared, serialized initialization. Failure never erases user data; recovery
// must be an explicit operation on the intended partition or namespace.
bool initialize(std::string &error);
} // namespace lunaredu::nvs
