#include "lunaredu_pet_application.h"
#include "lunaredu_pet_interaction.h"
#include "lunaredu_pet_emotion.h"
#include "lunaredu_pet_snapshot.h"
#include "lunaredu_lunaros/app/app_capability_json.h"
namespace lunaredu::pet {
namespace {
using namespace app;
bool ready(void *,std::string &error) {if(snapshot().active)return true;error="pet_runtime_inactive";return false;}
bool read(void *,const char *,JsonObjectConst,JsonObject out,std::string &) {
 const auto s=snapshot();out["mood"]=pet_mood_name(s.pet.mood);out["active"]=s.active;out["last_event"]=s.last_event;return true;
}
bool interact(void *,const char *,JsonObjectConst args,JsonObject out,std::string &error) {
 lunaredu::presentation::Expression expression;
 const char *emotion=args["emotion"].as<const char *>();
 if(!try_pet_expression_from_emotion(emotion,expression)){error="unknown_pet_interaction";return false;}
 if(!post_touch_reaction(emotion)){error="pet_interaction_queue_full";return false;}
 out["queued"]=true;return true;
}
const CapabilityField state_fields[]={{"mood",CapabilityFieldType::String},{"active",CapabilityFieldType::Boolean},{"last_event",CapabilityFieldType::String}};
const CapabilityField input[]={{"emotion",CapabilityFieldType::String,true,"Semantic interaction",0,0,1,32}};
const CapabilityField output[]={{"queued",CapabilityFieldType::Boolean}};
const char *const callers[]={"pet","product","device_api","micropython","eyes"};
const JsonCapabilityDefinition definitions[]={
 {"pet.state.get","pet",1,"宠物状态","读取宠物自身状态。",CapabilityExecution::Query,{}, {state_fields,3},read,nullptr,nullptr,callers,5},
 {"pet.interact","pet",1,"宠物互动","提交语义互动，queued 表示已入队。",CapabilityExecution::Command,{input,1},{output,1},interact,ready,nullptr,callers,5}};
}
bool register_application_capabilities(std::string &error) {
 // Pet discovers additional tools at runtime. These are optional integrations,
 // not a second permission list; product policy and provider ACLs authorize calls.
 static constexpr app::CapabilityDependency dependencies[] = {
     {"eyes.expression.show", 1, false},
     {"motion.activity.start", 1, false},
     {"performance.activity.start", 1, false},
 };
 if (!app::register_capability_consumer({"pet", dependencies, 3}, error)) return false;
 for (const auto &definition : definitions) {
   if (!app::register_json_capability(definition, error)) return false;
 }
 return true;
}
}
