#pragma once
#include "lunaros_firmware_installation.h"
#include "lunaros_resource_manifest.h"
#include "runtime/startup_sequence.h"
#include <span>
namespace lunaredu::lunaros {
struct PlatformLinkConfig;
struct RuntimeBootStep {const char *id;void *context;bool (*start)(void *,std::string &);};
struct RuntimeBootPlan {
  const char *name="LunarOS";
  FirmwareInstallationPolicy installation{};
  std::span<const ManagedAssetDefinition> assets{};
  void *context=nullptr;
  bool (*prepare)(void *,std::string &)=nullptr;
  std::span<const RuntimeBootStep> (*steps)(void *)=nullptr;
  // Standard-loop products supply their existing loop binding. Dedicated
  // executor plans already have boot-lived tasks and leave this null.
  void (*serve)(void *,bool started)=nullptr;
  bool (*recovery_link)(PlatformLinkConfig &,std::string &)=nullptr;
  void (*safe_failure)(void *,const char *,const std::string &)=nullptr;
  StartupSequence::Observer observer=nullptr;
};
// Runtime owns deployment/configuration, admission, rescue selection, failed
// boot rollback and the management loop. Product steps are borrowed metadata.
bool run_runtime_boot(const RuntimeBootPlan &);
}
