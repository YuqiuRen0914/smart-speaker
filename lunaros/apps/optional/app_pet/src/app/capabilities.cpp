#include "lunaredu_pet_application.h"
#include "app/setup_session.h"
#include "lunaredu_pet_interaction.h"
#include "behavior/emotion.h"
#include "lunaredu_pet_snapshot.h"
#include "app/codec/app_capability_json.h"
#include "runtime/runtime.h"
#include "lunaredu_pet_catalog.h"
#include <cstring>
#include <iterator>
namespace lunaredu::pet {
namespace {
using namespace app;
bool ready([[maybe_unused]] app::CapabilityRegistry &catalog, void *,std::string &error) {if(snapshot().active)return true;error="pet_runtime_inactive";return false;}
bool read([[maybe_unused]] app::CapabilityRegistry &catalog, void *,const char *,JsonObjectConst,JsonObject out,std::string &) {
 const auto s=snapshot();
 out["mood"]=pet_mood_name(s.pet.mood);out["active"]=s.active;out["last_event"]=s.last_event;
 out["mood_enabled"]=s.mood_enabled;
 out["autonomous_appearance_enabled"]=s.autonomous_appearance_enabled;
 out["valence"]=s.pet.affect.valence;out["arousal"]=s.pet.affect.arousal;
 out["affinity"]=s.pet.affect.affinity;out["energy"]=s.pet.affect.energy;
 out["intensity_percent"]=s.presentation.intensity_percent;
 out["conversation_turns"]=s.pet.affect.conversation_turns;
 out["appearance_mode"]=pet_appearance_mode_name(s.pet.appearance.mode);
 out["shape"]=s.presentation.shape;out["color"]=s.presentation.color;
 out["expression_state"]=s.presentation.expression_state;
 out["activity_state"]=s.activity_state;return true;
}
bool interact([[maybe_unused]] app::CapabilityRegistry &catalog, void *,const char *,JsonObjectConst args,JsonObject out,std::string &error) {
 const char *emotion=args["emotion"].as<const char *>();
 const char *theme=nullptr;
 if(emotion==nullptr||!try_pet_theme_state_from_emotion(emotion,theme)){error="unknown_pet_interaction";return false;}
 if(!post_touch_reaction(emotion)){error="pet_interaction_queue_full";return false;}
 out["queued"]=true;return true;
}
bool set_appearance([[maybe_unused]] app::CapabilityRegistry &catalog, void *,const char *,JsonObjectConst args,JsonObject out,std::string &error) {
 appearance::Update update;
 if(args["mode"].is<const char *>()) {
  const char *mode=args["mode"].as<const char *>();update.mode_explicit=true;
  if(std::strcmp(mode,"autonomous")==0)update.mode=PetAppearanceMode::Autonomous;
  else if(std::strcmp(mode,"temporary")==0)update.mode=PetAppearanceMode::Temporary;
  else if(std::strcmp(mode,"locked")==0)update.mode=PetAppearanceMode::Locked;
  else {error="invalid_pet_appearance_mode";return false;}
 }
 if(args["expression"].is<const char *>()){
  const char *expression=args["expression"].as<const char *>();
  if(!catalog::character_catalog_contains(catalog::kCharacterStates,expression)){error="invalid_pet_expression";return false;}
  update.expression=expression;
 }
 if(args["action"].is<const char *>()){error="unsupported_pet_action";return false;}
 if(args["shape"].is<const char *>())update.shape=args["shape"].as<const char *>();
 if(args["color"].is<const char *>())update.color=args["color"].as<const char *>();
 if(!runtime::update_appearance(update,error))return false;
 const auto s=snapshot();out["applied"]=true;
 out["mode"]=pet_appearance_mode_name(s.pet.appearance.mode);
 out["shape"]=s.presentation.shape;out["color"]=s.presentation.color;
 out["expression"]=s.presentation.expression_state;
 out["revision"]=s.presentation.revision;
 return true;
}
template <size_t Size>
void add_catalog(JsonArray output, const std::array<const char *, Size> &values) {
 for(const char *value:values)output.add(value);
}
bool read_appearance_catalog([[maybe_unused]] app::CapabilityRegistry &catalog, void *,const char *,JsonObjectConst,JsonObject out,std::string &) {
 add_catalog(out["expressions"].to<JsonArray>(),catalog::kCharacterStates);
 add_catalog(out["shapes"].to<JsonArray>(),catalog::kCharacterShapes);
 add_catalog(out["colors"].to<JsonArray>(),catalog::kCharacterColors);
 add_catalog(out["conversation_states"].to<JsonArray>(),catalog::kCharacterConversationStates);
 add_catalog(out["task_states"].to<JsonArray>(),catalog::kCharacterTaskStates);
 add_catalog(out["system_states"].to<JsonArray>(),catalog::kCharacterSystemStates);
 add_catalog(out["notice_states"].to<JsonArray>(),catalog::kCharacterNoticeStates);
 add_catalog(out["reactions"].to<JsonArray>(),catalog::kCharacterReactionStates);
 add_catalog(out["visual_cues"].to<JsonArray>(),catalog::kCharacterCueStates);
 add_catalog(out["interaction_demos"].to<JsonArray>(),catalog::kCharacterInteractionDemoStates);
 out["eye_contours"]=25;out["custom_color"]=true;
 out["project_format"]="lunaredu.xiaoxing.character.v1";out["project_maximum_bytes"]=24576;return true;
}
const CapabilityField state_fields[]={
 {"mood_enabled",CapabilityFieldType::Boolean},{"autonomous_appearance_enabled",CapabilityFieldType::Boolean},
 {"mood",CapabilityFieldType::String},{"active",CapabilityFieldType::Boolean},{"last_event",CapabilityFieldType::String},
 {"valence",CapabilityFieldType::Number},{"arousal",CapabilityFieldType::Number},{"affinity",CapabilityFieldType::Number},
 {"energy",CapabilityFieldType::Number},{"intensity_percent",CapabilityFieldType::Integer},
 {"conversation_turns",CapabilityFieldType::Integer},{"appearance_mode",CapabilityFieldType::String},
 {"shape",CapabilityFieldType::String},{"color",CapabilityFieldType::String},{"expression_state",CapabilityFieldType::String},
 {"activity_state",CapabilityFieldType::String}};
const CapabilityField input[]={{"emotion",CapabilityFieldType::String,true,"Semantic interaction",0,0,1,32}};
const CapabilityField output[]={{"queued",CapabilityFieldType::Boolean}};
const char *const modes[]={"autonomous","temporary","locked"};
const CapabilityField appearance_input[]={
 {"mode",CapabilityFieldType::String,false,"自主、临时或锁定",0,0,1,16,modes,3},
 {"expression",CapabilityFieldType::String,false,"一次性角色状态；先读取 pet.appearance.catalog",0,0,1,24},
 {"shape",CapabilityFieldType::String,false,"身体形状",0,0,1,16,catalog::kCharacterShapes.data(),catalog::kCharacterShapes.size()},
 {"color",CapabilityFieldType::String,false,"纯色名称或 #RRGGBB",0,0,1,16}};
const CapabilityField appearance_output[]={
 {"applied",CapabilityFieldType::Boolean},{"mode",CapabilityFieldType::String},
 {"shape",CapabilityFieldType::String},{"color",CapabilityFieldType::String},
 {"expression",CapabilityFieldType::String},
 {"revision",CapabilityFieldType::Integer}};
const CapabilityField catalog_string_item{"value",CapabilityFieldType::String};
const CapabilityField catalog_output[]={
 array_field("expressions",catalog_string_item,catalog::kCharacterStates.size()),
 array_field("shapes",catalog_string_item,catalog::kCharacterShapes.size()),
 array_field("colors",catalog_string_item,catalog::kCharacterColors.size()),
 array_field("conversation_states",catalog_string_item,catalog::kCharacterConversationStates.size()),
 array_field("task_states",catalog_string_item,catalog::kCharacterTaskStates.size()),
 array_field("system_states",catalog_string_item,catalog::kCharacterSystemStates.size()),
 array_field("notice_states",catalog_string_item,catalog::kCharacterNoticeStates.size()),
 array_field("reactions",catalog_string_item,catalog::kCharacterReactionStates.size()),
 array_field("visual_cues",catalog_string_item,catalog::kCharacterCueStates.size()),
 array_field("interaction_demos",catalog_string_item,catalog::kCharacterInteractionDemoStates.size()),
 {"eye_contours",CapabilityFieldType::Integer},{"custom_color",CapabilityFieldType::Boolean},
 {"project_format",CapabilityFieldType::String},{"project_maximum_bytes",CapabilityFieldType::Integer}};
const char *const callers[]={"pet","assistant","product","device_api","micropython"};
const JsonCapabilityDefinition definitions[]={
 {"pet.state.get","pet",1,"宠物状态","读取情绪、任务状态、外观和表现状态；mood_enabled 仅控制自主情绪，不控制真实任务状态。",CapabilityExecution::Query,{}, {state_fields,std::size(state_fields)},read,nullptr,nullptr,callers,5},
 {"pet.appearance.catalog","pet",2,"角色表现目录","按会话、任务、系统、通知、情绪、提示和演示分组；提示与演示不代表自动触发。",CapabilityExecution::Query,{}, {catalog_output,std::size(catalog_output)},read_appearance_catalog,nullptr,nullptr,callers,5},
 {"pet.interact","pet",1,"宠物互动","提交语义互动，queued 表示已入队。",CapabilityExecution::Command,{input,1},{output,1},interact,ready,nullptr,callers,5},
 {"pet.appearance.set","pet",1,"宠物外观策略","设置临时或锁定外观，或恢复自主选择；表情是一次性的。",CapabilityExecution::Command,{appearance_input,std::size(appearance_input)},{appearance_output,std::size(appearance_output)},set_appearance,ready,nullptr,callers,5}};
}
bool register_application_capabilities(lunaredu::app::CapabilityRegistry &catalog, std::string &error) {
 if(!register_setup_capabilities(catalog,error))return false;
 for (const auto &definition : definitions) {
   if (!app::register_json_capability(catalog, definition, error)) return false;
 }
 return true;
}
}
