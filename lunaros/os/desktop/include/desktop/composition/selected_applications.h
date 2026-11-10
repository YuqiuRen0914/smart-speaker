#pragma once
#include "desktop/composition/applications.h"
#include "desktop/composition/selection.h"
#include "app/lifecycle/app_controller.h"
#include <tuple>
#include <type_traits>

namespace lunaredu::lunaros {
// Shared registration for a selected desktop. Native entries use the same
// controller as Shell and downloaded Apps; declaring never enters foreground.
template<class Configuration> class SelectedDesktopApplications {
 public:
  SelectedDesktopApplications() = default;
  template<class Ports> bool declare(app::AppController &applications, Ports ports, std::string &error) {
    if (applications_) { error = "desktop_applications_already_declared"; return false; }
    const auto &selection = Configuration::selection();
    if (!selection.valid()) { error = "invalid_desktop_app_selection"; return false; }
    applications_ = &applications;
    ports.providers = selection.providers(); ports.provider_count = selection.provider_count();
    const auto &catalog = selection.catalog();
    static_assert(std::tuple_size_v<std::remove_reference_t<decltype(catalog.applications)>> <= app::kApplicationCapacity,
        "Selected desktop entries exceed CONFIG_LUNAREDU_LUNAROS_FOREGROUND_CAPACITY");
    return system_.declare(applications, Configuration::shell_id, catalog.applications.data(),
        catalog.applications.size(), ports, error) && selection.declare_native(applications, ports, error);
  }
  bool switch_to(const char *id, std::string &error) {
    if (!applications_) { error = "desktop_applications_unavailable"; return false; }
    return applications_->switch_to(id, error);
  }
  bool open_guest(const std::string &id, std::string &error) { return system_.open_guest(id, error); }
  app::AppController *controller() const { return applications_; }
 private:
  app::AppController *applications_ = nullptr;
  DesktopApplications system_;
};
}
