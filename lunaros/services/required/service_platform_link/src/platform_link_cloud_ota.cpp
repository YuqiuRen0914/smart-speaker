#include "platform_link_cloud_ota.h"
#include "platform_link_state.h"
#include "platform_link_ota_retry.h"
#include "platform_link_http.h"
#include "configuration/settings.h"
#include <algorithm>
#include <mutex>
#ifdef ESP_PLATFORM
#include <esp_random.h>
#endif

namespace lunaredu::lunaros::platform_link {
namespace {
constexpr const char *kSpace="lunar_link",*kKey="role_ota";
struct Intent {
  bool active=false;
  OtaCloudInstallRequest request;
  std::string assignment,release,origin,source,session,stage,error;
  int epoch=0;
};
std::recursive_mutex mutex;
Intent intent;
bool loaded=false,restored=false,read_failed=false;
std::string boot_session;
bool hash(const std::string &v){return v.size()==64&&std::all_of(v.begin(),v.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}
bool identifier(const std::string &s){return !s.empty()&&s.size()<=96&&s.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-")==std::string::npos;}
std::string public_error(const std::string &raw) {
  const auto end=raw.find(':');
  const auto code=raw.substr(0,std::min(end,size_t{80}));
  if(code.empty()||code.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos)return "cloud_ota_failed";
  return code;
}
std::string current_origin(){return platform_base_url();}
OtaCloudInstallRequest request_from(JsonVariantConst v){
  OtaCloudInstallRequest r;r.protocol_version=v["protocol_version"]|uint32_t{0};
  r.url=v["download_url"]|"";r.sha256=v["package_sha256"]|"";r.size=v["package_size"]|size_t{0};
  r.app_elf_sha256=v["app_elf_sha256"]|"";r.project_name=v["project_name"]|"";r.version=v["latest_version"]|"";
  r.target_role=v["target_role"]|"";r.layout_id=v["layout_id"]|"";r.hardware_id=v["hardware_id"]|"";return r;
}
void write_request(JsonObject v,const OtaCloudInstallRequest &r){
  v["protocol_version"]=r.protocol_version;v["download_url"]=r.url;v["package_sha256"]=r.sha256;v["package_size"]=r.size;
  v["app_elf_sha256"]=r.app_elf_sha256;v["project_name"]=r.project_name;v["latest_version"]=r.version;
  v["target_role"]=r.target_role;v["layout_id"]=r.layout_id;v["hardware_id"]=r.hardware_id;
}
bool load(){
  if(loaded)return !read_failed;
  loaded=true;std::string text,error;
  const auto result=settings_read_string(kSpace,kKey,4096,text,error);
  if(result==SettingsReadResult::Missing)return true;
  JsonDocument doc;
  if(result!=SettingsReadResult::Found||deserializeJson(doc,text)||doc["schema"]!=2){read_failed=true;return false;}
  intent.active=doc["active"]|false;
  if(!intent.active)return true;
  intent.request=request_from(doc["offer"]);intent.assignment=doc["assignment"]|"";intent.release=doc["release"]|"";
  intent.origin=doc["origin"]|"";intent.epoch=doc["epoch"]|0;intent.stage=doc["stage"]|"";
  intent.source=doc["source"]|"";intent.session=doc["session"]|"";intent.error=doc["error"]|"";
  if(!identifier(intent.assignment)||!identifier(intent.release)||!hash(intent.request.sha256)||!hash(intent.request.app_elf_sha256)||
      intent.request.protocol_version!=2||intent.origin.rfind("https://",0)!=0||intent.origin.size()>256||intent.epoch<0||
      (intent.stage!="handoff"&&intent.stage!="applying"&&intent.stage!="complete"&&intent.stage!="failed")){
    read_failed=true;intent={};return false;
  }
  restored=true;return true;
}
bool save(){
  JsonDocument doc;doc["schema"]=2;doc["active"]=intent.active;
  if(intent.active){write_request(doc["offer"].to<JsonObject>(),intent.request);
    doc["assignment"]=intent.assignment;doc["release"]=intent.release;doc["origin"]=intent.origin;doc["epoch"]=intent.epoch;
    doc["stage"]=intent.stage;doc["error"]=intent.error;doc["source"]=intent.source;doc["session"]=intent.session;
  }
  std::string text;serializeJson(doc,text);
  if(text.size()>4096||!settings_set_string(kSpace,kKey,text)){read_failed=true;return false;}
  read_failed=false;return true;
}
bool same(const OtaCloudInstallRequest &a,const OtaCloudInstallRequest &b){return a.sha256==b.sha256&&a.app_elf_sha256==b.app_elf_sha256&&a.target_role==b.target_role&&a.layout_id==b.layout_id&&a.hardware_id==b.hardware_id&&a.project_name==b.project_name&&a.version==b.version&&a.size==b.size;}
const std::string &session(){
  if(!boot_session.empty())return boot_session;
#ifdef ESP_PLATFORM
  uint8_t bytes[32];esp_fill_random(bytes,sizeof(bytes));constexpr char chars[]="0123456789abcdef";
  for(auto byte:bytes){boot_session+=chars[byte>>4];boot_session+=chars[byte&15];}
#endif
  return boot_session;
}
bool completed_request(const OtaCloudInstallRequest &r,const OtaCloudInventory &inventory,const OtaServiceStatus &status) {
  const auto &image=r.target_role=="boot"?inventory.boot:inventory.application;
  return inventory.ready&&inventory.layout_id==r.layout_id&&inventory.hardware_id==r.hardware_id&&
      status.install_sha256==r.sha256&&status.journal_error.empty()&&status.install_elf_sha256==r.app_elf_sha256&&
      image.confirmed&&image.sha256==r.sha256&&image.app_elf_sha256==r.app_elf_sha256&&
      image.project_name==r.project_name&&image.version==r.version&&
      ((r.target_role=="boot"&&r.layout_id=="lunaros-16m-v3-recovery"&&status.install_phase=="boot_updated"&&inventory.running_role=="application")||
       (status.install_phase=="confirmed"&&inventory.running_role==r.target_role));
}
void reconcile(const OtaCloudInventory &inventory){
  if(!intent.active||intent.stage=="failed")return;
  const auto status=ota_service_status_snapshot();const auto &r=intent.request;
  const bool matching=status.install_sha256==r.sha256;
  const bool idle=!status.upload_active&&!status.pull_active&&!status.pull_queued&&
      !status.restart_pending&&!status.pending_verify;
  if(idle&&matching&&(status.install_phase=="failed"||status.install_phase=="interrupted"||status.install_phase=="boot_mismatch")){
    intent.stage="failed";intent.error=public_error(status.install_error.empty()?status.install_phase:status.install_error);(void)save();return;
  }
  if(intent.stage=="applying"&&status.has_result&&!status.success&&idle){
    intent.stage="failed";intent.error=public_error(status.message);(void)save();return;
  }
  const bool complete=completed_request(r,inventory,status);
  if(complete){if(intent.stage!="complete"){intent.stage="complete";intent.error.clear();(void)save();}restored=false;return;}
  if(restored&&intent.stage=="applying"&&idle){
    intent.stage="failed";intent.error="cloud_ota_interrupted_unconfirmed";(void)save();
  }
}
bool replacement_allowed(const OtaAvailableUpdate &update,const OtaCloudInventory &inventory) {
  if(!inventory.ready||update.cloud_assignment_source!="targeted"||
      !identifier(update.cloud_assignment_id)||!identifier(update.cloud_release_id))return false;
  const auto status=ota_service_status_snapshot();
  if(status.upload_active||status.pull_active||status.pull_queued||status.restart_pending||status.pending_verify)return false;
  if(inventory.running_role=="boot"&&(!hash(update.cloud_recovery_session)||update.cloud_recovery_session!=session()))return false;
  std::string error;
  return ota_cloud_request_allowed(update.role_request,inventory,error);
}
bool scope(){return intent.origin==current_origin()&&intent.epoch==pairing_snapshot().binding_epoch;}
}

void fill_cloud_role_state(JsonObject state){
  std::lock_guard<std::recursive_mutex> lock(mutex);
  const auto inventory=ota_service_cloud_inventory();if(!inventory.ready)return;
  auto target=state["ota"].to<JsonObject>();target["protocol_version"]=2;target["layout_id"]=inventory.layout_id;
  target["hardware_id"]=inventory.hardware_id;target["running_role"]=inventory.running_role;
  if(inventory.running_role=="boot"&&!session().empty())target["recovery_session"]=session();
  auto images=target["images"].to<JsonObject>();
  for(const auto &entry:{std::pair{"boot",&inventory.boot},std::pair{"application",&inventory.application}}){
    if(!entry.second->present)continue;
    auto image=images[entry.first].to<JsonObject>();
    image["project_name"]=entry.second->project_name;image["version"]=entry.second->version;
    image["app_elf_sha256"]=entry.second->app_elf_sha256;image["confirmed"]=entry.second->confirmed;
    if(!entry.second->sha256.empty())image["sha256"]=entry.second->sha256;
  }
  if(!load()||!intent.active||!scope())return;
  reconcile(inventory);
  auto receipt=target["receipt"].to<JsonObject>();receipt["assignment_id"]=intent.assignment;receipt["release_id"]=intent.release;
  receipt["target_role"]=intent.request.target_role;receipt["sha256"]=intent.request.sha256;receipt["app_elf_sha256"]=intent.request.app_elf_sha256;
  const auto status=ota_service_status_snapshot();
  receipt["phase"]=intent.stage=="failed"?"failed":intent.stage=="complete"?
      (intent.request.target_role=="boot"&&intent.request.layout_id=="lunaros-16m-v3-recovery"?"boot_updated":"confirmed"):
      status.install_sha256==intent.request.sha256&&status.install_phase=="boot_selected"?"boot_selected":
      status.install_sha256==intent.request.sha256&&status.install_phase=="writing"?"writing":"downloading";
  if(!intent.error.empty())receipt["error"]=public_error(intent.error);
}

bool decode_cloud_role_offer(JsonVariantConst offer,OtaAvailableUpdate &update,std::string &error){
  if(!offer["protocol_version"].is<unsigned>()||offer["protocol_version"].as<unsigned>()!=2){error="cloud_ota_protocol_invalid";return false;}
  const auto request=request_from(offer);const auto inventory=ota_service_cloud_inventory();
  if(!ota_cloud_request_allowed(request,inventory,error))return false;
  const std::string assignment=offer["assignment_id"]|"",release=offer["release_id"]|"";
  if(!identifier(assignment)||!identifier(release)){error="cloud_ota_assignment_invalid";return false;}
  update.role_aware=true;update.role_request=request;
  update.cloud_assignment_id=assignment;update.cloud_release_id=release;
  update.cloud_assignment_source=offer["assignment_source"]|"";update.cloud_recovery_session=offer["recovery_session"]|"";
  if(update.cloud_assignment_source!="fleet"&&update.cloud_assignment_source!="targeted"){error="cloud_ota_source_invalid";return false;}
  return true;
}

bool cloud_role_offer_admitted(const OtaAvailableUpdate &update){
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!update.role_aware)return false;
  const auto inventory=ota_service_cloud_inventory();
  if(!load())return replacement_allowed(update,inventory);
  reconcile(inventory);
  // Exact offer identity is captured by prepare_cloud_role_install. Admission
  // here blocks failed work and completed repeats before reserving retry budget.
  const bool same_task=intent.active&&scope()&&intent.assignment==update.cloud_assignment_id&&intent.release==update.cloud_release_id&&same(intent.request,update.role_request);
  if(same_task&&(intent.stage=="complete"||intent.stage=="failed"))return false;
  if(inventory.running_role=="boot"){
    const bool continuation=intent.active&&scope()&&intent.assignment==update.cloud_assignment_id&&intent.stage!="failed"&&
        ((same(intent.request,update.role_request)&&intent.release==update.cloud_release_id)||intent.stage=="complete");
    const bool fresh=update.cloud_assignment_source=="targeted"&&hash(update.cloud_recovery_session)&&update.cloud_recovery_session==session()&&
        (!intent.active||intent.assignment!=update.cloud_assignment_id);
    if(!continuation&&!fresh)return false;
  }
  return inventory.ready;
}

bool cloud_role_observe_installed(const OtaAvailableUpdate &update) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!update.role_aware)return false;
  const auto inventory=ota_service_cloud_inventory();
  const auto status=ota_service_status_snapshot();
  if(status.upload_active||status.pull_active||status.pull_queued||status.restart_pending||status.pending_verify||
      !completed_request(update.role_request,inventory,status))return false;
  // Do not require a rewrite just to attach already-proven installed bytes to a
  // new assignment. Admission still enforces a fresh Boot recovery intent.
  if(cloud_role_offer_admitted(update)) {
    std::string error;
    if(prepare_cloud_role_install(update,error))reconcile(inventory);
  }
  return true;
}

