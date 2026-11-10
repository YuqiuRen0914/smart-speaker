#include <cassert>
#include <iostream>
#include <vector>
#include "lunaredu_lunaros/app/app_capability_json.h"
#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include "lunaredu_lunaros/device_link/device_link.h"
#include "lunaredu_clock_capabilities.h"
#include "lunaredu_clock_time_sync.h"
#include "lunaredu_calendar_capabilities.h"
#include "lunaredu_files_capabilities.h"
#include "lunaredu_settings_capabilities.h"
#include "lunaredu_settings_maintenance_diagnostics.h"
#include "lunaredu_eyes_capabilities.h"
#include "lunaredu_eyes_runtime.h"
#include "lunaredu_eyes_companion_capabilities.h"
#include "lunaredu_motion_service.h"
#include "esp_timer.h"

using namespace lunaredu;
namespace {
bool storage_online=false, display_online=false, installed=false, enabled=true, companion=false;
bool last_sync_force=false;
int expression_changes=0, pose_changes=0, sync_requests=0;
lunaros::NetworkSnapshot network;
eyes::CompanionLibrarySnapshot pairing;
clock::TimeSyncSnapshot sync_state;
}
namespace lunaredu::lunaros {
NetworkSnapshot network_snapshot() {return network;}
bool storage_ready() {return storage_online;}
std::string storage_normalize_path(const char *path) {return path;}
bool storage_is_safe_path(const char *path) {return std::string(path).find("..") == std::string::npos;}
bool storage_list_entries(const char *path,StorageEntryVisitor visitor,void *context) {
 for(int i=0;i<11;++i) {
  StorageListEntry entry; entry.path=std::string(path)+"/file"+std::to_string(i)+".py"; entry.size_bytes=123;
  if(!visitor(entry,context)) return false;
 }
 return true;
}
}
namespace lunaredu::clock {
void request_time_sync(bool force) {last_sync_force=force;++sync_requests;sync_state.state=TimeSyncState::Pending;}
TimeSyncSnapshot time_sync_snapshot() {return sync_state;}
}
namespace lunaredu::factory {
DiagnosticsSnapshot diagnostics_snapshot() {return {};}
}
namespace lunaredu::eyes {
EmotionEyeRuntimeStatus emotion_eye_runtime_status() {EmotionEyeRuntimeStatus state;state.available=display_online;return state;}
bool emotion_eye_runtime_get_theme_state(const char *id,EmotionEyeConfig &,int16_t &,int16_t &,bool &is_enabled,bool &active,std::string &name,std::string &description,std::string *error) {
 if(!installed) {*error="eye_theme_not_installed";return false;}
 is_enabled=enabled;active=false;name=id;description="test expression";return true;
}
bool emotion_eye_runtime_apply_theme_state(const char *,std::string *) {++expression_changes;return true;}
}

