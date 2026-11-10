#pragma once

#include <string>

namespace lunaredu::lunaros {
class DeviceProjectionRegistry;

// LunarOS owns the transport projection. Products only opt in and register
// checks supplied by their hardware and applications.
bool declare_local_factory_projection(DeviceProjectionRegistry &registry,
                                      std::string &error);
}  // namespace lunaredu::lunaros
