#include "lunaros_app_store_projection.h"
#include <algorithm>
#include <cstring>
namespace lunaredu::lunaros::apps {
namespace {
constexpr const char *presence[]={"application_packages"};
const char *phase(StorePhase value) {
  switch(value) {
    case StorePhase::Idle:return "idle";
    case StorePhase::Fetching:return "fetching";
    case StorePhase::Downloading:return "downloading";
    case StorePhase::Verifying:return "verifying";
    case StorePhase::Committing:return "committing";
    case StorePhase::Failed:return "failed";
  }
  return "unknown";
}
void applications(JsonArray out,const std::vector<Application> &items) {
  for(const auto &app:items) {
    auto item=out.add<JsonObject>();
    item["id"]=app.id;item["title"]=app.name;item["version"]=app.version;
    item["backend"]=app.backend;item["entry_format"]=app.entry_format;item["bytes"]=app.size;
  }
}
DeviceOperationResult failure(const std::string &error,uint16_t status=409) {
  return {false,status,error.empty()?"package_operation_unavailable":error};
}
}
PackageManagementProjection::PackageManagementProjection()
    : bindings_{{
      {[](void *p,JsonVariantConst,JsonObject out){return static_cast<PackageManagementProjection *>(p)->status(out,false);},this},
      {[](void *p,JsonVariantConst,JsonObject out){return static_cast<PackageManagementProjection *>(p)->status(out,true);},this},
      {[](void *p,JsonVariantConst in,JsonObject out){return static_cast<PackageManagementProjection *>(p)->command(0,in,out);},this},
      {[](void *p,JsonVariantConst in,JsonObject out){return static_cast<PackageManagementProjection *>(p)->command(1,in,out);},this},
      {[](void *p,JsonVariantConst in,JsonObject out){return static_cast<PackageManagementProjection *>(p)->command(2,in,out);},this},
      {[](void *p,JsonVariantConst in,JsonObject out){return static_cast<PackageManagementProjection *>(p)->command(3,in,out);},this}}},
      operations_{{make_device_json_binding("status.get",bindings_[0],false),
        make_device_json_binding("catalog.get",bindings_[1],false),
        make_device_json_binding("refresh",bindings_[2],true),
        make_device_json_binding("install",bindings_[3],true,256),
        make_device_json_binding("uninstall",bindings_[4],true,256),
        make_device_json_binding("launch",bindings_[5],true,256)}},
      capability_{"application_packages",presence,std::size(presence)},
      metadata_{"application_packages","application_packages",nullptr,"可安装应用"},
      api_{capability_.id,&metadata_,operations_.data(),operations_.size()} {}

bool PackageManagementProjection::declare(DeviceProjectionRegistry &registry,
    PackageManagementPorts ports,std::string &error) {
  if(!ports.context||!ports.snapshot||!ports.running_id||!ports.refresh||!ports.install||!ports.uninstall||!ports.launch_context||!ports.launch) {
    error="package_management_ports_invalid";return false;
  }
  if(registry_ && (registry_!=&registry || ports_.context!=ports.context ||
      ports_.snapshot!=ports.snapshot || ports_.running_id!=ports.running_id || ports_.refresh!=ports.refresh ||
      ports_.install!=ports.install || ports_.uninstall!=ports.uninstall || ports_.launch_context!=ports.launch_context || ports_.launch!=ports.launch)) {
    error="package_management_owner_conflict";return false;
  }
  if(!registry.register_projection({&capability_,&api_},&error))return false;
  ports_=ports;registry_=&registry;return true;
}
DeviceOperationResult PackageManagementProjection::status(JsonObject out,bool catalog) {
  const auto *view=ports_.snapshot?ports_.snapshot(ports_.context):nullptr;
  out["ready"]=view!=nullptr;
  out["running_id"]=ports_.running_id?ports_.running_id(ports_.context):"";
  if(!view)return {};
  out["generation"]=view->generation;out["phase"]=phase(view->phase);
  out["error"]=view->error;out["message"]=view->message;
  out["received_bytes"]=view->received_bytes;out["total_bytes"]=view->total_bytes;
  if(catalog) {
    applications(out["installed"].to<JsonArray>(),view->installed.entries());
    applications(out["available"].to<JsonArray>(),view->discovered.entries());
  }
  return {};
}
DeviceOperationResult PackageManagementProjection::command(unsigned action,JsonVariantConst in,JsonObject out) {
  if(!registry_)return failure("package_management_not_declared");
  const auto *view=ports_.snapshot(ports_.context);
  const char *id=action?in["id"].as<const char *>():nullptr;
  if(action && (!id || !id[0] || std::strlen(id)>64))return failure("package_id_required",400);
  if(action==2 && !in["delete_data"].is<bool>())return failure("package_data_choice_required",400);
  if(action && !view)return failure("application_directory_unavailable");
  if(view && view->phase!=StorePhase::Idle && view->phase!=StorePhase::Failed)return failure("package_operation_pending");
  const auto contains=[&](const auto &items){return std::any_of(items.begin(),items.end(),[&](const auto &app){return app.id==id;});};
  if(action==1 && !contains(view->discovered))return failure("package_not_in_catalog",404);
  if((action==2||action==3) && !contains(view->installed))return failure("package_not_installed",404);
  const char *running=ports_.running_id(ports_.context);
  // Do not tear down a backend behind its foreground/lease owner.
  if(action==2 && running && !std::strcmp(running,id))return failure("exit_active_package_first");
  if(action==3 && running && running[0] && std::strcmp(running,id))return failure("exit_active_package_first");
  std::string error;
  const bool accepted=action==0?ports_.refresh(ports_.context,error):
      action==1?ports_.install(ports_.context,id,error):
      action==2?ports_.uninstall(ports_.context,id,in["delete_data"].as<bool>(),error):
      ports_.launch(ports_.launch_context,id,error);
  if(!accepted)return failure(error);
  (void)status(out,false);out["accepted"]=true;
  return {true,202,{}};
}
}
