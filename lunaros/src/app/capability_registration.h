#pragma once

#include <string>

namespace lunaredu::app {
// Boot composition is serialized, before executors or discovery can run.
// The callback may only register providers/consumers, not reset/seal the registry
// or change its access policy. A failed declaration removes only its additions.
bool declare_capabilities_atomically(bool (*declare)(std::string &),
                                     std::string &error);
} // namespace lunaredu::app
