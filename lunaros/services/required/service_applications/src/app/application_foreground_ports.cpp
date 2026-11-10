#include "lunaros_applications.h"
namespace lunaredu::lunaros::apps {
ApplicationForegroundPorts application_foreground_ports(ApplicationsService &runtime) {
  return {&runtime,
    [](void *p, const std::string &id, std::string &error) {
      auto &owner = *static_cast<ApplicationsService *>(p);
      return owner.start(id, error);
    },
    [](void *p, std::string &error) { return static_cast<ApplicationsService *>(p)->close(error, 0); },
    [](void *p, int kind, int a, int b, int c) { return static_cast<ApplicationsService *>(p)->input(kind, a, b, c); },
    [](void *p) { return static_cast<ApplicationsService *>(p)->running(); },
    [](void *p) { return static_cast<ApplicationsService *>(p)->realtime(); },
    [](void *p) { return static_cast<ApplicationsService *>(p)->frame(); }};
}
}
