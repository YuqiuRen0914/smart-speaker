#include "app/package/application_package_policy.h"

namespace lunaredu::lunaros::apps {
int compare_app_versions(const std::string &candidate, const std::string &installed) {
  unsigned a[3]{}, b[3]{};
  if (!version_parts(candidate, a) || !version_parts(installed, b)) return 0;
  for (int i = 0; i < 3; ++i)
    if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
  return 0;
}

std::string application_package_path(const Application &app) {
  return "/managed/apps/" + app.id + "/" + app.version + ".lapp";
}
}