int main() {
 std::string error;
 assert(calendar::register_calendar_capabilities(error));
 assert(!app::seal_capabilities(error));
 assert(error == "capability_dependency_missing:calendar:clock.time.get");
 assert(clock::register_clock_capabilities(error));
 assert(files::register_files_capabilities(error));
 assert(settings::register_settings_capabilities(error));
 assert(eyes::register_eyes_capabilities(error));
 assert(eyes::register_companion_capabilities(error));
 assert(motion::declare_motion_capabilities(error));
 assert(app::seal_capabilities(error)); // Devices can be offline during declaration.
 assert(!app::capability_available(motion::kControlCapability, "pet"));
 motion::ServiceConfig config; config.head_ready=config.chassis_ready=true;
 assert(motion::setup_service(config,error));
 assert(motion::setup_service(config,error)); // Restart cannot add/change a provider.
 assert(app::capabilities_sealed());
 JsonDocument args,result;
 auto call=[&](const char *id,const char *json="{}",const char *caller="pet",bool changes=true) {
  assert(deserializeJson(args,json)==DeserializationError::Ok);
  const bool ok=app::call_json_capability(caller,id,1,args.as<JsonObjectConst>(),result.to<JsonObject>(),error,changes);
  if(!ok) assert(result.size()==0);
  return ok;
 };
 for(const char *provider:{"clock","calendar","files","settings","eyes","motion"}) {
  assert(app::list_json_capabilities("pet",provider,0,8,result.to<JsonObject>(),error));
  assert(result["total"].as<int>()>0);
 }
 assert(call("clock.time.get")); assert(result["valid"]==false && result["epoch_seconds"]==0);
 assert(!call("calendar.today.get")); assert(error=="calendar_clock_not_synchronized");
 network.clock=lunaros::NetworkClockState::Valid;
 const bool date_ok=call("calendar.today.get");
 if(!date_ok) std::cerr<<"Calendar error: "<<error<<"\n";
 assert(date_ok); assert(result["date"].as<std::string>().size()==10);
 assert(result["utc_offset"].as<std::string>().size()==5);
 assert(!call("clock.sync.request","{}","pet",false)); assert(sync_requests==0);
 assert(call("clock.sync.request")); assert(result["sync_state"]=="pending" && sync_requests==1);
 assert(last_sync_force);
 assert(call("clock.sync.request",R"({"force":false})","calendar")); assert(!last_sync_force);
 assert(call("settings.diagnostics.get","{}","pet",false)); assert(result["tests"].size()==4);

 assert(!call("files.entries.list",R"({"directory":"/scripts"})"));
 storage_online=true;
 assert(!call("files.entries.list",R"({"directory":"/private"})"));
 assert(!call("files.entries.list",R"({"directory":"/scripts/../private"})"));
 assert(!call("files.entries.list",R"({"directory":"/scripts","limit":9})"));
 assert(call("files.entries.list",R"({"directory":"/scripts"})"));
 assert(result["entries"].size()==8 && result["next_offset"]==8 && result["total"]==11);
 assert(call("files.entries.list",R"({"directory":"/scripts","offset":8})"));
 assert(result["entries"].size()==3 && result["next_offset"].isNull());

 assert(call("eyes.expressions.list")); assert(result["installed"]==false);
 installed=display_online=true;
 assert(call("eyes.expressions.list")); assert(result["expressions"].size()==4 && result["next_offset"]==4);
 assert(call("eyes.expressions.list",R"({"offset":24})")); assert(result["expressions"].size()==1 && result["next_offset"].isNull());
 enabled=false;
 assert(!call("eyes.expression.show",R"({"state_id":"happy"})")); assert(expression_changes==0);
 enabled=true;
 assert(!call("eyes.expression.show",R"({"state_id":"unknown"})")); assert(expression_changes==0);
 assert(call("eyes.expression.show",R"({"state_id":"happy"})")); assert(expression_changes==1);
 eyes::configure_capability_presentation({[]{return companion;},nullptr});
 companion=true;
 assert(!call("eyes.expression.show",R"({"state_id":"happy"})")); assert(expression_changes==1);

 assert(call("eyes.poses.list")); assert(result["available"]==false);
 assert(!call("eyes.pose.show",R"({"pose_id":"happy"})"));
 pairing.companion_available=pairing.companion_animation_available=true;
 pairing.companion_animation_count=1; pairing.companion_animations[0]={"happy",4,100};
 eyes::configure_companion_capabilities({[]{return companion;},[](const std::string &,uint32_t,std::string &){++pose_changes;return true;},[]{return pairing;}});
 assert(call("eyes.poses.list")); assert(result["poses"].size()==1);
 assert(!call("eyes.pose.show",R"({"pose_id":"nap"})")); assert(pose_changes==0);
 assert(!call("eyes.pose.show",R"({"pose_id":"happy","duration_ms":10001})"));
 assert(call("eyes.pose.show",R"({"pose_id":"happy"})")); assert(pose_changes==1);

 assert(!call("motion.activity.start",R"({"chassis":true,"left_speed":2})"));
 assert(!call("motion.activity.start",R"({"head":true,"left_speed":1})"));
 assert(!motion::state("pet").activity_active);
 assert(call("motion.activity.start",R"({"chassis":true,"left_speed":1,"right_speed":1,"duration_ms":200})"));
 const auto id=result["activity_id"].as<std::string>();
 const auto cancel_json="{\"activity_id\":\""+id+"\"}";
 assert(motion::state("pet").activity_active);
 assert(call("motion.activity.cancel",cancel_json.c_str(),"micropython"));
 assert(motion::state("pet").activity_active); // Foreign cancellation is a no-op.
 test_time_us+=201000;
 assert(call("motion.state.get")); assert(result["active"]==false && result["left_speed"]==0);
 assert(call("motion.activity.start",R"({"head":true,"pan":0.5})"));
 const auto newer=result["activity_id"].as<std::string>();
 assert(newer!=id);
 assert(call("motion.activity.cancel",cancel_json.c_str()));
 assert(result["activity_id"].as<std::string>()==newer && result["active"]==true);
 assert(!call("motion.activity.start",R"({"head":true})","unknown"));
 std::cout<<"All additional app providers: discovery, nested contracts, availability, pagination, presentation and motion ownership passed\n";
}
