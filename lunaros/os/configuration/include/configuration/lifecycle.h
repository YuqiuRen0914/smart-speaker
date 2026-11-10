#pragma once

#include <string>

namespace lunaredu::lunaros {

// Core startup entry; call before admitting network or service work.
bool initialize_configuration(std::string &error);

// Recovery installers opt in before Runtime startup. Ordinary firmware still
// fails closed on an unreadable primary configuration. No partition is erased.
void enable_configuration_recovery();
bool configuration_recovery_active();
bool configuration_recovery_snapshot_available();
// A degraded boot cannot silently publish defaults over damaged user data.
// This explicit repair is only exposed by an administrator-authorized route.
bool restore_configuration_from_recovery(std::string &error);

} // namespace lunaredu::lunaros
