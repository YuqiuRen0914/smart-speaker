#include "lunaredu_lunaros/device_api/standard_projections.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/device_link/device_link.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/persistence/system_info.h"
#include "lunaredu_lunaros/network/network.h"
#include "persistence/system/board_facts.h"
#include "device_api/access/device_api_access_policy.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
using namespace lunaredu::lunaros;
namespace { std::map<std::string,const DeviceModuleProjection*> modules; DeviceConfig settings; bool ready=true, accept=true, saved=false; int pairing=0, updates=0; }
namespace lunaredu::lunaros {
bool register_device_projection(const DeviceProjectionContribution &c,std::string *) { if(c.module)modules[c.module->module->id]=c.module; return true; }
DeviceConfig device_config(){return settings;}
bool save_device_identity_config(const char *name,const char *ap){assert(ap==nullptr); if(!accept)return false; settings.identity.name=name; saved=true; return true;}
const char *hardware_serial_number(){return "serial";}
const char *firmware_version(){return "1.0";}
BoardSupportFacts board_support_facts(){return {"board","test","Test Device"};}
bool service_available(const char *){return ready;}
NetworkSnapshot network_snapshot(){NetworkSnapshot n;n.link=NetworkLinkState::Up;return n;}
DeviceLinkPairingSnapshot device_link_pairing_snapshot(){DeviceLinkPairingSnapshot s;s.state=DeviceLinkBindingState::Pairing;s.code="123456";s.expires_at_ms=500;s.account_id="private";return s;}
bool request_device_link_pairing(){++pairing;return accept;}
bool cancel_device_link_pairing(){return accept;}
bool request_device_link_firmware_update(std::string &error){++updates; if(!accept)error="no_network";return accept;}
}
namespace lunaredu::system {
SystemInfoSnapshot read_system_info_snapshot(){SystemInfoSnapshot s;s.flash_size=16777216;s.chip_model_name="ESP32-S3";s.uptime_ms=1000;return s;}
}
const DeviceOperationDefinition &operation(const char *module,const char *id){const auto *m=modules.at(module);for(size_t i=0;i<m->operation_count;i++)if(!std::strcmp(m->operations[i].model.id,id))return m->operations[i];std::abort();}
int main(){
 std::string error;assert(setup_identity_projection(error));assert(setup_system_projection(error));assert(setup_device_link_projection(error));
 JsonDocument input,output;
 const auto &rename=operation("identity","name.set");assert(rename.model.mutating);assert(!std::strcmp(rename.model.owner_executor,kMainServiceExecutor));
 input["device_name"]="New Pet";settings.wifi.credentials[0].ssid="unchanged";
 assert(rename.projection.json_command_handler(input.as<JsonVariantConst>(),output.to<JsonObject>()).ok);assert(saved&&settings.identity.name=="New Pet");assert(settings.wifi.credentials[0].ssid=="unchanged");
 for(const auto &name:{std::string(""),std::string(65,'a'),std::string("bad\nname")}) {input["device_name"]=name;saved=false;assert(!rename.projection.json_command_handler(input.as<JsonVariantConst>(),output.to<JsonObject>()).ok);assert(!saved);}
 input["device_name"]="Valid";accept=false;assert(rename.projection.json_command_handler(input.as<JsonVariantConst>(),output.to<JsonObject>()).status_code==500);accept=true;
 operation("system","info.get").projection.json_provider(output.to<JsonObject>());assert(output["flash_bytes"]==16777216);
 operation("device_link","status.get").projection.json_provider(output.to<JsonObject>());assert(output["code"]=="123456");assert(!output["account_id"].is<const char*>());
 ready=false;assert(operation("device_link","pairing.start").projection.json_command_handler(input.as<JsonVariantConst>(),output.to<JsonObject>()).status_code==503);assert(pairing==0);
 ready=true;assert(operation("device_link","pairing.start").projection.json_command_handler(input.as<JsonVariantConst>(),output.to<JsonObject>()).status_code==202);assert(pairing==1);
 assert(operation("device_link","firmware.update").projection.json_command_handler(input.as<JsonVariantConst>(),output.to<JsonObject>()).status_code==202);assert(updates==1);
 for(const char *id:{"identity.status.get","identity.name.set","system.info.get","network.provision.scan","network.provision.submit"})assert(device_api_access_policy_allows(true,DeviceApiAccessSurface::Operation,id));
 for(const char *id:{"ota.firmware.upload","ota.restart","device_link.pairing.start","device_link.status.get","network.config.clear"})assert(!device_api_access_policy_allows(true,DeviceApiAccessSurface::Operation,id));
 std::cout<<"Identity isolation, system facts, binding readiness, update dispatch and AP access passed\n";
}
