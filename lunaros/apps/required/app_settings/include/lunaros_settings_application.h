#pragma once
#include "app/lifecycle/app_controller.h"
#include "app/package/application_data_ports.h"
#include "app/lifecycle/application_surface_session.h"
#include <memory>

namespace lunaredu::desktop { struct DesktopShell; }
namespace lunaredu::settings {
inline constexpr const char *kApplicationId = "os.settings";
// Product ports run on the existing foreground owner. Settings creates no task.
struct ApplicationPorts {
  void *context = nullptr;
  bool (*prepare)(void *, const char *, std::string &) = nullptr;
  bool (*enter)(void *, const char *, std::string &) = nullptr;
  bool (*leave)(void *, const char *, std::string &) = nullptr;
  desktop::DesktopShell *shell = nullptr; // Borrowed on the foreground owner.
  uint32_t (*now_ms)() = nullptr;
  lunaros::apps::ApplicationDataPorts data{};
  std::shared_ptr<app::ApplicationSurfaceSession> surface{};
  bool requires_display=true;
};
class Application {
public:
  Application() = default;
  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;
  // Retain this owner and its ports until the controller is stopped/destroyed.
  bool declare(app::AppController &, ApplicationPorts, std::string &);
private:
  ApplicationPorts ports_{};
  std::shared_ptr<app::ApplicationSurfaceSession> surface_;
  bool declared_ = false;
};
}
