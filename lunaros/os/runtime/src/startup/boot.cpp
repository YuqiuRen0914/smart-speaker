#include "runtime/boot.h"
#include "runtime/device.h"
#include "runtime/device_context.h"
#include "kernel/runtime.h"
#include "runtime/update_maintenance.h"
#include "runtime/preset.h"
#include "configuration/lifecycle.h"
#include "configuration/settings.h"
#include "system/boot_guard.h"
#include "lunaredu_ota.h"
#include "lunaros_resource_managed_assets.h"
#include "lunaros_provisioning_policy.h"
#include "lunaros_platform_link.h"
#include "lunaros_platform_link_json.h"
#include "lunaros_platform_link_projection.h"
#include "lunaros_network.h"
#include "lunaros_local_web_lifecycle.h"
#include "lunaros_device_api_service_id.h"
#include "device_api/core_projections.h"
#include "device_api/projection_composition.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace lunaredu::lunaros {
namespace {
[[noreturn]] void management(const RuntimeBootPlan &plan,bool installation){
  // No product business or actuator declarations are installed in this graph.
  configure_firmware_installation_recovery(!installation);
  static Device device;
  DeviceOptions options;options.name=plan.name;options.enable_applications=false;
  static const RuntimePreset preset=[]{auto p=default_runtime_preset();p.id="runtime.management";p.default_execution_services=false;p.configure_backends=nullptr;return p;}();
  static const ProvisioningPolicy provisioning=[]{ProvisioningPolicy p;p.mode=ProvisioningMode::SoftApBle;return p;}();
  options.preset=&preset;options.provisioning=&provisioning;
  options.assets={plan.assets.data(),plan.assets.size()};
  PlatformLinkConfig link;
  link.open_claim=true;link.auto_pairing=false;link.realtime_teleop_enabled=false;
  link.remote_commands_enabled=false;link.firmware_updates_enabled=false;link.managed_assets_enabled=false;
  link.capabilities={"local_device_api","wifi_provisioning","ota","recovery"};
  static const PlatformLinkObserver observer{.presence=[](const PlatformLinkPairingSnapshot &,JsonVariantConst payload){firmware_installation_accept_cloud_assets(payload["managedAssets"]);}};
  link.observer=&observer;
  std::string error;
  bool link_ready=!plan.recovery_link||plan.recovery_link(link,error);
  if(link_ready)link_ready=configure_platform_link_runtime(device.context(),link,error);
  if(!link_ready)ESP_LOGW("runtime.boot","management platform unavailable: %s",error.c_str());
  static const ServiceNode services[]={platform_link_service_node(device.context())};
  static const ProductDeclarationNode declarations[]={
    standard_projection_declaration_node<standard_projection::platform_link>("platform_link",device.projections()),
    firmware_installation_declaration(device.projections())};
  if(link_ready)options.services=services;
  options.declarations=declarations;
  if(installation)options.initialize=begin_firmware_installation;
  // A cloud configuration or optional coordinator failure must not suppress
  // local system diagnostics. OTA still fails closed without its handler.
  if(!configure_update_maintenance(device.context(),{{},nullptr,nullptr,5000,false},error))
    ESP_LOGE("runtime.boot","management update owner: %s",error.c_str());
  const bool started=device.start(options,error,StartupFailureOwner::BootCoordinator);
  if(!started)ESP_LOGE("runtime.boot","management start: %s",error.c_str());
  for(;;){
    const bool ready=started&&device.runtime().service_available(kDeviceApiServiceId)&&local_web_management_ready();
    device.poll(ready);poll_firmware_installation();
    vTaskDelay(pdMS_TO_TICKS(kNetworkPollIntervalMs));
  }
}
}
bool run_runtime_boot(const RuntimeBootPlan &plan){
  ota_service_start_boot_guard();enable_configuration_recovery();
  const char *stage="deployment";std::string error;
  StartupSequence boot(stage,error,plan.observer,plan.context);
  bool deployment_ready=false;
  boot.step("boot_declarations",[&](std::string &e){if(!plan.name||!*plan.name||!plan.steps){e="runtime_boot_declarations_invalid";return false;}return true;});
  boot.step("installation_policy",[&](std::string &e){return configure_firmware_installation(plan.installation,e);});
  boot.step("deployment",[&](std::string &e){deployment_ready=ota_service_initialize_deployment(e);return deployment_ready;});
  boot.step("storage",initialize_settings);
  boot.step("assets",[&](std::string &e){return configure_managed_assets(plan.assets.data(),plan.assets.size(),e);});
  const bool rescue=system_boot_guard_snapshot().rescue_requested;
  if(boot&&rescue)management(plan,false);
  if(boot&&!plan.installation.existing_assets_only&&firmware_installation_pending())management(plan,true);
  // A confirmed image alone does not admit business with missing required
  // resources. Rescue remains read/repair-only; it never replays an old plan.
  if(boot&&!plan.installation.required_assets.empty()){
    if(!validate_firmware_installation_assets(error))management(plan,false);
  }
  if(boot&&plan.prepare)boot.step("product_declarations",[&](std::string &e){return plan.prepare(plan.context,e);});
  bool business_declared=false;
  if(boot&&plan.steps){
    business_declared=true;
    for(const auto &step:plan.steps(plan.context)){
      boot.step(step.id,[&](std::string &e){if(!step.start){e="runtime_boot_step_invalid";return false;}return step.start(step.context,e);});
      if(!boot)break;
    }
  }
  if(!boot){
    ESP_LOGE("runtime.boot","%s stage=%s failed: %s",plan.name,stage,error.c_str());
    if(plan.safe_failure)plan.safe_failure(plan.context,stage,error);
    (void)ota_service_rollback_running_image(error.c_str());
    // No business graph exists yet: retain a system management path after a
    // settings/role declaration failure. Invalid deployment stays fail-closed.
    if(!business_declared&&deployment_ready)management(plan,false);
  }
  if(plan.serve&&business_declared)plan.serve(plan.context,bool(boot));
  return bool(boot);
}
}
