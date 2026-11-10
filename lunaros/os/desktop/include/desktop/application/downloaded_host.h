#pragma once
#include "app/lifecycle/app_controller.h"

namespace lunaredu::lunaros::apps {
inline constexpr const char *kDownloadedHostId = "os.downloaded";
struct DownloadedHostPorts {
  void *context = nullptr;
  bool (*prepare)(void *, const char *, std::string &) = nullptr;
  app::ApplicationStartResult (*open)(void *, const std::string &, std::string &) = nullptr;
  bool (*close)(void *, std::string &) = nullptr;
  bool (*entered)(void *, const char *, std::string &) = nullptr;
  bool (*left)(void *, const char *, std::string &) = nullptr;
  const app::ApplicationPreparation *preparation = nullptr;
  const char *service_id = nullptr;
};
// Hidden foreground owner for installed packages. No VM starts at registration.
// The controller owns leases; backend callbacks retain their state on failure.
class DownloadedHost {
 public:
  bool declare(app::AppController &, const char *return_id, DownloadedHostPorts,
               std::string &);
  bool open(const std::string &id, std::string &);

 private:
  app::AppController *controller_ = nullptr;
  DownloadedHostPorts ports_;
  app::ApplicationServiceRequirement service_;
  std::string return_id_, selected_id_;
  bool retiring_ = false;
};
} // namespace lunaredu::lunaros::apps
