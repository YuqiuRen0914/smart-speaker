#pragma once
#include "lunaros_app_store.h"
namespace lunaredu::lunaros::apps {
// Main-owned borrowed view and actions of the existing package/desktop owner.
// No duplicate Store, catalog, VM, command queue or installation worker.
struct PackageManagementPorts {
  void *context=nullptr;
  const StoreSnapshot *(*snapshot)(void *)=nullptr;
  const char *(*running_id)(void *)=nullptr;
  bool (*refresh)(void *,std::string &)=nullptr;
  bool (*install)(void *,const std::string &,std::string &)=nullptr;
  bool (*uninstall)(void *,const std::string &,bool,std::string &)=nullptr;
  void *launch_context=nullptr;
  bool (*launch)(void *,const std::string &,std::string &)=nullptr;
};
}
