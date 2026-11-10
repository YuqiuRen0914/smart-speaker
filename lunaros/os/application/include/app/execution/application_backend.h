#pragma once
#include "app/package/package_format.h"
#include "app/execution/application_session.h"
#include "app/package/application_limits.h"

namespace lunaredu::lunaros { class KernelRuntime; }
namespace lunaredu::lunaros::apps {
using ApplicationArtifactProfile = package::TargetProfile;
// Profile and strings are borrowed for the boot lifetime; lookup creates no VM.
using ApplicationSessionFactory = std::unique_ptr<ApplicationSession> (*)(KernelRuntime &);
using ApplicationEntryValidator = bool (*)(const uint8_t *, size_t);
// One firmware-wide catalog of compiled physical engine providers, not a
// session registry. Definitions and availability callbacks outlive all users;
// services own VM state. Resetting a Device/Kernel never unregisters providers.
// Register optional backends during product composition, before applications launch.
// Registration never starts a VM; an unselected backend cannot be launched.
bool register_application_backend(const char *id, ApplicationSessionFactory,
                                  ApplicationEntryValidator = nullptr,
                                  bool (*available)() = nullptr,
                                  const ApplicationArtifactProfile *profile = nullptr);
bool application_backend_available(const std::string &id);
bool validate_application_entry(const std::string &backend, const uint8_t *, size_t);
} // namespace lunaredu::lunaros::apps