bool cloud_role_retry_repair_allowed(const OtaAvailableUpdate &update) {
  if(!update.role_aware||update.cloud_assignment_source!="targeted"||!cloud_role_offer_admitted(update))return false;
  const auto inventory=ota_service_cloud_inventory();
  // Replacing a damaged retry record requires current explicit intent; a
  // historical persisted continuation is insufficient to repair this guard.
  if(inventory.running_role=="boot"&&(!hash(update.cloud_recovery_session)||update.cloud_recovery_session!=session()))return false;
  const auto status=ota_service_status_snapshot();
  return inventory.ready&&!status.upload_active&&!status.pull_active&&!status.pull_queued&&
      !status.restart_pending&&!status.pending_verify;
}
bool reserve_cloud_role_retry(AutomaticOtaRetryGuard &guard,const OtaAvailableUpdate &update,
    const std::string &key,int64_t epoch,uint32_t now) {
  if(cloud_role_handoff_resume(update))return true;
  if(guard.reserve(key,epoch,now))return true;
  const auto error=guard.error();
  const bool damaged=error=="auto_ota_retry_record_invalid"||error=="auto_ota_retry_write_failed"||
      error.rfind("auto_ota_retry_read_failed:",0)==0||error.rfind("auto_ota_retry_reset_failed:",0)==0;
  if(!damaged||!cloud_role_retry_repair_allowed(update))return false;
  return guard.reset()&&guard.reserve(key,epoch,now);
}

