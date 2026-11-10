#pragma once
#include "app/lifecycle/app_controller.h"
namespace lunaredu::app {
struct HostedApplicationPorts {
  void *context=nullptr;
  bool (*prepare)(void *,const char *,std::string &)=nullptr;
  bool (*enter)(void *,const char *,std::string &)=nullptr;
  bool (*leave)(void *,const char *,std::string &)=nullptr;
};
// Native content owners borrow a host's presentation/lifecycle ports. OS owns
// registration, preflight and checked exit; concrete Apps declare metadata.
// The id and callback context must outlive this retained registration.
class HostedApplication {
 public:
  HostedApplication()=default;
  HostedApplication(const HostedApplication &)=delete;
  HostedApplication &operator=(const HostedApplication &)=delete;
  bool declare(AppController &controller,const char *id,ForegroundManifest manifest,
      HostedApplicationPorts ports,const ApplicationPreparation *preparation,std::string &error);
 private:
  const char *id_=nullptr;
  HostedApplicationPorts ports_;
  bool declared_=false;
};
}
