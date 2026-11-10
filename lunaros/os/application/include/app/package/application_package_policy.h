#pragma once
#include "app/package/application_package.h"
#include "app/package/application_identity.h"
namespace lunaredu::lunaros::apps {
// Admission/version and installed-path policy is independent of filesystem I/O.
int compare_app_versions(const std::string &candidate, const std::string &installed);
std::string application_package_path(const Application &app);
}
