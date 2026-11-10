#pragma once
#include "lunaros_applications_service_id.h"

#include "lunaros_app_manager_view.h"
#include "app/lifecycle/hosted_application.h"

namespace lunaredu::lunaros::apps::manager {
inline constexpr const char *kApplicationId = "os.appstore";
class SystemApplication {
 public:
  bool declare(app::AppController &controller,app::HostedApplicationPorts ports,
      const app::ApplicationPreparation *preparation,std::string &error);
 private:
  app::HostedApplication application_;
};

// The shell handles launch requests and its hardware navigation. The system
// app owns detail pages, confirmation flows, distribution requests; local actions are delegated to the OS runtime.
struct ActionResult {
  std::string launch_id;
  bool builtin = false;
  bool catalog_changed = false;
};

void activate_row(View &, const StoreSnapshot &);
bool back(View &);
ActionResult dispatch(View &, Store &, ApplicationsService &, uint32_t action);
} // namespace lunaredu::lunaros::apps::manager