bool cloud_role_handoff_resume(const OtaAvailableUpdate &update){
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return update.role_aware&&load()&&intent.active&&scope()&&intent.stage=="handoff"&&intent.assignment==update.cloud_assignment_id&&intent.release==update.cloud_release_id&&same(intent.request,update.role_request)&&ota_service_cloud_inventory().running_role=="boot";
}

// Source/session/assignment are kept separately from OTA's image authority.
bool prepare_cloud_role_install(const OtaAvailableUpdate &update,std::string &error){
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!update.role_aware){error="cloud_ota_intent_unavailable";return false;}
  const bool replacing=!load();
  const auto inventory=ota_service_cloud_inventory();
  if(replacing&&!replacement_allowed(update,inventory)){error="cloud_ota_intent_unavailable";return false;}
  if(!ota_cloud_request_allowed(update.role_request,inventory,error))return false;
  if(!cloud_role_offer_admitted(update)){error="cloud_ota_offer_not_admitted";return false;}
  if(!replacing&&intent.active&&scope()&&intent.stage=="applying"&&same(intent.request,update.role_request)){
    error="cloud_ota_install_already_started";return false;
  }
  Intent next;next.active=true;next.request=update.role_request;next.assignment=update.cloud_assignment_id;
  next.release=update.cloud_release_id;next.source=update.cloud_assignment_source;next.session=update.cloud_recovery_session;
  next.origin=current_origin();next.epoch=pairing_snapshot().binding_epoch;
  next.stage=next.request.target_role=="application"&&inventory.running_role=="application"?"handoff":"applying";
  intent=std::move(next);restored=false;
  if(!save()){error="cloud_ota_intent_write_failed";return false;}
  error.clear();return true;
}

void cloud_role_install_rejected(const std::string &error){
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!load()||!intent.active)return;
  intent.stage="failed";intent.error=public_error(error);(void)save();
}

std::string pending_cloud_ota_platform_origin(){
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!load()||!intent.active)return {};
  return intent.origin;
}
bool clear_cloud_role_intent(){
  std::lock_guard<std::recursive_mutex> lock(mutex);
  intent={};loaded=true;restored=false;return save();
}
}
namespace lunaredu::lunaros {
std::string platform_link_pending_ota_origin(){return platform_link::pending_cloud_ota_platform_origin();}
}
